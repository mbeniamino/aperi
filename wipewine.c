#define _GNU_SOURCE 1
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

/* Functions for a basic implementation of the systemd notify protocol. These
 * are derived from the standalone C implementation found in the sd_notify(3)
 * man page. */

#define _cleanup_(f) __attribute__((cleanup(f)))

static void closep(int *fd) {
    if (!fd || *fd < 0) return;

    close(*fd);
    *fd = -1;
}

static int notify(const char *message) {
    union sockaddr_union {
        struct sockaddr sa;
        struct sockaddr_un sun;
    } socket_addr = {
        .sun.sun_family = AF_UNIX,
    };
    size_t path_length, message_length;
    _cleanup_(closep) int fd = -1;
    const char *socket_path;

    /* Verify the argument first */
    if (!message) return -EINVAL;

    message_length = strlen(message);
    if (message_length == 0) return -EINVAL;

    /* If the variable is not set, the protocol is a noop */
    socket_path = getenv("NOTIFY_SOCKET");
    if (!socket_path) return 0; /* Not set? Nothing to do */

    /* Only AF_UNIX is supported, with path or abstract sockets */
    if (socket_path[0] != '/' && socket_path[0] != '@') {
        return -EAFNOSUPPORT;
    }

    path_length = strlen(socket_path);
    /* Ensure there is room for NUL byte */
    if (path_length >= sizeof(socket_addr.sun.sun_path)) {
        return -E2BIG;
    }

    memcpy(socket_addr.sun.sun_path, socket_path, path_length);

    /* Support for abstract socket */
    if (socket_addr.sun.sun_path[0] == '@')
        socket_addr.sun.sun_path[0] = 0;

    fd = socket(AF_UNIX, SOCK_DGRAM|SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -errno;

    if (connect(fd, &socket_addr.sa,
        offsetof(struct sockaddr_un, sun_path) + path_length) != 0) {
        return -errno;
    }

    ssize_t written = write(fd, message, message_length);
    if (written != (ssize_t) message_length) {
        return written < 0 ? -errno : -EPROTO;
    }

    return 1; /* Notified! */
}

static int notify_ready(void) {
    return notify("READY=1");
}

static int notify_stopping(void) {
    return notify("STOPPING=1");
}

static volatile sig_atomic_t terminating = 0;

static void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) terminating = 1;
}

/* strtcpy and stpecpy implementation as found in the string_copying(7) man page */
ssize_t strtcpy(char *restrict dst, const char *restrict src, size_t dsize) {
   bool    trunc;
   size_t  dlen, slen;

   if (dsize == 0) {
       errno = ENOBUFS;
       return -1;
   }

   slen = strnlen(src, dsize);
   trunc = (slen == dsize);
   dlen = slen - trunc;

   stpcpy(mempcpy(dst, src, dlen), "");
   if (trunc)
       errno = E2BIG;
   return trunc ? -1 : slen;
}

char* stpecpy(char *dst, char* end, const char *restrict src) {
   size_t  dlen;

   if (dst == NULL)
       return NULL;

   dlen = strtcpy(dst, src, end - dst);
   return (dlen == -1) ? NULL : dst + dlen;
}

/* Other functions */

// Print errno in human readable form and exit with an error code
void perror_exit(const char* s) {
    perror(s);
    exit(EXIT_FAILURE);
}

/* Path to $XDG_DATA_HOME/applications. If $XDG_DATA_HOME is not set, use
   $HOME/.local/share instead. If $HOME is not set use /home/<userid> . */
char* xdg_applications() {
    const size_t BUFSIZE=2048;
    char* res = (char*)malloc(BUFSIZE);
    char* end = res + BUFSIZE;
    char* xdg_data_home = getenv("XDG_DATA_HOME");
    char* p;
    if (xdg_data_home) {
        // XDG_DATA_HOME set: use it
        p = strncpy(res, xdg_data_home, BUFSIZE-1);
    } else {
        // XDG_DATA_HOME not set: use $HOME/.local/share
        char* home = getenv("HOME");
        if (!home) {
            // HOME not set: use /home/<userid>
            p = strncpy(res, "/home/", BUFSIZE) + 6;
            if (!getlogin_r(p, BUFSIZE - 6)) {
                perror_exit("getlogin_r");
            }
            p = res + strlen(res);
        } else {
            // Set $HOME
            p = stpecpy(res, end, home);
        }
        p = stpecpy(p, end, "/.local/share");
    }
    p = stpecpy(p, end, "/applications");
    return res;
}

/* Main */
#define BUF_LEN (10 * (sizeof(struct inotify_event) + NAME_MAX + 1))
int main(int argc, char *argv[]) {
    char buf[BUF_LEN] __attribute__ ((aligned(8)));
    ssize_t numRead;

    /* Setup signal handler */
    struct sigaction sa = {
        .sa_handler = signal_handler,
        .sa_flags = SA_RESTART,
    };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    // Initialize inotify
    int inotify_fd = inotify_init();
    if (inotify_fd == -1) perror_exit("inotify_init");

    // Add watch on creation of files in $XDG_DATA_HOME/applications dir
    char* xdg_data_home_path = xdg_applications();
    int wd = inotify_add_watch(inotify_fd, xdg_data_home_path, IN_CREATE);
    if (wd == -1) perror_exit("inotify_add_watch");

    /* change dir to xdg_applications so that we can later call unlink on the event->name
     * directly, without the need to allocate and concatenate c strings */
    if (chdir(xdg_data_home_path)) perror_exit("cd");

    // Notify systemd we are ready
    int r = notify_ready();
    if (r < 0) {
        fprintf(stderr, "Failed to notify readiness to $NOTIFY_SOCKET: %s\n", strerror(-r));
        return EXIT_FAILURE;
    }

    fd_set set;
    char *p;
    while (!terminating) {
        /* wait for inotify event or a signal */
        FD_ZERO(&set);
        FD_SET(inotify_fd, &set);
        int ret = pselect(inotify_fd + 1, &set, NULL, NULL, NULL, NULL);

        if (ret == -1 && errno == EINTR) {
            // Signal: do nothing, let the signal handler do its job
        } else if (ret == -1) {
            perror_exit("pselect");
        } else {
            numRead = read(inotify_fd, buf, BUF_LEN);
            if (numRead == 0) {
                fprintf(stderr, "read() from inotify fd returned 0!\n");
                exit(EXIT_FAILURE);
            }
            if (numRead == -1) perror_exit("read");

            /* Process all of the events in buffer returned by read() */
            for (p = buf; p < buf + numRead; ) {
                struct inotify_event *event = (struct inotify_event *)p;

                p += sizeof(struct inotify_event) + event->len;
                // if the file matches wine-extension*.desktop: unlink it at once
                if ((strncmp("wine-extension", event->name, 14) == 0 ||
                     strncmp("wine-protocol", event->name, 13) == 0) &&
                    strncmp(".desktop", event->name + strlen(event->name) - 8, 8) == 0) {
                    if (unlink(event->name)) {
                        fprintf(stderr, "Error unlinking %s: ", event->name);
                        perror("");
                    } else {
                        printf("Unlinked %s\n", event->name);
                        fflush(stdout);
                    }
                }
            }
        }
    }

    // Notify systemd we are stopping
    r = notify_stopping();
    if (r < 0) {
        fprintf(stderr, "Failed to report termination to $NOTIFY_SOCKET: %s\n", strerror(-r));
        return EXIT_FAILURE;
    }

    // Final clean up and exit
    inotify_rm_watch(inotify_fd, wd);
    close(inotify_fd);
    free(xdg_data_home_path);
    return EXIT_SUCCESS;
}

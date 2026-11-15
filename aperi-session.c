/* aperi-session: session helper for aperi, meant to be started at the
 * beginning of the graphical session by a systemd user unit.
 *
 * It provides two independent features, each of which can be disabled:
 *
 *  - fm1: a minimal org.freedesktop.FileManager1 D-Bus service implementing
 *    ShowItems by spawning `aperi aperi-show-items://<path>`
 *    (only available when built with D-Bus support);
 *  - wipewine: removal of the wine-extension*.desktop and
 *    wine-protocol*.desktop files wine creates in
 *    $XDG_DATA_HOME/applications, as soon as they are created.
 *
 * Both features share a single poll(2) based event loop. Readiness and
 * termination are reported to systemd via the sd_notify protocol, when run
 * under systemd. It can also be run without systemd (e.g. from the compositor
 * startup commands): only one instance per user is allowed, and with
 * --exit-with-session it terminates when the Wayland compositor goes away. */

#define _GNU_SOURCE 1
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef HAVE_DBUS
#include <dbus/dbus.h>
#endif

#include "config.h"

#define _cleanup_(f) __attribute__((cleanup(f)))

/* ------------------------------------------------------------------------ */
/* systemd notify protocol, derived from the standalone C implementation    */
/* found in the sd_notify(3) man page.                                      */
/* ------------------------------------------------------------------------ */

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

    if (!message) return -EINVAL;

    message_length = strlen(message);
    if (message_length == 0) return -EINVAL;

    /* If the variable is not set, the protocol is a noop */
    socket_path = getenv("NOTIFY_SOCKET");
    if (!socket_path) return 0;

    /* Only AF_UNIX is supported, with path or abstract sockets */
    if (socket_path[0] != '/' && socket_path[0] != '@') return -EAFNOSUPPORT;

    path_length = strlen(socket_path);
    /* Ensure there is room for NUL byte */
    if (path_length >= sizeof(socket_addr.sun.sun_path)) return -E2BIG;

    memcpy(socket_addr.sun.sun_path, socket_path, path_length);

    /* Support for abstract socket */
    if (socket_addr.sun.sun_path[0] == '@') socket_addr.sun.sun_path[0] = 0;

    fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -errno;

    if (connect(fd, &socket_addr.sa,
                offsetof(struct sockaddr_un, sun_path) + path_length) != 0)
        return -errno;

    ssize_t written = write(fd, message, message_length);
    if (written != (ssize_t)message_length)
        return written < 0 ? -errno : -EPROTO;

    return 1;
}

/* ------------------------------------------------------------------------ */
/* wipewine                                                                 */
/* ------------------------------------------------------------------------ */

/* Return $XDG_DATA_HOME/applications, falling back to
 * $HOME/.local/share/applications. Caller frees. */
static char *xdg_applications(void) {
    const char *xdg_data_home = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    char *res = NULL;
    int n;

    if (xdg_data_home && xdg_data_home[0] == '/') {
        n = asprintf(&res, "%s/applications", xdg_data_home);
    } else if (home && home[0]) {
        n = asprintf(&res, "%s/.local/share/applications", home);
    } else {
        fprintf(stderr, "Neither XDG_DATA_HOME nor HOME are set\n");
        return NULL;
    }
    if (n < 0) {
        perror("asprintf");
        return NULL;
    }
    return res;
}

/* Return true if `name` matches a wine association desktop file */
static bool is_wine_desktop_file(const char *name) {
    size_t len = strlen(name);
    return (strncmp("wine-extension", name, 14) == 0 ||
            strncmp("wine-protocol", name, 13) == 0) &&
           len >= 8 && strcmp(".desktop", name + len - 8) == 0;
}

static void unlink_at(int dirfd, const char *name) {
    if (unlinkat(dirfd, name, 0)) {
        if (errno != ENOENT)
            fprintf(stderr, "wipewine: error unlinking %s: %s\n", name, strerror(errno));
    } else {
        printf("wipewine: unlinked %s\n", name);
        fflush(stdout);
    }
}

/* Remove all the wine association desktop files already present */
static void wipewine_sweep(int dirfd) {
    int fd = dup(dirfd);
    if (fd < 0) {
        perror("dup");
        return;
    }
    DIR *dfd = fdopendir(fd);
    if (!dfd) {
        perror("fdopendir");
        close(fd);
        return;
    }
    struct dirent *dp;
    while ((dp = readdir(dfd)) != NULL)
        if (is_wine_desktop_file(dp->d_name)) unlink_at(dirfd, dp->d_name);
    closedir(dfd);
}

struct wipewine {
    int inotify_fd;
    int dir_fd;
};

static int wipewine_init(struct wipewine *w) {
    w->inotify_fd = -1;
    w->dir_fd = -1;

    char *path = xdg_applications();
    if (!path) return -1;

    w->inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (w->inotify_fd < 0) {
        perror("inotify_init1");
        goto fail;
    }
    /* IN_MOVED_TO catches files written to a temporary name and renamed */
    if (inotify_add_watch(w->inotify_fd, path, IN_CREATE | IN_MOVED_TO) < 0) {
        fprintf(stderr, "wipewine: cannot watch %s: %s\n", path, strerror(errno));
        goto fail;
    }
    w->dir_fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (w->dir_fd < 0) {
        fprintf(stderr, "wipewine: cannot open %s: %s\n", path, strerror(errno));
        goto fail;
    }
    free(path);

    /* Watch is in place: clean up whatever is already there */
    wipewine_sweep(w->dir_fd);
    return 0;

fail:
    free(path);
    closep(&w->inotify_fd);
    closep(&w->dir_fd);
    return -1;
}

/* Returns -1 on fatal error */
static int wipewine_handle(struct wipewine *w) {
    char buf[10 * (sizeof(struct inotify_event) + NAME_MAX + 1)]
        __attribute__((aligned(__alignof__(struct inotify_event))));

    for (;;) {
        ssize_t n = read(w->inotify_fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR) return 0;
            perror("wipewine: read");
            return -1;
        }
        if (n == 0) {
            fprintf(stderr, "wipewine: 0 bytes read from inotify fd\n");
            return -1;
        }
        for (char *p = buf; p < buf + n;) {
            struct inotify_event *ev = (struct inotify_event *)p;
            p += sizeof(struct inotify_event) + ev->len;
            if (ev->mask & IN_IGNORED) {
                fprintf(stderr, "wipewine: watched directory was removed\n");
                return -1;
            }
            if (ev->mask & IN_Q_OVERFLOW) {
                wipewine_sweep(w->dir_fd);
                continue;
            }
            if (ev->len && is_wine_desktop_file(ev->name))
                unlink_at(w->dir_fd, ev->name);
        }
    }
}

static void wipewine_free(struct wipewine *w) {
    closep(&w->inotify_fd);
    closep(&w->dir_fd);
}

/* ------------------------------------------------------------------------ */
/* fm1: org.freedesktop.FileManager1                                        */
/* ------------------------------------------------------------------------ */

#ifdef HAVE_DBUS
#define FM1_NAME "org.freedesktop.FileManager1"
#define FM1_INTERFACE "org.freedesktop.FileManager1"
#define SCHEMA "aperi-show-items"

/* Spawn `aperi aperi-show-items://<path>` fully detached (double fork) */
static void spawn_aperi(const char *uri) {
    /* "file://..." -> "aperi-show-items://..." */
    char *arg = NULL;
    if (asprintf(&arg, "%s%s", SCHEMA, uri + 4) < 0) {
        perror("asprintf");
        return;
    }
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
    } else if (pid == 0) {
        setsid();
        if (fork() == 0) {
            sigset_t mask;
            sigemptyset(&mask);
            sigprocmask(SIG_SETMASK, &mask, NULL);
            execlp("aperi", "aperi", arg, (char *)NULL);
            perror("fm1: execlp aperi");
            _exit(127);
        }
        _exit(0);
    } else {
        while (waitpid(pid, NULL, 0) < 0 && errno == EINTR)
            ;
    }
    free(arg);
}

static DBusHandlerResult fm1_filter(DBusConnection *conn, DBusMessage *msg, void *data) {
    (void)data;
    if (!dbus_message_is_method_call(msg, FM1_INTERFACE, "ShowItems"))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    DBusMessageIter args;
    if (!dbus_message_iter_init(msg, &args) ||
        dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_ARRAY) {
        DBusMessage *err = dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                                  "Expected (as uris, s startup_id)");
        if (err) {
            dbus_connection_send(conn, err, NULL);
            dbus_message_unref(err);
        }
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    DBusMessageIter sub;
    dbus_message_iter_recurse(&args, &sub);
    while (dbus_message_iter_get_arg_type(&sub) == DBUS_TYPE_STRING) {
        const char *uri;
        dbus_message_iter_get_basic(&sub, &uri);
        if (strncmp(uri, "file://", 7) == 0) spawn_aperi(uri);
        dbus_message_iter_next(&sub);
    }

    DBusMessage *reply = dbus_message_new_method_return(msg);
    if (!reply || !dbus_connection_send(conn, reply, NULL)) {
        fprintf(stderr, "fm1: out of memory\n");
        exit(EXIT_FAILURE);
    }
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusConnection *fm1_init(int *fd) {
    DBusError error;
    dbus_error_init(&error);

    DBusConnection *conn = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
    if (!conn) {
        fprintf(stderr, "fm1: connection error (%s)\n",
                dbus_error_is_set(&error) ? error.message : "unknown");
        dbus_error_free(&error);
        return NULL;
    }
    dbus_connection_set_exit_on_disconnect(conn, FALSE);

    int ret = dbus_bus_request_name(conn, FM1_NAME, DBUS_NAME_FLAG_REPLACE_EXISTING, &error);
    if (dbus_error_is_set(&error)) {
        fprintf(stderr, "fm1: name error (%s)\n", error.message);
        dbus_error_free(&error);
        goto fail;
    }
    if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER &&
        ret != DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER) {
        fprintf(stderr, "fm1: could not become owner of %s\n", FM1_NAME);
        goto fail;
    }
    if (!dbus_connection_add_filter(conn, fm1_filter, NULL, NULL)) {
        fprintf(stderr, "fm1: out of memory\n");
        goto fail;
    }
    if (!dbus_connection_get_unix_fd(conn, fd)) {
        fprintf(stderr, "fm1: cannot get connection fd\n");
        goto fail;
    }
    return conn;

fail:
    dbus_connection_close(conn);
    dbus_connection_unref(conn);
    return NULL;
}

/* Process all pending traffic. Returns -1 if the bus went away. */
static int fm1_handle(DBusConnection *conn) {
    if (!dbus_connection_read_write(conn, 0)) {
        fprintf(stderr, "fm1: disconnected from the session bus\n");
        return -1;
    }
    while (dbus_connection_dispatch(conn) == DBUS_DISPATCH_DATA_REMAINS)
        ;
    dbus_connection_flush(conn);
    if (!dbus_connection_get_is_connected(conn)) {
        fprintf(stderr, "fm1: disconnected from the session bus\n");
        return -1;
    }
    return 0;
}

static void fm1_free(DBusConnection *conn) {
    if (!conn) return;
    dbus_connection_close(conn);
    dbus_connection_unref(conn);
}
#endif /* HAVE_DBUS */

/* ------------------------------------------------------------------------ */
/* Single instance lock                                                     */
/* ------------------------------------------------------------------------ */

/* Take an exclusive lock on $XDG_RUNTIME_DIR/aperi-session.lock (or
 * /tmp/aperi-session-<uid>.lock). Returns the lock fd, which must be kept
 * open for the whole life of the process, -2 if another instance holds the
 * lock, -1 on error. The lock is released by the kernel when the process
 * exits, however it terminates. */
static int single_instance_lock(void) {
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    char *path = NULL;
    int n = (runtime_dir && runtime_dir[0] == '/')
                ? asprintf(&path, "%s/aperi-session.lock", runtime_dir)
                : asprintf(&path, "/tmp/aperi-session-%ju.lock", (uintmax_t)getuid());
    if (n < 0) {
        perror("asprintf");
        return -1;
    }

    int fd = open(path, O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) {
        fprintf(stderr, "Cannot open lock file %s: %s\n", path, strerror(errno));
        free(path);
        return -1;
    }
    /* In a shared directory like /tmp make sure the file is really ours */
    struct stat st;
    if (fstat(fd, &st) || st.st_uid != getuid() || !S_ISREG(st.st_mode)) {
        fprintf(stderr, "Lock file %s is not a regular file owned by us\n", path);
        close(fd);
        free(path);
        return -1;
    }
    free(path);

    if (flock(fd, LOCK_EX | LOCK_NB)) {
        int e = errno;
        close(fd);
        if (e == EWOULDBLOCK) return -2;
        fprintf(stderr, "flock: %s\n", strerror(e));
        return -1;
    }
    return fd;
}

/* ------------------------------------------------------------------------ */
/* Wayland session watcher                                                  */
/* ------------------------------------------------------------------------ */

/* Open a bare connection to the Wayland compositor socket. Nothing is ever
 * sent on it: it is only polled to detect when the compositor closes it,
 * i.e. when the compositor exits. Returns the socket fd or -1. */
static int wayland_watch_init(void) {
    const char *display = getenv("WAYLAND_DISPLAY");
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    int n;

    if (!display || !display[0]) {
        fprintf(stderr, "--exit-with-session: WAYLAND_DISPLAY is not set\n");
        return -1;
    }
    if (display[0] == '/') {
        n = snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", display);
    } else if (runtime_dir && runtime_dir[0] == '/') {
        n = snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/%s", runtime_dir, display);
    } else {
        fprintf(stderr, "--exit-with-session: XDG_RUNTIME_DIR is not set\n");
        return -1;
    }
    if (n < 0 || (size_t)n >= sizeof(addr.sun_path)) {
        fprintf(stderr, "--exit-with-session: Wayland socket path too long\n");
        return -1;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr))) {
        fprintf(stderr, "--exit-with-session: cannot connect to %s: %s\n", addr.sun_path,
                strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

/* Returns true if the compositor closed the connection */
static bool wayland_watch_closed(int fd, short revents) {
    if (revents & (POLLERR | POLLHUP | POLLNVAL)) return true;
    /* We never send requests, so nothing is expected: discard whatever
     * arrives and check for EOF */
    char buf[256];
    for (;;) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n > 0) continue;
        if (n == 0) return true;
        return !(errno == EAGAIN || errno == EINTR);
    }
}

/* ------------------------------------------------------------------------ */
/* main                                                                     */
/* ------------------------------------------------------------------------ */

static void usage(FILE *out, const char *prog) {
    fprintf(out,
            "Usage: %s [OPTION]...\n"
            "Session helper for aperi, to be run at the start of the graphical session\n"
            "by a systemd user unit or by the compositor (e.g. `exec` in sway config).\n"
            "\n"
            "  -F, --no-fm1       do not provide the org.freedesktop.FileManager1\n"
            "                     D-Bus service (ShowItems -> aperi)\n"
            "  -W, --no-wipewine  do not remove wine file association .desktop files\n"
            "  -s, --exit-with-session\n"
            "                     exit when the Wayland compositor ($WAYLAND_DISPLAY)\n"
            "                     terminates\n"
            "  -h, --help         show this help and exit\n"
            "  -V, --version      show version and exit\n"
#ifndef HAVE_DBUS
            "\nThis build has no D-Bus support: fm1 is always disabled.\n"
#endif
            ,
            prog);
}

static const char *const signal_reason[] = {
    [SIGHUP] = "SIGHUP", [SIGINT] = "SIGINT", [SIGTERM] = "SIGTERM",
};

enum { POLL_SIGNAL, POLL_WAYLAND, POLL_WIPEWINE, POLL_FM1, POLL_N };

int main(int argc, char *argv[]) {
    bool use_fm1 = true, use_wipewine = true, exit_with_session = false;
    int retcode = EXIT_FAILURE;

    static const struct option longopts[] = {
        {"no-fm1", no_argument, NULL, 'F'},
        {"no-wipewine", no_argument, NULL, 'W'},
        {"exit-with-session", no_argument, NULL, 's'},
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, 'V'},
        {NULL, 0, NULL, 0},
    };
    int c;
    while ((c = getopt_long(argc, argv, "FWshV", longopts, NULL)) != -1) {
        switch (c) {
        case 'F': use_fm1 = false; break;
        case 'W': use_wipewine = false; break;
        case 's': exit_with_session = true; break;
        case 'h': usage(stdout, argv[0]); return EXIT_SUCCESS;
        case 'V': printf("aperi-session %s\n", VERSION); return EXIT_SUCCESS;
        default: usage(stderr, argv[0]); return EXIT_FAILURE;
        }
    }
    if (optind < argc) {
        fprintf(stderr, "%s: unexpected argument '%s'\n", argv[0], argv[optind]);
        usage(stderr, argv[0]);
        return EXIT_FAILURE;
    }
#ifndef HAVE_DBUS
    if (use_fm1) {
        fprintf(stderr, "Built without D-Bus support: fm1 disabled\n");
        use_fm1 = false;
    }
#endif
    if (!use_fm1 && !use_wipewine) {
        fprintf(stderr, "%s: all features disabled, nothing to do\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* Only one instance per user: a second one exits successfully, so that
     * e.g. reloading the compositor config or a systemd unit racing with the
     * compositor startup commands is harmless */
    _cleanup_(closep) int lock_fd = single_instance_lock();
    if (lock_fd == -2) {
        fprintf(stderr, "aperi-session is already running, exiting\n");
        return EXIT_SUCCESS;
    }
    if (lock_fd < 0) return EXIT_FAILURE;

    /* Handle termination signals synchronously through a signalfd. SIGHUP is
     * included so that the end of a login session is a clean shutdown. */
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGHUP);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &mask, NULL)) {
        perror("sigprocmask");
        return EXIT_FAILURE;
    }
    _cleanup_(closep) int sig_fd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (sig_fd < 0) {
        perror("signalfd");
        return EXIT_FAILURE;
    }

    struct pollfd fds[POLL_N];
    for (int i = 0; i < POLL_N; i++) fds[i] = (struct pollfd){.fd = -1, .events = POLLIN};
    fds[POLL_SIGNAL].fd = sig_fd;

    _cleanup_(closep) int wayland_fd = -1;
    if (exit_with_session) {
        wayland_fd = wayland_watch_init();
        if (wayland_fd < 0) return EXIT_FAILURE;
        fds[POLL_WAYLAND].fd = wayland_fd;
    }

    struct wipewine ww = {.inotify_fd = -1, .dir_fd = -1};
#ifdef HAVE_DBUS
    DBusConnection *bus = NULL;
#endif
    if (use_wipewine) {
        if (wipewine_init(&ww)) goto exit;
        fds[POLL_WIPEWINE].fd = ww.inotify_fd;
    }

#ifdef HAVE_DBUS
    if (use_fm1) {
        bus = fm1_init(&fds[POLL_FM1].fd);
        if (!bus) goto exit;
        /* Messages may already be queued after the name request */
        if (fm1_handle(bus)) goto exit;
    }
#endif

    int r = notify("READY=1");
    if (r < 0)
        fprintf(stderr, "Failed to notify readiness to $NOTIFY_SOCKET: %s\n", strerror(-r));

    printf("aperi-session started (fm1: %s, wipewine: %s)\n", use_fm1 ? "on" : "off",
           use_wipewine ? "on" : "off");
    fflush(stdout);

    for (;;) {
        if (poll(fds, POLL_N, -1) < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            goto exit;
        }
        if (fds[POLL_SIGNAL].revents) {
            struct signalfd_siginfo si;
            const char *name = "signal";
            if (read(sig_fd, &si, sizeof(si)) == sizeof(si) &&
                si.ssi_signo < sizeof(signal_reason) / sizeof(signal_reason[0]) &&
                signal_reason[si.ssi_signo])
                name = signal_reason[si.ssi_signo];
            printf("Received %s, exiting\n", name);
            break;
        }
        if (fds[POLL_WAYLAND].revents && wayland_watch_closed(wayland_fd, fds[POLL_WAYLAND].revents)) {
            printf("Wayland compositor terminated, exiting\n");
            break;
        }

        if (fds[POLL_WIPEWINE].revents) {
            if (fds[POLL_WIPEWINE].revents & (POLLERR | POLLHUP | POLLNVAL)) goto exit;
            if (wipewine_handle(&ww)) goto exit;
        }
#ifdef HAVE_DBUS
        if (fds[POLL_FM1].revents) {
            if (fds[POLL_FM1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                fprintf(stderr, "fm1: session bus connection lost\n");
                goto exit;
            }
            if (fm1_handle(bus)) goto exit;
        }
#endif
    }

    fflush(stdout);
    notify("STOPPING=1");
    retcode = EXIT_SUCCESS;

exit:
#ifdef HAVE_DBUS
    fm1_free(bus);
#endif
    wipewine_free(&ww);
    return retcode;
}

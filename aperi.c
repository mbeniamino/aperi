#include <ctype.h>
#include <stdio.h>
#include <sys/types.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

int read_to(FILE* f, const char sep) {
    while(1) {
        int ch = getc(f);
        if (ch == EOF || ch == '\n' || ch == '\r') {
            return 0;
        }
        if (ch == sep) {
            return 1;
        }
    }
}

int match(FILE* f, const char* pattern) {
    int pattern_idx = 0;
    int match_count = 0;
    int pattern_ln = strlen(pattern);
    int star = 0;
    while(1) {
        int ch = getc(f);
        if (ch == ',') {
            pattern_idx = 0;
            match_count = 0;
            star = 0;
        } else if (ch == '=' && star) {
            return 1;
        } else if (ch == '=' || ch == '\n' || ch == '\r' || ch == EOF) {
            break;
        } else {
            star = pattern_idx == 0 && ch == '*';
            if (ch == pattern[pattern_idx++]) {
                ++match_count;
                if (pattern[pattern_idx] == 0 && match_count == pattern_ln) {
                    read_to(f, '=');
                    return 1;
                }
            }
        }
    }
    return 0;
}

int next_line(FILE* f) {
    int c;
    while(1) {
        c = getc(f);
        if (c == '\n' || c == '\r' || c == EOF) break;
    }
    ungetc(c, f);
    while(1) {
        c = getc(f);
        if (c != '\n' && c != '\r') break;
    }
    ungetc(c, f);
    return 0;
}

int rule_id(const char* path, char** ext) {
    int exists = 0;
    // Check if file exists
    struct stat statbuf;
    if (stat(path, &statbuf) == 0) {
        exists = 1;
        if ((statbuf.st_mode & S_IFMT) == S_IFDIR) {
            *ext = malloc(2);
            (*ext)[0] = '/';
            (*ext)[1] = 0;
            return 0;
        }
    }

    int idx = -1;
    int last_dot = -1;
    char ch;
    const char* schema_id = "://";
    int schema_idx = 0;
    while((ch = path[++idx])) {
        if(ch == '.') last_dot = idx;
        else if (ch == '/') last_dot = -1;
        if (ch == schema_id[schema_idx]) {
            ++schema_idx;
            if (schema_id[schema_idx] == 0) {
                *ext = malloc(idx+2);
                strncpy(*ext, path, idx+1);
                (*ext)[idx+1] = 0;
                return 0;
            }
        } else {
            schema_idx = 0;
        }
    }
    if (last_dot == -1) last_dot = idx - 1;
    size_t path_ln = strlen(path);
    int target_ln = path_ln - last_dot;
    *ext = malloc(target_ln);
    strncpy(*ext, path+last_dot+1, target_ln);
    idx = 0;
    for(idx = 0; (*ext)[idx]; ++idx) (*ext)[idx] = tolower((*ext)[idx]);
    return !exists;
}

typedef struct Aperi {
    const char* file_path;
    char* rule_id;
    FILE* config_f;
} Aperi;

void init(Aperi* aperi, const char* file_path) {
    if (strncmp(file_path, "file://", 7) == 0) {
        aperi->file_path = file_path + 7;
    } else {
        aperi->file_path = file_path;
    }
    aperi->rule_id = NULL;

    // Retrieve and set the file rule_id
    if (rule_id(aperi->file_path, &aperi->rule_id) != 0) {
        fprintf(stderr, "Couldn't stat %s. Exiting.", aperi->file_path);
        exit(2);
    }
}

void close_config_file(Aperi* aperi) {
    if(aperi->config_f) fclose(aperi->config_f);
    aperi->config_f = 0;
}

void deinit(Aperi* aperi) {
    free(aperi->rule_id);
    aperi->rule_id = NULL;
    close_config_file(aperi);
}

void open_config_file(Aperi* aperi) {
    // Open the configuration file
    const char* CONFIG_REL_PATH = "/.config/aperi/config";
    struct passwd *pw = getpwuid(getuid());
    const char *homedir = pw->pw_dir;
    size_t homedir_ln = strlen(homedir);
    char* cfgpath = malloc(homedir_ln+strlen(CONFIG_REL_PATH)+1);
    strcpy(cfgpath, homedir);
    strcpy(cfgpath+homedir_ln, CONFIG_REL_PATH);
    aperi->config_f = fopen(cfgpath, "rb");
    free(cfgpath);
}

void read_app_and_launch(Aperi* aperi) {
    int ch;

    int new_arg = 1;
    int curr_arg = -1;
    int allocated_args = 10;
    char **argv = (char**)malloc(allocated_args * sizeof(char*));
    int used_args = 2;

    int allocated_str = 0;
    int used_str = 0;
    int curr_str = -1;

    while(1) {
        ch = getc(aperi->config_f);
        if(ch == '\n' || ch == '\r' || ch == EOF) {
            break;
        } else if (ch == ' ')  {
            new_arg = 1;
        } else {
            if (new_arg) {
                while(used_args + 1 > allocated_args) {
                    allocated_args *= 2;
                    argv = (char**)realloc(argv, allocated_args * sizeof(char*));
                }
                allocated_str = 10;
                ++curr_arg;
                argv[curr_arg] = (char*)malloc(allocated_str);
                ++used_args;
                argv[curr_arg][0] = 0;
                curr_str = -1;
                used_str = 1;
                new_arg = 0;
            }
            while(used_str + 1 > allocated_str) {
                allocated_str *= 2;
                argv[curr_arg] = (char*)realloc(argv[curr_arg], allocated_str);
            }
            argv[curr_arg][++curr_str] = ch;
            argv[curr_arg][curr_str+1] = 0;
            ++used_str;
        }
    }
    int is_schema = strstr(aperi->file_path, "://") != NULL;
    if (is_schema) {
        int pathlen = strlen(aperi->file_path);
        argv[used_args-2] = malloc(pathlen+1);
        strncpy(argv[used_args-2], aperi->file_path, pathlen+1);
    } else {
        argv[used_args-2] = realpath(aperi->file_path, NULL);
    }
    argv[used_args-1] = NULL;
    execvp(argv[0], argv);
    fprintf(stderr, "Error executing %s: %s\n", argv[0], strerror(errno));
    for(int i = 0; i < used_args; ++i) free(argv[i]);
    free(argv);
}

void launch_associated_app(Aperi* aperi) {
    open_config_file(aperi);
    FILE* f = aperi->config_f;
    if (!f) return;
    int eof = 0;
    while(!eof) {
        int ch = getc(f);
        ungetc(ch, f);
        switch(ch) {
            case '#':
            case ';':
            case '\n':
            case '\r':
                next_line(f);
                break;
            case EOF:
                eof = 1;
                break;
            default:
                int got_match = match(f, aperi->rule_id);
                if (got_match) {
                    read_app_and_launch(aperi);
                    break;
                } else {
                    read_to(f, '\n');
                }
        }
    }
    close_config_file(aperi);
}

int main(int argc, char* argv[]) {
    // No args: print help
    if (argc < 2) {
        printf("Usage: %s <file>\n", argv[0]);
        exit(0);
    }

    Aperi aperi;
    init(&aperi, argv[1]);
    launch_associated_app(&aperi);

    deinit(&aperi);

    return 0;
}

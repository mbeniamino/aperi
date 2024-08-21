#include <ctype.h>
#include <stdio.h>
#include <sys/types.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

/* Read a line from file f up to the next `sep` character. Return 1 if the character
 * was found or 0 if it reached the end of the line or of the file */
int read_line_to(FILE* f, const char sep) {
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

/* check if the current config line matches for the pattern. Read up to '=' or 
 * end of line/file, whatever comes first */
int match(FILE* f, const char* pattern) {
    int pattern_idx = 0;
    int match_count = 0;
    int pattern_ln = strlen(pattern);
    // flag set when '*' is found at the beginning of a match
    int star = 0;
    while(1) {
        int ch = getc(f);
        if (ch == ',') {
            pattern_idx = 0;
            match_count = 0;
            star = 0;
        } else if (ch == '=' && star) {
            /* catchall rule */
            return 1;
        } else if (ch == '=' || ch == '\n' || ch == '\r' || ch == EOF) {
            /* end of line/file -> exit from loop */
            break;
        } else {
            star = pattern_idx == 0 && ch == '*';
            if (pattern_idx < pattern_ln && ch == pattern[pattern_idx++]) {
                ++match_count;
                if (pattern[pattern_idx] == 0 && match_count == pattern_ln) {
                    ch = getc(f);
                    if (ch == '=' || ch == ',') {
                        ungetc(ch, f);
                        // rule matches, move to '=' and return
                        read_line_to(f, '=');
                        return 1;
                    }
                }
            }
        }
    }
    // no match
    return 0;
}

/* skip to the next non empty line in file `f` */
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

/* Calc the rule id to match for the resource `path` (extension, schema, /, etc...)
   ext will be filled with the rule id to match in the config file
   return 1 if the file doesn't exists. urls always return 0. */
int rule_id(const char* path, char** ext) {
    int exists = 0;
    // Check if file exists. If it does and it's a directory return the rule id '/'
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

    // Search for :// or a dot
    int idx = -1;
    int last_dot = -1;
    char ch;
    const char* schema_id = "://";
    int schema_idx = 0;
    while((ch = path[++idx])) {
        if(ch == '.') last_dot = idx;
        else if (ch == '/') last_dot = -1;
        if (ch == schema_id[schema_idx]) {
            // found '://' -> return the string up to :// included
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

    // Not a folder, not an url: return the extension (the substring after the last '.')
    // in lowercase
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
    // File path/url to open
    const char* file_path;
    // Rule id to match for file_path
    char* rule_id;
    // Aperi config file
    FILE* config_f;
} Aperi;

// Init aperi struct members. `file_path` is the url/file to open.
void init(Aperi* aperi, const char* file_path) {
    // If file_path starts with file://, remove it
    if (strncmp(file_path, "file://", 7) == 0) {
        aperi->file_path = file_path + 7;
    } else {
        aperi->file_path = file_path;
    }
    aperi->rule_id = NULL;

    // Retrieve and set the file rule_id. If the file doesn't exist exit with an error
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
    // Open the configuration file from $HOME/.config/aperi/config
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

// read to the end of the line the app to launch and launch it
void read_app_and_launch(Aperi* aperi) {
    int ch;

    // next char starts a new argument
    int new_arg = 1;
    // index of the command argument being read from the config file
    int curr_arg = -1;
    // currently allocated space for arguments
    int allocated_args = 10;
    // alloc argv
    char **argv = (char**)malloc(allocated_args * sizeof(char*));
    // used args, at least the url to open and the args terminator ("")
    int used_args = 2;

    int allocated_str = 0;
    int used_str = 0;
    int curr_str = -1;

    // Read the config file one char at the time
    while(1) {
        ch = getc(aperi->config_f);
        if(ch == '\n' || ch == '\r' || ch == EOF) {
            break;
        } else if (ch == ' ')  {
            // separator -> set new arg flag
            new_arg = 1;
        } else {
            if (new_arg) {
                // allocate a new arg, reallocate argv if needed
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
            // append the character to the current arg
            while(used_str + 1 > allocated_str) {
                allocated_str *= 2;
                argv[curr_arg] = (char*)realloc(argv[curr_arg], allocated_str);
            }
            argv[curr_arg][++curr_str] = ch;
            argv[curr_arg][curr_str+1] = 0;
            ++used_str;
        }
    }
    // expand real path or use arg as is if it's a url
    int is_schema = strstr(aperi->file_path, "://") != NULL;
    if (is_schema) {
        int pathlen = strlen(aperi->file_path);
        argv[used_args-2] = malloc(pathlen+1);
        strncpy(argv[used_args-2], aperi->file_path, pathlen+1);
    } else {
        argv[used_args-2] = realpath(aperi->file_path, NULL);
    }
    // args terminator
    argv[used_args-1] = NULL;
    // exec the program
    execvp(argv[0], argv);
    fprintf(stderr, "Error executing %s: %s\n", argv[0], strerror(errno));
    for(int i = 0; i < used_args; ++i) free(argv[i]);
    free(argv);
}

// Search for a matching app and, if found, launch it
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
                // comment/empty line: skip to next valid line
                next_line(f);
                break;
            case EOF:
                eof = 1;
                break;
            default:
            {
                // check if the current line matches the rule
                int got_match = match(f, aperi->rule_id);
                if (got_match) {
                    // match: launch the associated program
                    read_app_and_launch(aperi);
                    break;
                } else {
                    // no match: skip to next line
                    read_line_to(f, '\n');
                }
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

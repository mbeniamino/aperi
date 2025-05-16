#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <sys/types.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#ifdef USE_GIT_VERSION
#include "git_version.h"
#endif

const char* CONFIG_DIR_PATH = "/.config/aperi/";

typedef enum { MTExact, MTEnd } MatchType;

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

typedef struct Aperi {
    // File path/url to open
    char* file_path;
    // String to match
    char* rule_id;
    // Match type
    MatchType match_type;
    // Aperi config file
    FILE* config_f;
} Aperi;

/* Calc `rule_id` and `match_type` to match for the resource `file_path`
 * (extension, schema, /, etc...)
 return 1 if the file doesn't exists. urls always return 0. */
int aperi_calc_rule_id(Aperi* aperi) {
    int exists = 0;
    // Check if file exists. If it does and it's a directory return the rule id '/'
    struct stat statbuf;
    if (stat(aperi->file_path, &statbuf) == 0) {
        exists = 1;
        if ((statbuf.st_mode & S_IFMT) == S_IFDIR) {
            aperi->rule_id = malloc(2);
            aperi->rule_id[0] = '/';
            aperi->rule_id[1] = 0;
            aperi->match_type = MTExact;
            return 0;
        }
    }

    // Search for ://
    int idx = -1;
    int last_slash = -1;
    char ch;
    const char* schema_id = "://";
    int schema_idx = 0;
    while((ch = aperi->file_path[++idx])) {
        if (ch == '/') last_slash = idx;
        if (ch == schema_id[schema_idx]) {
            // found '://' -> return the string up to :// included
            ++schema_idx;
            if (schema_id[schema_idx] == 0) {
                aperi->rule_id = malloc(idx+2);
                strncpy(aperi->rule_id, aperi->file_path, idx+1);
                aperi->rule_id[idx+1] = 0;
                aperi->match_type = MTExact;
                return 0;
            }
        } else {
            schema_idx = 0;
        }
    }

    // Not a folder, not an url: return the basename in lowercase
    int target_ln = idx - last_slash + 1;
    aperi->rule_id = malloc(target_ln);
    strncpy(aperi->rule_id, &aperi->file_path[last_slash+1], target_ln);
    aperi->match_type = MTEnd;
    for(idx = 0; aperi->rule_id[idx]; ++idx) {
        aperi->rule_id[idx] = tolower(aperi->rule_id[idx]);
    }
    return !exists;
}

/* check if the current config line matches for the pattern. Read up to '=' or
 * end of line/file, whatever comes first */
int aperi_match(Aperi* aperi) {
    int pattern_idx = 0;
    int match_count = 0;
    char* rule_id = aperi->rule_id;
    int rule_id_ln = strlen(aperi->rule_id);
    // flag set when '*' is found at the beginning of a match
    int star = 0;
    ssize_t pattern_allocation = 64;
    char *current_pattern = (char*)malloc(pattern_allocation);
    while(1) {
        int ch = getc(aperi->config_f);
        while (pattern_idx + 2 > pattern_allocation) {
            pattern_allocation *= 2;
            current_pattern = realloc(current_pattern, pattern_allocation);
        }
        if (ch == ',' || ch == '=') {
            if (star) return 1;
            current_pattern[pattern_idx] = 0;
            if (aperi->match_type == MTEnd) {
                // rule_id endswith
                current_pattern[pattern_idx+1] = 0;
                if(rule_id_ln - pattern_idx - 1 >= 0 &&
                   aperi->rule_id[rule_id_ln - pattern_idx - 1] == '.' &&
                   strncmp(current_pattern,
                           aperi->rule_id + rule_id_ln - pattern_idx,
                           pattern_idx + 1) == 0) {
                    // rule matches, move to '=' and return
                    if (ch == ',') read_line_to(aperi->config_f, '=');
                    return 1;
                }
            } else if (aperi->match_type == MTExact) {
                if (rule_id[pattern_idx] == 0 && match_count == rule_id_ln) {
                    // rule matches, move to '=' and return
                    if (ch == ',') read_line_to(aperi->config_f, '=');
                    return 1;
                }
            } else {
                fprintf(stderr, "Unexpected value for match_type. "
                                "This shouldn't happen! Contact the author.\n");
                exit(2);
            }
            // no more rules on this line -> no match
            if (ch == '=') return 0;
            pattern_idx = 0;
            match_count = 0;
            star = 0;
            current_pattern[0] = 0;
        } else if (ch == '\n' || ch == '\r' || ch == EOF) {
            /* end of line/file -> exit from loop */
            break;
        } else {
            star = pattern_idx == 0 && ch == '*';
            current_pattern[pattern_idx] = ch;
            if (pattern_idx < rule_id_ln && ch == rule_id[pattern_idx]) {
                ++match_count;
            }
            ++pattern_idx;
        }
    }
    free(current_pattern);
    // no match
    return 0;
}

/* Percent decode `s` (see https://en.wikipedia.org/wiki/Percent-encoding) */
void percent_decode(char* s) {
    char* src = s;
    char* dest = s;
    // counter of digits to decode
    int decode = 0;
    // decoded char
    char c;
    while(*src) {
        if (decode > 0) {
            c = c << 4;
            if ('0' <= *src && *src <= '9') c += *src - '0';
            if ('A' <= *src && *src <= 'F') c += *src - 'A' + 10;
            if ('a' <= *src && *src <= 'f') c += *src - 'a' + 10;
            --decode;
            if (decode == 0) {
                *dest = c;
                ++dest;
            }
        } else if(*src == '%') {
            decode = 2;
            c = 0;
        } else {
            *dest = *src;
            ++dest;
        }
        ++src;
    }
    *dest = 0;
}

// Init aperi struct members. `file_path` is the url/file to open.
void init(Aperi* aperi, char* file_path) {
    // If file_path starts with file://, remove it
    if (strncmp(file_path, "file://", 7) == 0) {
        aperi->file_path = file_path + 7;
        percent_decode(aperi->file_path);
    } else {
        aperi->file_path = file_path;
    }
    aperi->rule_id = NULL;

    // Retrieve and set the file rule_id. If the file doesn't exist exit with an error
    if (aperi_calc_rule_id(aperi) != 0) {
        fprintf(stderr, "Couldn't stat %s. Exiting.\n", aperi->file_path);
        exit(1);
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

const char* get_homedir() {
    struct passwd *pw = getpwuid(getuid());
    if (!pw) return "/";
    return pw->pw_dir;
}

void open_config_file(Aperi* aperi) {
    // Open the configuration file from $HOME/.config/aperi/config
    const char* CONFIG_BASENAME = "config";
    const char *homedir = get_homedir();
    size_t homedir_ln = strlen(homedir);
    size_t config_dir_path_ln = strlen(CONFIG_DIR_PATH);
    size_t config_basename_ln = strlen(CONFIG_BASENAME);
    char* cfgpath = malloc(homedir_ln+config_dir_path_ln+config_basename_ln+1);
    char* ptr = cfgpath;
    strcpy(ptr, homedir);
    ptr += homedir_ln;
    strcpy(ptr, CONFIG_DIR_PATH);
    ptr += config_dir_path_ln;
    strcat(cfgpath, CONFIG_BASENAME);
    ptr += config_basename_ln;
    aperi->config_f = fopen(cfgpath, "rb");
    free(cfgpath);
}

void check_for_wrapper_and_exec(Aperi *aperi) {
    if (aperi->match_type != MTEnd) return;
    const char *homedir = get_homedir();
    const char* WRAPPERS_DIR = "wrappers/";
    size_t homedir_ln = strlen(homedir);
    size_t config_dir_path_ln = strlen(CONFIG_DIR_PATH);
    size_t wrappers_dir_ln = strlen(WRAPPERS_DIR);
    char* wrapper_path = malloc(homedir_ln+config_dir_path_ln+wrappers_dir_ln+
                                strlen(aperi->rule_id)+1);
    char* ptr = wrapper_path;
    strcpy(ptr, homedir);
    ptr += homedir_ln;
    strcpy(ptr, CONFIG_DIR_PATH);
    ptr += config_dir_path_ln;
    strcpy(ptr, WRAPPERS_DIR);
    ptr += wrappers_dir_ln;
    DIR* dir = opendir(wrapper_path);
    // if wrappers dir doesn't exists... early exit
    if(!dir) {
        free(wrapper_path);
        return;
    }
    closedir(dir);
    for(char* c = aperi->rule_id; *c; ++c) {
        if (*c == '.') {
            char* argv[3];
            strcpy(ptr, c+1);
            argv[0] = wrapper_path;
            argv[1] = realpath(aperi->file_path, NULL);
            argv[2] = NULL;
            execvp(argv[0], argv);
            if (errno != ENOENT) {
                fprintf(stderr, "Couldn't launch wrapper %s: ", argv[0]);
                perror(NULL);
            }
            free(argv[1]);
        }
    }
    free(wrapper_path);
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
    int quoting = 0;
    int last_ch_quotes = 0;
    while(1) {
        ch = getc(aperi->config_f);
        if(ch == '\n' || ch == '\r' || ch == EOF) {
            break;
        } else if (ch == ' ' && !quoting)  {
            // separator -> set new arg flag
            new_arg = 1;
        } else if (ch == '"' && !last_ch_quotes && quoting == 0)  {
            quoting = 1;
        } else if (ch == '"' && !last_ch_quotes)  {
            quoting = 0;
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
        last_ch_quotes = ch == '"';
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
    // first: search for a wrapper in the wrappers directory...
    check_for_wrapper_and_exec(aperi);
    // if we are here no wrapper was found/worked. Continue with config file...
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
                int got_match = aperi_match(aperi);
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
#ifdef USE_GIT_VERSION
        printf("aperi version %s\n", GIT_VERSION);
#endif
        printf("Usage: %s <file>\n", argv[0]);
        exit(0);
    }

    Aperi aperi;
    init(&aperi, argv[1]);
    launch_associated_app(&aperi);

    deinit(&aperi);

    return 0;
}

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

/* Rule match type:
 * MTExact - the rule must match exactly the calculated pattern
 * MTEnd - the calculated pattern must end with "."+<rule>
 */
typedef enum { MTExact, MTEnd } MatchType;

// Main aperi struct and related functions

typedef struct Aperi {
    // File path/url to open
    char* file_path;
    // String to match
    char* rule_id;
    // Match type
    MatchType match_type;
    // Path to the config directory
    char* config_dir_path;
    // Aperi config file
    FILE* config_f;
    // the last parsed char from the config file is inside double quotes
    int quoting;
} Aperi;

// Init aperi struct members. `file_path` is the url/file to open.
void aperi_init(Aperi* aperi, char* file_path);

// Deallocate all resources allocated for the aperi struct
void aperi_deinit(Aperi* aperi);

// allocate and initialize aperi->config_dir_path
void aperi_init_config_dir_path(Aperi* aperi);

/* Get the next valid character from the configuration file. Handles doublequotes and
 * set the aperi->quoting flag accordingly */
int aperi_getc(Aperi* aperi);

/* Read a line from file f up to the next `sep` character. */
void aperi_read_line_to(Aperi* aperi, const char sep);

/* Calc `rule_id` and `match_type` to match for the resource `file_path`
 * (extension, schema, /, etc...)
 return 1 if the file doesn't exists. urls always return 0. */
int aperi_calc_rule_id(Aperi* aperi);

/* check if the current config line matches for the pattern. Read up to '=' or
 * end of line/file, whatever comes first */
int aperi_match(Aperi* aperi);

/* open the configuration file and set aperi->config_f */
void aperi_open_config_file(Aperi* aperi);

/* close the configuration file and reset aperi->config_f */
void aperi_close_config_file(Aperi* aperi);

/* check if there's a wrapper script able to handle the current resource. If so, exec the
 * script passing the aperi argument */
void aperi_check_for_wrapper_and_exec(Aperi *aperi);

/* check if there's a configuration rule able to handle the current resource. If so, exec the
 * associated commend appending the aperi argument to the list of arguments*/
void aperi_launch_associated_app(Aperi* aperi);

/* this function is called when a matching rule was found while parsing the config file.
 * Read the rest of the line and exec the associated commend appending the aperi argument 
 * to the list of arguments */
void aperi_read_app_and_launch(Aperi *aperi);


// Utility functions

/* Percent decode `s` (see https://en.wikipedia.org/wiki/Percent-encoding) */
void percent_decode(char* s);

/* skip to the next non empty line in file `f` */
int next_line(FILE* f);

/* return a pointer to a string containing the current user home directory.
 * The string must not be modified or freed */
const char* get_homedir();

// Implementation

void aperi_init(Aperi* aperi, char* file_path) {
    aperi_init_config_dir_path(aperi);
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

void aperi_deinit(Aperi* aperi) {
    free(aperi->rule_id);
    aperi->rule_id = NULL;
    aperi_close_config_file(aperi);
    free(aperi->config_dir_path);
}

void aperi_init_config_dir_path(Aperi* aperi) {
    char *xdg_config_home = getenv("XDG_CONFIG_HOME");
    const char* aperi_path = "/aperi/";
    size_t aperi_ln = strlen(aperi_path);
    char* ptr;
    if (xdg_config_home) {
        size_t xdg_config_home_ln = strlen(xdg_config_home);
        aperi->config_dir_path = malloc(xdg_config_home_ln+aperi_ln+1);
        ptr = aperi->config_dir_path;
        strcpy(ptr, xdg_config_home);
        ptr += xdg_config_home_ln;
    } else {
        const char *homedir = get_homedir();
        size_t homedir_ln = strlen(homedir);
        const char* config = "/.config";
        size_t config_ln = strlen("/.config");
        aperi->config_dir_path = malloc(homedir_ln+config_ln+aperi_ln+1);
        ptr = aperi->config_dir_path;
        strcpy(ptr, homedir);
        ptr += homedir_ln;
        strcpy(ptr, config);
        ptr += config_ln;
    }
    strcpy(ptr, aperi_path);
}

int aperi_getc(Aperi* aperi) {
    int ch = getc(aperi->config_f);
    if (ch == '"') {
        // we're either opening or closing a quoted sequence. Set the quoting
        // flag and read another character
        aperi->quoting = !aperi->quoting;
        ch = getc(aperi->config_f);
    }
    if (ch == '"') {
        // if we are here this is the second double quote in a row. Reset the quoting flag
        // and leave the character to return as " .
        aperi->quoting = !aperi->quoting;
    }
    return ch;
}

void aperi_read_line_to(Aperi* aperi, const char sep) {
    while(1) {
        int ch = aperi_getc(aperi);
        if (ch == EOF || ch == '\n' || ch == '\r' || (!aperi->quoting && ch == sep)) {
            return;
        }
    }
}

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
        int ch = aperi_getc(aperi);
        while (pattern_idx + 2 > pattern_allocation) {
            pattern_allocation *= 2;
            current_pattern = realloc(current_pattern, pattern_allocation);
        }
        if (!aperi->quoting && (ch == ',' || ch == '=')) {
            current_pattern[pattern_idx] = 0;
            star = strcmp(current_pattern, "*") == 0;
            if (star) {
                fprintf(stderr, "aperi: '*' rule is deprecated and will be removed "
                                "in a future version. Use '/*' instead.\n");
            }
            star |= strcmp(current_pattern, "/*") == 0;
            if (star) return 1;
            if (aperi->match_type == MTEnd) {
                // rule_id endswith
                current_pattern[pattern_idx+1] = 0;
                if(rule_id_ln - pattern_idx - 1 >= 0 &&
                   aperi->rule_id[rule_id_ln - pattern_idx - 1] == '.' &&
                   strncmp(current_pattern,
                           aperi->rule_id + rule_id_ln - pattern_idx,
                           pattern_idx + 1) == 0) {
                    // rule matches, move to '=' and return
                    if (ch == ',') aperi_read_line_to(aperi, '=');
                    return 1;
                }
            } else if (aperi->match_type == MTExact) {
                if (rule_id[pattern_idx] == 0 && match_count == rule_id_ln) {
                    // rule matches, move to '=' and return
                    if (ch == ',') aperi_read_line_to(aperi, '=');
                    return 1;
                }
            } else {
                fprintf(stderr, "Unexpected value for match_type. "
                                "This shouldn't happen! Contact the author.\n");
                exit(2);
            }
            // no more rules on this line -> no match
            if (!aperi->quoting && ch == '=') return 0;
            pattern_idx = 0;
            match_count = 0;
            current_pattern[0] = 0;
        } else if (ch == '\n' || ch == '\r' || ch == EOF) {
            /* end of line/file -> exit from loop */
            break;
        } else {
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

void aperi_open_config_file(Aperi* aperi) {
    // Open the configuration file from $XDG_CONFIG_HOME/aperi/config
    const char* CONFIG_BASENAME = "config";
    size_t config_dir_path_ln = strlen(aperi->config_dir_path);
    size_t config_basename_ln = strlen(CONFIG_BASENAME);
    char* cfgpath = malloc(config_dir_path_ln+config_basename_ln+1);
    char* ptr = cfgpath;
    strcpy(ptr, aperi->config_dir_path);
    ptr += config_dir_path_ln;
    strcat(cfgpath, CONFIG_BASENAME);
    ptr += config_basename_ln;
    aperi->config_f = fopen(cfgpath, "rb");
    aperi->quoting = 0;
    free(cfgpath);
}

void aperi_close_config_file(Aperi* aperi) {
    if(aperi->config_f) fclose(aperi->config_f);
    aperi->config_f = 0;
}

void aperi_check_for_wrapper_and_exec(Aperi *aperi) {
    if (aperi->match_type != MTEnd) return;
    const char* WRAPPERS_DIR = "wrappers/";
    size_t config_dir_path_ln = strlen(aperi->config_dir_path);
    size_t wrappers_dir_ln = strlen(WRAPPERS_DIR);
    char* wrapper_path = malloc(config_dir_path_ln+wrappers_dir_ln+
                                strlen(aperi->rule_id)+1);
    char* ptr = wrapper_path;
    strcpy(ptr, aperi->config_dir_path);
    ptr += config_dir_path_ln;
    strcpy(ptr, WRAPPERS_DIR);
    ptr += wrappers_dir_ln;
    DIR* dir = opendir(wrapper_path);
    // if wrappers dir doesn't exists... early exit
    printf("%s\n", wrapper_path);
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

void aperi_launch_associated_app(Aperi* aperi) {
    // first: search for a wrapper in the wrappers directory...
    aperi_check_for_wrapper_and_exec(aperi);
    // if we are here no wrapper was found/worked. Continue with config file...
    aperi_open_config_file(aperi);
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
                    aperi_read_app_and_launch(aperi);
                    break;
                } else {
                    // no match: skip to next line
                    aperi_read_line_to(aperi, '\n');
                }
            }
        }
    }
    aperi_close_config_file(aperi);
}

void aperi_read_app_and_launch(Aperi* aperi) {
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
        ch = aperi_getc(aperi);
        if(ch == '\n' || ch == '\r' || ch == EOF) {
            break;
        } else if (ch == ' ' && !aperi->quoting)  {
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

const char* get_homedir() {
    struct passwd *pw = getpwuid(getuid());
    if (!pw) return "/";
    return pw->pw_dir;
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
    aperi_init(&aperi, argv[1]);
    aperi_launch_associated_app(&aperi);
    aperi_deinit(&aperi);

    return 0;
}

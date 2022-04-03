#include <ctype.h>
#include <stdio.h>
#include <sys/types.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#ifdef HAVE_GTK4
#include <gtk/gtk.h>
#endif

int read_to(FILE* f, const char sep, char **result) {
    const int SSIZE = 40;
    int idx = 0;
    if (result) {
        *result = malloc(SSIZE);
    }
    int current_size = SSIZE;

    while(1) {
        int ch = getc(f);
        if (ch == EOF || ch == '\n' || ch == '\r') {
            if (result) (*result)[idx++] = 0;
            return 0;
        }
        if (ch == sep) {
            if (result) (*result)[idx++] = 0;
            return 1;
        }
        // We keep an extra char for the null terminator
        if (result && idx + 2 == current_size) {
            current_size <<= 1;
            char* new_result = malloc(current_size);
            strcpy(new_result, *result);
            free(*result);
            *result = new_result;
        }
        if (result) (*result)[idx++] = ch;
    }
}

int match(FILE* f, const char* pattern) {
    int pattern_idx = 0;
    int match_count = 0;
    int pattern_ln = strlen(pattern);
    while(1) {
        int ch = getc(f);
        if (ch == ',') {
            pattern_idx = 0;
            match_count = 0;
        } else if (ch == '=' || ch == '\n' || ch == '\r' || ch == EOF) {
            break;
        } else {
            if (ch == pattern[pattern_idx++]) {
                ++match_count;
                if (pattern[pattern_idx] == 0 && match_count == pattern_ln) {
                    read_to(f, '=', NULL);
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
    if (strncmp(path, "file://", 7) == 0) {
        path += 7;
    }
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

#ifdef HAVE_GTK4
// Code used for the Gtk dialog ===============================================
typedef struct Context {
    char* filepath;
    GFile* file;
    GtkWindow* main_window;
    GtkWidget *chooser;
} Context;

void app_chooser_response_cb(GtkDialog* self, gint response_id, gpointer user_data) {
    Context *context = (Context*)user_data;
    GError *error = NULL;
    if (response_id == GTK_RESPONSE_OK) {
        GAppInfo* app_info = gtk_app_chooser_get_app_info(
                GTK_APP_CHOOSER(gtk_app_chooser_dialog_get_widget(GTK_APP_CHOOSER_DIALOG(self))));
        GList *files = NULL;
        files = g_list_append(files, context->file);
        g_app_info_launch(app_info, files, NULL, &error);
        if (error) g_error_free(error);
        g_list_free(files);
    }
    gtk_window_destroy(context->main_window);
}

static void
activate (GtkApplication *app,
          gpointer        user_data) {
          Context* context = (Context*)user_data;

          context->main_window = GTK_WINDOW(gtk_application_window_new(app));
          context->chooser = gtk_app_chooser_dialog_new(context->main_window, GTK_DIALOG_MODAL,
                                                        context->file);
          context->filepath = (char*)user_data;
          g_signal_connect(context->chooser,
                           "response",
                           G_CALLBACK(app_chooser_response_cb),
                           context);
          gtk_window_set_title(GTK_WINDOW(context->chooser), "Aperi");
          gtk_window_present(context->main_window);
          gtk_widget_hide(GTK_WIDGET(context->main_window));
          gtk_widget_show(context->chooser);
}
// ============================================================================
#endif

typedef struct Aperi {
    const char* file_path;
    char* rule_id;
    FILE* config_f;
} Aperi;

void init(Aperi* aperi, const char* file_path) {
    aperi->file_path = file_path;
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
    const char* CONFIG_REL_PATH = "/.config/aperi.cfg";
    struct passwd *pw = getpwuid(getuid());
    const char *homedir = pw->pw_dir;
    char* cfgpath = malloc(strlen(homedir)+strlen(CONFIG_REL_PATH)+1);
    strcpy(cfgpath, homedir);
    strcat(cfgpath, CONFIG_REL_PATH);
    aperi->config_f = fopen(cfgpath, "rb");
    free(cfgpath);
}

void launch_associated_app(Aperi* aperi) {
    open_config_file(aperi);
    FILE* f = aperi->config_f;
    if (!f) return;
    int eof = 0;
    while(!eof) {
        int ch = getc(f);
        ungetc(ch, f);
        char* executable;
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
                read_to(f, '\n', &executable);
                if (got_match) {
                    execl(executable, executable, aperi->file_path, (char*)NULL);
                    fprintf(stderr, "Error executing %s %s.\n", executable, aperi->file_path);
                    free(executable);
                    break;
                }
                free(executable);
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

    int status = 0;
#ifdef HAVE_GTK4
    // No association found... use gtk4 dialog to let the user choose a proper application
    GFile* gf = g_file_new_for_path(aperi.file_path);
    if(gf) {
        GtkApplication *app;
        Context context;
        context.file = gf;
        app = gtk_application_new ("org.tautologica.aperi", G_APPLICATION_FLAGS_NONE);
        g_signal_connect(app, "activate", G_CALLBACK (activate), &context);
        status = g_application_run(G_APPLICATION (app), 0, NULL);
        g_object_unref(gf);
        g_object_unref(app);
    }
#endif
    deinit(&aperi);

#ifndef HAVE_GTK4
    fprintf(stderr, "No applications configured to handle %s\n", argv[1]);
    status = 1;
#endif

    return status;
}

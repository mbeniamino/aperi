#include <stdio.h>
#include <sys/types.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <gtk/gtk.h>

int read_to(FILE* f, const char sep, char **result) {
    const int SSIZE = 40;
    int idx = 0;
    *result = malloc(SSIZE);
    int current_size = SSIZE;

    while(1) {
        int ch = getc(f);
        if (ch == EOF || ch == '\n' || ch == '\r') {
            (*result)[idx++] = 0;
            return 0;
        }
        if (ch == sep) {
            (*result)[idx++] = 0;
            return 1;
        }
        // We keep an extra char for the null terminator
        if (idx + 2 == current_size) {
            current_size <<= 1;
            char* new_result = malloc(current_size);
            strcpy(new_result, *result);
            free(*result);
            *result = new_result;
        }
        (*result)[idx++] = ch;
    }
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

char* extension(const char* path) {
    int idx = -1;
    int last_dot = -1;
    char ch;
    while(ch = path[++idx]) {
        if(ch == '.') last_dot = idx;
        else if (ch == '/') last_dot = -1;
    }
    if (last_dot == -1) last_dot = idx - 1;
    size_t path_ln = strlen(path);
    int target_ln = path_ln - last_dot;
    char* res = malloc(target_ln);
    strncpy(res, path+last_dot+1, target_ln);
    return res;
}

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
          gtk_window_set_title(GTK_WINDOW(context->chooser), "BOpen");
          gtk_window_present(context->main_window);
          gtk_widget_hide(GTK_WIDGET(context->main_window));
          gtk_widget_show(context->chooser);
}
// ============================================================================

typedef struct BOpen {
    const char* file_path;
    char* file_ext;
    FILE* config_f;
} BOpen;

void init(BOpen* bopen, const char* file_path) {
    bopen->file_path = file_path;
    bopen->file_ext = NULL;
    FILE* config_f = 0;

    // Check if file exists
    struct stat statbuf;
    if (stat(bopen->file_path, &statbuf) != 0) {
        fprintf(stderr, "Couldn't stat %s. Exiting.", bopen->file_path);
        exit(2);
    }
    // Retrieve and set the file extension
    bopen->file_ext = extension(bopen->file_path);
}

void close_config_file(BOpen* bopen) {
    if(bopen->config_f) fclose(bopen->config_f);
    bopen->config_f = 0;
}

void deinit(BOpen* bopen) {
    free(bopen->file_ext);
    bopen->file_ext = NULL;
    close_config_file(bopen);
}

void open_config_file(BOpen* bopen) {
    // Open the configuration file
    const char* CONFIG_REL_PATH = "/.config/bopen.cfg";
    struct passwd *pw = getpwuid(getuid());
    const char *homedir = pw->pw_dir;
    char* cfgpath = malloc(strlen(homedir)+strlen(CONFIG_REL_PATH)+1);
    strcpy(cfgpath, homedir);
    strcat(cfgpath, CONFIG_REL_PATH);
    bopen->config_f = fopen(cfgpath, "rb");
    free(cfgpath);
}

void launch_associated_app(BOpen* bopen) {
    open_config_file(bopen);
    FILE* f = bopen->config_f;
    if (!f) return;
    int eof = 0;
    while(!eof) {
        int ch = getc(f);
        ungetc(ch, f);
        char* extension;
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
                if(!read_to(f, '=', &extension)) break;
                if(!read_to(f, ';', &executable)) {
                    free(extension);
                    break;
                }
                if (strcasecmp(extension, bopen->file_ext) == 0) {
                    execl(executable, executable, bopen->file_path, (char*)NULL);
                    fprintf(stderr, "Error executing %s %s.\n", executable, bopen->file_path);
                    free(extension);
                    free(executable);
                    break;
                }
                free(extension);
                free(executable);
                next_line(f);
        }
    }
    close_config_file(bopen);
}

int main(int argc, char* argv[]) {
    // No args: print help
    if (argc < 2) {
        printf("Usage: %s <file>\n", argv[0]);
        exit(0);
    }

    BOpen bopen;
    init(&bopen, argv[1]);
    launch_associated_app(&bopen);

    // No association found... use gtk4 dialog to let the user choose a proper application
    GFile* gf = g_file_new_for_path(bopen.file_path);
    if(gf) {
        int status;
        GtkApplication *app;
        Context context;
        context.file = gf;
        app = gtk_application_new ("org.tautologica.bopen", G_APPLICATION_FLAGS_NONE);
        g_signal_connect(app, "activate", G_CALLBACK (activate), &context);
        status = g_application_run(G_APPLICATION (app), 0, NULL);
        g_object_unref(gf);
        g_object_unref(app);
    }

    deinit(&bopen);

    return 0;
}

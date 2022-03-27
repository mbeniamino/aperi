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
} Context;

void app_chooser_response_cb(GtkDialog* self, gint response_id, gpointer user_data) {
    Context *context = (Context*)user_data;
    GError *error = NULL;
    if (response_id == GTK_RESPONSE_OK) {
        GAppInfo* app_info = gtk_app_chooser_get_app_info(
                (GtkAppChooser*)gtk_app_chooser_dialog_get_widget((GtkAppChooserDialog*)self));
        GList *files = NULL;
        files = g_list_append(files, context->file);
        g_app_info_launch(app_info, files, NULL, &error);
    }
    gtk_window_destroy(context->main_window);
}

static void
activate (GtkApplication *app,
          gpointer        user_data) {
          Context* context = (Context*)user_data;

          GtkWindow *window;
          window = (GtkWindow*)gtk_application_window_new(app);
          GtkWidget* chooser = gtk_app_chooser_dialog_new(window, GTK_DIALOG_MODAL, context->file);
          context->main_window = window;
          context->filepath = (char*)user_data;
          g_signal_connect(chooser,
                           "response",
                           G_CALLBACK(app_chooser_response_cb),
                           context);
          // To make the chooser open as a floating window for some reason we have to
          // show the parent window. We can hide it just after.
          gtk_widget_show((GtkWidget*)window);
          gtk_widget_show(chooser);
          gtk_widget_hide((GtkWidget*)window);
}
// ============================================================================

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s <file>\n", argv[0]);
        exit(0);
    }
    const char* path = argv[1];
    struct stat statbuf;
    if (stat(path, &statbuf) != 0) {
        fprintf(stderr, "Couldn't stat %s. Exiting.", path);
        exit(2);
    }
    char* ext = extension(path);
    const char* CONFIG_REL_PATH = "/.config/bopen.cfg";
    struct passwd *pw = getpwuid(getuid());
    const char *homedir = pw->pw_dir;
    char* cfgpath = malloc(strlen(homedir)+strlen(CONFIG_REL_PATH)+1);
    strcpy(cfgpath, homedir);
    strcat(cfgpath, CONFIG_REL_PATH);
    FILE* f = fopen(cfgpath, "rb");
    if (!f) {
        fprintf(stderr, "Error: couldn't read configuration file %s\n", cfgpath);
        free(cfgpath);
        exit(2);
    }
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
                if(!read_to(f, ';', &executable)) break;
                printf("%s == %s?\n", extension, ext);
                if (strcmp(extension, ext) == 0) {
                    execl(executable, executable, path, (char*)NULL);
                    fprintf(stderr, "Error executing %s %s.\n", executable, path);
                    break;
                }
                free(extension);
                free(executable);
                next_line(f);
        }
    }
    fclose(f);
    free(cfgpath);

    // No association found... use gtk4 dialog to let the user choose a proper application
    GFile* gf = g_file_new_for_path(path);
    if(gf) {
        int status;
        GtkApplication *app;
        Context context;
        context.file = gf;
        app = gtk_application_new ("org.tautologica.bopen", G_APPLICATION_FLAGS_NONE);
        g_signal_connect(app, "activate", G_CALLBACK (activate), &context);
        status = g_application_run(G_APPLICATION (app), 0, NULL);
        g_object_unref(app);
    }
    return 0;
}

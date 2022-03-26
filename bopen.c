#include <stdio.h>
#include <sys/types.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <magic.h>
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
          gtk_widget_show((GtkWidget*)window);
          gtk_widget_show(chooser);
          gtk_widget_hide((GtkWidget*)window);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s <file>\n", argv[0]);
        exit(0);
    }
    magic_t magic_cookie = magic_open(MAGIC_MIME_TYPE);
    if (!magic_cookie) {
        fprintf(stderr, "Couldn't allocate magic cookie. Exiting...\n");
        exit(1);
    }
    if (magic_load(magic_cookie, 0) != 0) {
        fprintf(stderr, "Couldn't load magic db. Exiting...\n");
        exit(1);
    }
    const char* detected_mime;
    if ((detected_mime = magic_file(magic_cookie, argv[1])) == 0) {
        fprintf(stderr, "Couldn't detect mimetype for file %s. Exiting...\n", argv[1]);
        exit(1);
    }
    printf("Detected mime = %s\n", detected_mime);
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
        char* mimetype;
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
                if(!read_to(f, '=', &mimetype)) break;
                if(!read_to(f, ';', &executable)) break;
                if (strcmp(mimetype, detected_mime) == 0) {
                    execl(executable, executable, argv[1], (char*)NULL);
                    fprintf(stderr, "Error executing %s %s.\n", executable, argv[1]);
                    break;
                }
                free(mimetype);
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

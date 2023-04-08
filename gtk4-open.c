#include <gtk/gtk.h>

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
          gtk_widget_set_visible(GTK_WIDGET(context->main_window), FALSE);
          gtk_widget_set_visible(context->chooser, TRUE);
}

int main(int argc, char* argv[]) {
    // No args: print help
    if (argc < 2) {
        printf("Usage: %s <file>\n", argv[0]);
        exit(0);
    }

    int status = 0;
    // No association found... use gtk4 dialog to let the user choose a proper application
    GFile* gf = g_file_new_for_path(argv[1]);
    if(gf) {
        GtkApplication *app;
        Context context;
        context.file = gf;
        app = gtk_application_new ("org.tautologica.gtk4-open", G_APPLICATION_DEFAULT_FLAGS);
        g_signal_connect(app, "activate", G_CALLBACK (activate), &context);
        status = g_application_run(G_APPLICATION (app), 0, NULL);
        g_object_unref(gf);
        g_object_unref(app);
    }

    fprintf(stderr, "No applications configured to handle %s\n", argv[1]);
    status = 1;

    return status;
}

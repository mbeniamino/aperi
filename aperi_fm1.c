#include <dbus/dbus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define DBUS_INTERFACE "org.freedesktop.FileManager1"
#define DBUS_PATH "/org/freedesktop/FileManager1"
#define SCHEMA "aperi-show-items"

// Function to handle ShowItems call
DBusHandlerResult handle_method_call(DBusConnection* connection, DBusMessage* message,
                                     void* user_data) {
    if (dbus_message_is_method_call(message, DBUS_INTERFACE, "ShowItems")) {
        DBusMessageIter args;
        if (!dbus_message_iter_init(message, &args)) {
            fprintf(stderr, "Message has no arguments!\n");
        } else {
            if (DBUS_TYPE_ARRAY != dbus_message_iter_get_arg_type(&args)) {
                fprintf(stderr, "Argument is not array!\n");
            } else if (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_ARRAY) {
                DBusMessageIter sub_iter;
                dbus_message_iter_recurse(&args, &sub_iter);
                while (dbus_message_iter_get_arg_type(&sub_iter) == DBUS_TYPE_STRING) {
                    const char *str;
                    dbus_message_iter_get_basic(&sub_iter, &str);
                    if (strncmp(str, "file://", 7) == 0) {
                        char* arg = malloc(strlen(str) + strlen(SCHEMA) - 4);
                        char* cp = stpcpy(arg, SCHEMA);
                        cp = stpcpy(cp, &str[4]);
                        pid_t pid = fork();
                        if (pid == 0) {
                            if (fork() == 0) {
                                char* argv[3];
                                argv[0] = "aperi";
                                argv[1] = arg;
                                argv[2] = NULL;
                                execvp(argv[0], argv);
                            }
                            exit(0);
                        }
                        free(arg);
                        int res;
                        wait(&res);
                    }
                    dbus_message_iter_next(&sub_iter);
                }
            }
        }

        dbus_message_iter_next(&args);

        // Create an empty response
        DBusMessage* reply = dbus_message_new_method_return(message);
        if (reply == NULL) {
            fprintf(stderr, "No memory\n");
            exit(1);
        }

        // Send response
        if (!dbus_connection_send(connection, reply, NULL)) {
            fprintf(stderr, "No memory\n");
            exit(1);
        }

        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

int main(int argc, char** argv) {
    DBusConnection* connection;
    DBusError error;
    int ret;

    dbus_error_init(&error);

    // Connect to the seesion bus
    connection = dbus_bus_get(DBUS_BUS_SESSION, &error);
    if (dbus_error_is_set(&error)) {
        fprintf(stderr, "Connection Error (%s)\n", error.message);
        dbus_error_free(&error);
        return EXIT_FAILURE;
    }

    if (connection == NULL) {
        return EXIT_FAILURE;
    }

    // Request the service name
    ret = dbus_bus_request_name(connection, "org.freedesktop.FileManager1",
                                DBUS_NAME_FLAG_REPLACE_EXISTING, &error);
    if (dbus_error_is_set(&error)) {
        fprintf(stderr, "Name Error (%s)\n", error.message);
        dbus_error_free(&error);
        return EXIT_FAILURE;
    }

    if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        return EXIT_FAILURE;
    }

    // Add a filter for the method calls
    dbus_connection_add_filter(connection, (DBusHandleMessageFunction)handle_method_call,
                               NULL, NULL);
    while (1) {
        dbus_connection_read_write_dispatch(connection, 1000);
    }
    return EXIT_SUCCESS;
}


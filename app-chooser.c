#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <dbus/dbus.h>
#include "git_version.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stdout, "app-chooser version %s\n", GIT_VERSION);
        fprintf(stdout, "Usage: %s <path>\n", argv[0]);
        exit(0);
    }

    DBusError err;
    DBusConnection* conn;

    // initialise the errors
    dbus_error_init(&err);

    // connect to the bus
    conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err)) {
        fprintf(stderr, "Connection Error (%s)\n", err.message);
        dbus_error_free(&err);
    }
    if (!conn) {
        exit(1);
    }

    // open the passed file
    int fh;
    fh = open(argv[1], O_RDONLY);
    if (fh < 0) {
        fprintf(stderr, "Couldn't open file %s (%s)\n", argv[1], strerror(errno));
        exit(1);
    }

    // Create the message for the call
    DBusMessage* msg;
    DBusMessageIter args;
    DBusPendingCall* pending;
    msg = dbus_message_new_method_call("org.freedesktop.portal.Desktop",
                                       "/org/freedesktop/portal/desktop",
                                       "org.freedesktop.portal.OpenURI",
                                       "OpenFile");
    if (!msg) {
        fprintf(stderr, "Error creating D-Bus Message. This application requires the "
                        "org.freedesktop.portal.OpenURI interface.\n");
        exit(1) ;
    }

    // append arguments
    dbus_message_iter_init_append(msg, &args);
    // parent window
    const char* empty_string = "";
    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &empty_string)) {
        fprintf(stderr, "Out Of Memory!\n");
        exit(1);
    }
    // file handler
    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_UNIX_FD, &fh)) {
        fprintf(stderr, "Out Of Memory!\n");
        exit(1);
    }

    // Dictionary of options {string: variant}
    DBusMessageIter iterDict, iterEntry, iterValue;
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY,
                                     DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
                                     DBUS_TYPE_STRING_AS_STRING DBUS_TYPE_VARIANT_AS_STRING
                                     DBUS_DICT_ENTRY_END_CHAR_AS_STRING,
                                     &iterDict);
    // Add the option "ask=true"
    dbus_message_iter_open_container(&iterDict, DBUS_TYPE_DICT_ENTRY, NULL, &iterEntry);
    char *key = "ask";
    dbus_message_iter_append_basic(&iterEntry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&iterEntry, DBUS_TYPE_VARIANT,
                                     DBUS_TYPE_BOOLEAN_AS_STRING, &iterValue);
    int val = 1;
    dbus_message_iter_append_basic(&iterValue, DBUS_TYPE_BOOLEAN, &val);
    dbus_message_iter_close_container(&iterEntry, &iterValue);
    dbus_message_iter_close_container(&iterDict, &iterEntry);
    dbus_message_iter_close_container(&args, &iterDict);

    // Call the method
    if (!dbus_connection_send_with_reply (conn, msg, &pending, -1)) {
        fprintf(stderr, "Out Of Memory!\n");
        exit(1);
    }

    if (!pending) {
        fprintf(stderr, "Pending Call Null\n");
        exit(1);
    }

    // Block until the call is completed
    dbus_pending_call_block(pending);
    // get the reply message
    msg = dbus_pending_call_steal_reply(pending);
    if (NULL == msg) {
        fprintf(stderr, "Reply Null\n"); 
        exit(1); 
    }
    // free the pending message handle
    dbus_pending_call_unref(pending);
    dbus_connection_flush(conn);

    // free message
    dbus_message_unref(msg);
    close(fh);
    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <atspi/atspi.h>
#include <glib.h>

#define SOCKET_PATH "/run/momentum_mouse.sock"

static int sock_fd = -1;

#include <sys/stat.h>
static void log_seen_app(const char* app) {
    if (!app || strlen(app) == 0) return;
    FILE *f = fopen("/tmp/momentum_mouse_seen_apps.txt", "a+");
    if (!f) return;
    fseek(f, 0, SEEK_SET);
    char line[256];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (strcmp(line, app) == 0) {
            found = 1;
            break;
        }
    }
    if (!found) {
        fseek(f, 0, SEEK_END);
        fprintf(f, "%s\n", app);
    }
    fclose(f);
    chmod("/tmp/momentum_mouse_seen_apps.txt", 0666);
}


// Define AtspiEvent type properly handled by callback
static void on_focus_changed(AtspiEvent *event, void *user_data) {
    (void)user_data;
    if ((strcmp(event->type, "object:state-changed:focused") == 0 && event->detail1 == 1) ||
        (strcmp(event->type, "object:state-changed:active") == 0 && event->detail1 == 1) ||
        strcmp(event->type, "window:activate") == 0) {
        
        if (event->source) {
            GError *error = NULL;
            AtspiAccessible *app = atspi_accessible_get_application(event->source, &error);
            if (error) {
                g_error_free(error);
                error = NULL;
            }
            if (app) {
                char *app_name = atspi_accessible_get_name(app, &error);
                if (error) {
                    g_error_free(error);
                    error = NULL;
                }
                if (app_name) {
                    log_seen_app(app_name);
                    
                    if (sock_fd == -1) {
                        sock_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
                    }
                    if (sock_fd != -1) {
                        struct sockaddr_un addr;
                        memset(&addr, 0, sizeof(addr));
                        addr.sun_family = AF_UNIX;
                        strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
                        sendto(sock_fd, app_name, strlen(app_name), 0, (struct sockaddr*)&addr, sizeof(addr));
                    }
                    
                    g_free(app_name);
                }
                g_object_unref(app);
            }
        }
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    // Initialize ATSPI
    atspi_init();

    // Register our event listener
    GError *error = NULL;
    AtspiEventListener *listener = atspi_event_listener_new(on_focus_changed, NULL, NULL);
    if (!atspi_event_listener_register(listener, "object:state-changed:focused", &error)) {
        fprintf(stderr, "Failed to register listener: %s\n", error ? error->message : "unknown error");
        if (error) g_error_free(error);
        return 1;
    }

    printf("momentum_mouse_window_listener started. Listening for focus events...\n");

    // Run main loop
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    // Cleanup
    atspi_event_listener_deregister(listener, "object:state-changed:focused", NULL);
    g_object_unref(listener);
    atspi_exit();
    
    if (sock_fd != -1) close(sock_fd);
    
    return 0;
}

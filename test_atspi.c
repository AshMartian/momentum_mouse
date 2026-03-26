#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <atspi/atspi.h>

static void focus_changed_cb(AtspiEvent *event, void *user_data) {
    if (strcmp(event->type, "object:state-changed:focused") == 0 && event->detail1 == 1) {
        AtspiAccessible *obj = event->source;
        GError *error = NULL;
        AtspiAccessible *app = atspi_accessible_get_application(obj, &error);
        
        if (app && !error) {
            char *app_name = atspi_accessible_get_name(app, NULL);
            if (app_name) {
                printf("FOCUSED APP: %s\n", app_name);
                g_free(app_name);
            }
            g_object_unref(app);
        }
        if (error) {
            g_error_free(error);
        }
    }
}

int main(int argc, char **argv) {
    atspi_init();
    
    // Register listener for focus events
    GError *error = NULL;
    atspi_register_keystroke_listener(NULL, NULL, 0, 0, NULL); // dummy?
    // Use proper atspi register.
    atspi_register_listener(focus_changed_cb, "object:state-changed:focused", NULL);
    
    printf("Listening to window changes...\n");
    g_main_loop_run(g_main_loop_new(NULL, FALSE));
    
    atspi_exit();
    return 0;
}

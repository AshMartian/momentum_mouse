#include <gio/gio.h>
#include <stdio.h>

int main() {
    GList *apps = g_app_info_get_all();
    int count = 0;
    for (GList *l = apps; l != NULL && count < 5; l = l->next) {
        GAppInfo *app = G_APP_INFO(l->data);
        if (g_app_info_should_show(app)) {
            printf("App: %s (Exec: %s)\n", g_app_info_get_name(app), g_app_info_get_commandline(app));
            count++;
        }
    }
    g_list_free_full(apps, g_object_unref);
    return 0;
}

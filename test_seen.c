#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void log_seen_app(const char* app) {
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
}
int main() {
    log_seen_app("gnome-shell");
    log_seen_app("momentum_mouse_gui");
    log_seen_app("gnome-shell");
    return 0;
}

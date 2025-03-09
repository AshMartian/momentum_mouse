#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <unistd.h>
#include "momentum_mouse.h"

// Detect the system's scroll direction setting
// Returns 1 if successful, 0 if detection failed
int detect_scroll_direction(void) {
    // Get the current user's username
    uid_t uid = getuid();
    struct passwd *pw = getpwuid(uid);
    if (!pw) {
        fprintf(stderr, "Warning: Could not determine current user\n");
        return 0;
    }
    
    // If running as root, try to get the real user from environment
    char username[256] = {0};
    char display[64] = {0};
    char *sudo_user = getenv("SUDO_USER");
    // We don't need XDG_RUNTIME_DIR for now
    
    if (uid == 0 && sudo_user != NULL) {
        // Running as root with sudo, use SUDO_USER
        strncpy(username, sudo_user, sizeof(username) - 1);
        if (debug_mode) {
            printf("Running as root, using SUDO_USER: %s\n", username);
        }
    } else {
        // Normal user
        strncpy(username, pw->pw_name, sizeof(username) - 1);
    }
    
    // Get DISPLAY environment variable
    char *display_env = getenv("DISPLAY");
    if (display_env) {
        strncpy(display, display_env, sizeof(display) - 1);
    } else {
        strcpy(display, ":0");  // Default to :0 if not set
    }
    
    if (debug_mode) {
        printf("Detecting scroll direction for user %s on display %s\n", username, display);
    }
    
    // Construct command with proper user context
    char cmd[512];
    if (uid == 0) {
        // Running as root, use su to run as the real user
        snprintf(cmd, sizeof(cmd), 
                "su %s -c 'DISPLAY=%s gsettings get org.gnome.desktop.peripherals.mouse natural-scroll' 2>/dev/null", 
                username, display);
    } else {
        // Normal user
        snprintf(cmd, sizeof(cmd), 
                "DISPLAY=%s gsettings get org.gnome.desktop.peripherals.mouse natural-scroll 2>/dev/null", 
                display);
    }
    
    // For GNOME/Ubuntu, check mouse setting first
    if (debug_mode) {
        printf("Trying GNOME mouse settings...\n");
    }
    FILE *fp = popen(cmd, "r");
    if (fp != NULL) {
        char buffer[128];
        if (fgets(buffer, sizeof(buffer), fp) != NULL) {
            pclose(fp);
            
            // Trim whitespace
            char *trimmed = buffer;
            while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
            
            // Check if natural scrolling is enabled
            if (strncmp(trimmed, "true", 4) == 0) {
                scroll_direction = SCROLL_DIRECTION_NATURAL;
                printf("Detected system setting (mouse): Natural scrolling\n");
                return 1;
            } else if (strncmp(trimmed, "false", 5) == 0) {
                scroll_direction = SCROLL_DIRECTION_TRADITIONAL;
                printf("Detected system setting (mouse): Traditional scrolling\n");
                return 1;
            }
        } else {
            pclose(fp);
        }
    }
    
    // If mouse setting didn't work, try touchpad setting
    if (debug_mode) {
        printf("Trying GNOME touchpad settings...\n");
    }
    if (uid == 0) {
        snprintf(cmd, sizeof(cmd), 
                "su %s -c 'DISPLAY=%s gsettings get org.gnome.desktop.peripherals.touchpad natural-scroll' 2>/dev/null", 
                username, display);
    } else {
        snprintf(cmd, sizeof(cmd), 
                "DISPLAY=%s gsettings get org.gnome.desktop.peripherals.touchpad natural-scroll 2>/dev/null", 
                display);
    }
    
    fp = popen(cmd, "r");
    if (fp != NULL) {
        char buffer[128];
        if (fgets(buffer, sizeof(buffer), fp) != NULL) {
            pclose(fp);
            
            // Trim whitespace
            char *trimmed = buffer;
            while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
            
            // Check if natural scrolling is enabled
            if (strncmp(trimmed, "true", 4) == 0) {
                scroll_direction = SCROLL_DIRECTION_NATURAL;
                printf("Detected system setting (touchpad): Natural scrolling\n");
                return 1;
            } else if (strncmp(trimmed, "false", 5) == 0) {
                scroll_direction = SCROLL_DIRECTION_TRADITIONAL;
                printf("Detected system setting (touchpad): Traditional scrolling\n");
                return 1;
            }
        } else {
            pclose(fp);
        }
    }
    
    // Try KDE Plasma setting
    if (debug_mode) {
        printf("Trying KDE settings...\n");
    }
    if (uid == 0) {
        snprintf(cmd, sizeof(cmd), 
                "su %s -c 'DISPLAY=%s kreadconfig5 --group \"Mouse\" --key \"NaturalScroll\"' 2>/dev/null", 
                username, display);
    } else {
        snprintf(cmd, sizeof(cmd), 
                "DISPLAY=%s kreadconfig5 --group 'Mouse' --key 'NaturalScroll' 2>/dev/null", 
                display);
    }
    
    fp = popen(cmd, "r");
    if (fp == NULL) {
        return 0;  // Command failed
    }
    
    char buffer[128];
    if (fgets(buffer, sizeof(buffer), fp) != NULL) {
        pclose(fp);
        
        // Trim whitespace
        char *trimmed = buffer;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
        
        // Check if natural scrolling is enabled
        if (strncmp(trimmed, "true", 4) == 0 || strcmp(trimmed, "1\n") == 0) {
            scroll_direction = SCROLL_DIRECTION_NATURAL;
            printf("Detected system setting (KDE): Natural scrolling\n");
            return 1;
        } else if (strncmp(trimmed, "false", 5) == 0 || strcmp(trimmed, "0\n") == 0) {
            scroll_direction = SCROLL_DIRECTION_TRADITIONAL;
            printf("Detected system setting (KDE): Traditional scrolling\n");
            return 1;
        }
    }
    pclose(fp);
    
    return 0;  // Detection failed
}

// gui/momentum_mouse_gui.c
// Requires GTK development libraries: apt-get install libgtk-3-dev
#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/input.h>
#include "../include/momentum_mouse.h"

// Stub implementation of debug_log for the GUI
void debug_log(const char *format, ...) {
    // In GUI mode, we don't need debug logs from the device scanner
    // This is just a stub to satisfy the linker
    (void)format; // Suppress unused parameter warning
}

static gboolean read_log_output(GIOChannel *source, GIOCondition condition, gpointer data) {
    GtkTextBuffer *buffer = GTK_TEXT_BUFFER(data);
    gchar *str = NULL;
    gsize length = 0;
    GError *error = NULL;

    if (condition & G_IO_IN) {
        GIOStatus status;
        do {
            status = g_io_channel_read_line(source, &str, &length, NULL, &error);
            if (status == G_IO_STATUS_NORMAL && str != NULL) {
                GtkTextIter iter;
                gtk_text_buffer_get_end_iter(buffer, &iter);
                gtk_text_buffer_insert(buffer, &iter, str, -1);
                
                GtkWidget *text_view = g_object_get_data(G_OBJECT(buffer), "text_view");
                if (text_view) {
                    gtk_text_buffer_get_end_iter(buffer, &iter);
                    GtkTextMark *mark = gtk_text_buffer_create_mark(buffer, NULL, &iter, FALSE);
                    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(text_view), mark);
                    gtk_text_buffer_delete_mark(buffer, mark);
                }
                g_free(str);
            }
        } while (status == G_IO_STATUS_NORMAL);
    }

    if (condition & (G_IO_ERR | G_IO_HUP)) {
        return FALSE; // Remove source
    }
    return TRUE; // Keep source
}

#define CONFIG_GROUP "smooth_scroll"

// Default values
#define DEFAULT_SENSITIVITY 1.0
#define DEFAULT_MULTIPLIER  1.0
#define DEFAULT_FRICTION    2.0
#define DEFAULT_MAX_VELOCITY 0.8
#define DEFAULT_SENSITIVITY_DIVISOR 1.0
#define DEFAULT_INERTIA_STOP_THRESHOLD 1.0

// System-wide config file path
#define SYSTEM_CONFIG_FILE "/etc/momentum_mouse.conf"

// Function declarations
static GKeyFile* load_config(void);
static void save_config(GKeyFile *key_file, GtkWidget *parent);
static gboolean detect_gnome_natural_scrolling(void);
static void set_gnome_natural_scrolling(gboolean natural);

// Function to detect GNOME natural scrolling setting
static gboolean detect_gnome_natural_scrolling(void) {
    gboolean natural = FALSE;
    GError *error = NULL;
    gchar *stdout_data = NULL;
    gint exit_status;
    
    // Try to get the GNOME natural scrolling setting
    if (g_spawn_command_line_sync(
            "gsettings get org.gnome.desktop.peripherals.mouse natural-scroll",
            &stdout_data, NULL, &exit_status, &error)) {
        
        if (exit_status == 0 && stdout_data != NULL) {
            // Trim whitespace
            g_strstrip(stdout_data);
            
            // Check if it's set to true
            if (g_strcmp0(stdout_data, "true") == 0) {
                natural = TRUE;
            }
        }
    }
    
    if (error) {
        g_error_free(error);
    }
    
    g_free(stdout_data);
    return natural;
}

// Function to set GNOME natural scrolling setting
static void set_gnome_natural_scrolling(gboolean natural) {
    GError *error = NULL;
    gchar *command = g_strdup_printf(
        "gsettings set org.gnome.desktop.peripherals.mouse natural-scroll %s",
        natural ? "true" : "false");
    
    // Try to set the GNOME setting, but don't worry if it fails
    g_spawn_command_line_async(command, &error);
    
    if (error) {
        // Just log the error, don't show to user
        g_warning("Failed to set GNOME natural scrolling: %s", error->message);
        g_error_free(error);
    }
    
    g_free(command);
}

// Function to handle apply button click
static void on_apply_clicked(GtkWidget *widget, gpointer data) {
    (void)widget; // Suppress unused parameter warning
    
    // Retrieve widgets from the grid.
    GtkWidget **widgets = (GtkWidget **)data;
    GtkWidget *sens_scale = widgets[0];
    GtkWidget *mult_scale = widgets[1];
    GtkWidget *fric_scale = widgets[2];
    GtkWidget *vel_scale = widgets[3];
    GtkWidget *natural_switch = widgets[4];
    GtkWidget *grab_switch = widgets[5];
    GtkWidget *device_combo = widgets[6];  // New device combo box

    gdouble sensitivity = gtk_range_get_value(GTK_RANGE(sens_scale));
    gdouble multiplier = gtk_range_get_value(GTK_RANGE(mult_scale));
    gdouble friction = gtk_range_get_value(GTK_RANGE(fric_scale));
    gdouble max_velocity = gtk_range_get_value(GTK_RANGE(vel_scale));
    gboolean natural = gtk_switch_get_active(GTK_SWITCH(natural_switch));
    gboolean grab = gtk_switch_get_active(GTK_SWITCH(grab_switch));
    gboolean drag = gtk_switch_get_active(GTK_SWITCH(widgets[7]));
    gdouble resolution_mult = gtk_range_get_value(GTK_RANGE(widgets[8]));
    gint refresh_rate_val = (gint)gtk_range_get_value(GTK_RANGE(widgets[9]));
    gdouble stop_threshold = gtk_range_get_value(GTK_RANGE(widgets[10]));
    gboolean multitouch = gtk_switch_get_active(GTK_SWITCH(widgets[11]));
    gboolean horizontal = gtk_switch_get_active(GTK_SWITCH(widgets[12]));
    gboolean debug_mode = gtk_switch_get_active(GTK_SWITCH(widgets[13]));

    // Get the selected device
    gchar *selected_device = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(device_combo));
    
    // Load existing config, update values, and save.
    GKeyFile *config = load_config();
    g_key_file_set_double(config, CONFIG_GROUP, "sensitivity", sensitivity);
    g_key_file_set_double(config, CONFIG_GROUP, "multiplier", multiplier);
    g_key_file_set_double(config, CONFIG_GROUP, "friction", friction);
    g_key_file_set_double(config, CONFIG_GROUP, "max_velocity", max_velocity);
    g_key_file_set_boolean(config, CONFIG_GROUP, "natural", natural);
    g_key_file_set_boolean(config, CONFIG_GROUP, "grab", grab);
    g_key_file_set_boolean(config, CONFIG_GROUP, "mouse_move_drag", drag);
    g_key_file_set_double(config, CONFIG_GROUP, "resolution_multiplier", resolution_mult);
    g_key_file_set_integer(config, CONFIG_GROUP, "refresh_rate", refresh_rate_val);
    g_key_file_set_double(config, CONFIG_GROUP, "inertia_stop_threshold", stop_threshold);
    g_key_file_set_boolean(config, CONFIG_GROUP, "multitouch", multitouch);
    g_key_file_set_boolean(config, CONFIG_GROUP, "horizontal", horizontal);
    g_key_file_set_boolean(config, CONFIG_GROUP, "debug", debug_mode);
    
    // Handle device selection
    if (selected_device && strcmp(selected_device, "Auto-detect (recommended)") != 0 && 
        strcmp(selected_device, "No devices found") != 0) {
        // Extract the device name from the combo box text (format: "Name (path)")
        gchar *device_name = g_strdup(selected_device);
        gchar *paren = strrchr(device_name, '(');
        if (paren) {
            *(paren - 1) = '\0';  // Cut off at the space before the parenthesis
            g_key_file_set_string(config, CONFIG_GROUP, "device_name", device_name);
        }
        g_free(device_name);
    } else {
        // Remove the device_name key if auto-detect is selected
        g_key_file_remove_key(config, CONFIG_GROUP, "device_name", NULL);
    }
    
    g_free(selected_device);
    
    // Optionally update the GNOME setting
    set_gnome_natural_scrolling(natural);
    
    // Get the parent window for the notification
    GtkWidget *parent_window = gtk_widget_get_toplevel(widget);
    if (!gtk_widget_is_toplevel(parent_window)) {
        parent_window = NULL;
    }
    
    save_config(config, parent_window);
    g_key_file_free(config);
}

// Load settings from config file into a GKeyFile structure.
static GKeyFile* load_config(void) {
    GKeyFile *key_file = g_key_file_new();
    GError *error = NULL;
    if (!g_key_file_load_from_file(key_file, SYSTEM_CONFIG_FILE, G_KEY_FILE_NONE, &error)) {
        // File might not exist yet; ignore error and continue with defaults.
        g_clear_error(&error);
    }
    return key_file;
}


// Callback function for auto-closing dialog
static gboolean auto_close_dialog(gpointer data) {
    GtkWidget *dialog = GTK_WIDGET(data);
    gtk_widget_destroy(dialog);
    return FALSE; // Don't repeat the timeout
}

// Function to show a success notification
static void show_success_notification(GtkWindow *parent_window) {
    // Create a dialog with a success message
    GtkWidget *dialog = gtk_message_dialog_new(
        parent_window,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_NONE,
        "Settings saved successfully!");
    
    // Add a subtitle with more details
    gtk_message_dialog_format_secondary_text(
        GTK_MESSAGE_DIALOG(dialog),
        "The momentum mouse service has been restarted with your new settings.");
    
    // Set up a timer to auto-close the dialog after 2 seconds
    g_timeout_add(2000, auto_close_dialog, dialog);
    
    // Show the dialog
    gtk_widget_show_all(dialog);
}

// Save settings from key_file back to disk.
static void save_config(GKeyFile *key_file, GtkWidget *parent) {
    GError *error = NULL;
    gchar *data = g_key_file_to_data(key_file, NULL, &error);
    if (error) {
        g_printerr("Error converting config to data: %s\n", error->message);
        g_error_free(error);
        return;
    }
    
    // Since we're running as root, we can directly write to the system config file
    if (!g_file_set_contents(SYSTEM_CONFIG_FILE, data, -1, &error)) {
        g_printerr("Error writing config file: %s\n", error->message);
        g_error_free(error);
        g_free(data);
        return;
    }
    
    // Directly restart the service (we're already root)
    gint exit_status;
    if (!g_spawn_command_line_sync("systemctl restart momentum_mouse.service", 
                                  NULL, NULL, &exit_status, &error)) {
        g_printerr("Error restarting service: %s\n", error->message);
        g_error_free(error);
        g_free(data);
        return;
    } else if (exit_status != 0) {
        g_printerr("Service restart command failed with exit status %d\n", exit_status);
        g_free(data);
        return;
    }
    
    g_free(data);
    g_print("Settings saved and service restarted.\n");
    
    // Show a success notification
    if (parent && GTK_IS_WINDOW(parent)) {
        show_success_notification(GTK_WINDOW(parent));
    }
}

// Callback for expander toggled
static void on_expander_notify(GObject *object, GParamSpec *param_spec, gpointer user_data) {
    (void)param_spec; // Unused
    GtkExpander *expander = GTK_EXPANDER(object);
    GtkWidget *scrolled_window = GTK_WIDGET(user_data);
    gboolean expanded = gtk_expander_get_expanded(expander);
    
    GtkWidget *parent_box = gtk_widget_get_parent(GTK_WIDGET(expander));
    
    if (expanded) {
        gtk_widget_set_vexpand(GTK_WIDGET(expander), TRUE);
        gtk_widget_set_vexpand(scrolled_window, TRUE);
        if (parent_box && GTK_IS_BOX(parent_box)) {
            gtk_box_set_child_packing(GTK_BOX(parent_box), GTK_WIDGET(expander), TRUE, TRUE, 0, GTK_PACK_START);
        }
    } else {
        gtk_widget_set_vexpand(GTK_WIDGET(expander), FALSE);
        gtk_widget_set_vexpand(scrolled_window, FALSE);
        if (parent_box && GTK_IS_BOX(parent_box)) {
            gtk_box_set_child_packing(GTK_BOX(parent_box), GTK_WIDGET(expander), FALSE, FALSE, 0, GTK_PACK_START);
        }
    }
}

// Function to reliably detect GNOME dark mode across sudo/pkexec
static gboolean detect_system_dark_mode(void) {
    const gchar *dark_mode_env = g_getenv("MOMENTUM_DARK_MODE");
    if (dark_mode_env && strcmp(dark_mode_env, "1") == 0) {
        return TRUE;
    }

    gchar *out = NULL;
    gchar *cmd1 = NULL;
    gchar *cmd2 = NULL;
    const gchar *sudo_user = g_getenv("SUDO_USER");
    const gchar *pkexec_uid = g_getenv("PKEXEC_UID");
    
    if (sudo_user) {
        cmd1 = g_strdup_printf("su - %s -c 'gsettings get org.gnome.desktop.interface color-scheme 2>/dev/null'", sudo_user);
        cmd2 = g_strdup_printf("su - %s -c 'gsettings get org.gnome.desktop.interface gtk-theme 2>/dev/null'", sudo_user);
    } else if (pkexec_uid) {
        cmd1 = g_strdup_printf("su - $(id -nu %s) -c 'gsettings get org.gnome.desktop.interface color-scheme 2>/dev/null'", pkexec_uid);
        cmd2 = g_strdup_printf("su - $(id -nu %s) -c 'gsettings get org.gnome.desktop.interface gtk-theme 2>/dev/null'", pkexec_uid);
    } else {
        cmd1 = g_strdup("gsettings get org.gnome.desktop.interface color-scheme 2>/dev/null");
        cmd2 = g_strdup("gsettings get org.gnome.desktop.interface gtk-theme 2>/dev/null");
    }

    gboolean is_dark = FALSE;
    if (cmd1 && g_spawn_command_line_sync(cmd1, &out, NULL, NULL, NULL)) {
        if (out && strstr(out, "prefer-dark")) {
            is_dark = TRUE;
        }
        g_free(out);
    }
    
    if (!is_dark && cmd2 && g_spawn_command_line_sync(cmd2, &out, NULL, NULL, NULL)) {
        if (out && strstr(out, "-dark")) {
            is_dark = TRUE;
        }
        g_free(out);
    }

    g_free(cmd1);
    g_free(cmd2);
    return is_dark;
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    // Load configuration
    GKeyFile *config = load_config();

    // Read existing values or fall back to defaults.
    gdouble sensitivity = g_key_file_get_double(config, CONFIG_GROUP, "sensitivity", NULL);
    if (sensitivity == 0) sensitivity = DEFAULT_SENSITIVITY;
    gdouble multiplier = g_key_file_get_double(config, CONFIG_GROUP, "multiplier", NULL);
    if (multiplier == 0) multiplier = DEFAULT_MULTIPLIER;
    gdouble friction = g_key_file_get_double(config, CONFIG_GROUP, "friction", NULL);
    if (friction == 0) friction = DEFAULT_FRICTION;
    gdouble max_velocity = g_key_file_get_double(config, CONFIG_GROUP, "max_velocity", NULL);
    if (max_velocity == 0) max_velocity = DEFAULT_MAX_VELOCITY;

    // Create main window.
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Momentum Mouse");
    gtk_window_set_default_size(GTK_WINDOW(window), 450, 600);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    // Create a box for the main layout (vertical)
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    
    // Get env var for dark mode (useful when launching via pkexec)
    if (detect_system_dark_mode()) {
        g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", TRUE, NULL);
    }
    
    gtk_container_set_border_width(GTK_CONTAINER(main_box), 10);
    gtk_container_add(GTK_CONTAINER(window), main_box);
    
    // Add the icon at the top
    GtkWidget *icon_image = NULL;
    
    // Try to load the icon from standard locations
    const gchar *icon_paths[] = {
        "/usr/local/share/icons/hicolor/256x256/apps/momentum_mouse.png",
        "/usr/local/share/pixmaps/momentum_mouse.png",
        "/usr/share/icons/hicolor/256x256/apps/momentum_mouse.png",
        "/usr/share/icons/hicolor/128x128/apps/momentum_mouse.png",
        "/usr/share/pixmaps/momentum_mouse.png",
        "debian/icons/momentum_mouse.png",
        "../debian/icons/momentum_mouse.png"  // For development environment
    };
    
    for (unsigned int i = 0; i < G_N_ELEMENTS(icon_paths); i++) {
        if (g_file_test(icon_paths[i], G_FILE_TEST_EXISTS)) {
            icon_image = gtk_image_new_from_file(icon_paths[i]);
            break;
        }
    }
    
    if (icon_image) {
        // Resize the icon to 256x256
        GdkPixbuf *original_pixbuf = gtk_image_get_pixbuf(GTK_IMAGE(icon_image));
        if (original_pixbuf) {
            GdkPixbuf *resized_pixbuf = gdk_pixbuf_scale_simple(
                original_pixbuf, 
                256,  // width
                256,  // height
                GDK_INTERP_BILINEAR);
                
            if (resized_pixbuf) {
                // Replace the image with the resized version
                gtk_image_set_from_pixbuf(GTK_IMAGE(icon_image), resized_pixbuf);
                g_object_unref(resized_pixbuf);
            }
        }
        
        // Create a centered container for the icon
        GtkWidget *icon_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_box_pack_start(GTK_BOX(icon_box), icon_image, TRUE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(main_box), icon_box, FALSE, FALSE, 10);
    }
    
    // Add a title label
    GtkWidget *title_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title_label), "<span size='large' weight='bold'>Momentum Mouse Settings</span>");
    gtk_box_pack_start(GTK_BOX(main_box), title_label, FALSE, FALSE, 5);
    
    // Create a grid for the settings
    GtkWidget *options_scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(options_scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(options_scrolled, -1, 200); // Allow it to shrink down if needed
    gtk_widget_set_vexpand(options_scrolled, TRUE);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_add(GTK_CONTAINER(options_scrolled), grid);
    gtk_box_pack_start(GTK_BOX(main_box), options_scrolled, TRUE, TRUE, 0);
    
    // Create a combo box for device selection
    GtkWidget *device_label = gtk_label_new("Input Device:");
    gtk_widget_set_halign(device_label, GTK_ALIGN_END);
    GtkWidget *device_combo = gtk_combo_box_text_new();
    gtk_widget_set_hexpand(device_combo, TRUE);
    gtk_widget_set_halign(device_combo, GTK_ALIGN_FILL);
    gtk_widget_set_tooltip_text(device_combo, 
        "Select the mouse or input device to use for smooth scrolling.");

    // Populate the combo box with available devices
    InputDevice *devices;
    int device_count = list_input_devices(&devices);
    if (device_count > 0) {
        // Add a "Auto-detect" option first
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(device_combo), "Auto-detect (recommended)");
        
        // Add all mouse devices
        for (int i = 0; i < device_count; i++) {
            // Skip the momentum mouse Trackpad
            if (devices[i].is_mouse && strstr(devices[i].name, "momentum mouse Trackpad") == NULL) {
                char device_entry[PATH_MAX + 256 + 4]; // Size to fit name (256) + path (PATH_MAX) + " ()" + null terminator
                snprintf(device_entry, sizeof(device_entry), "%s (%s)", 
                         devices[i].name, devices[i].path);
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(device_combo), device_entry);
            }
        }
        
        // Add all other devices
        for (int i = 0; i < device_count; i++) {
            // Skip the momentum mouse Trackpad
            if (!devices[i].is_mouse && strstr(devices[i].name, "momentum mouse Trackpad") == NULL) {
                char device_entry[PATH_MAX + 256 + 4]; // Size to fit name (256) + path (PATH_MAX) + " ()" + null terminator
                snprintf(device_entry, sizeof(device_entry), "%s (%s)", 
                         devices[i].name, devices[i].path);
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(device_combo), device_entry);
            }
        }
        
        // Set active item based on config
        gchar *device_name = g_key_file_get_string(config, CONFIG_GROUP, "device_name", NULL);
        if (device_name) {
            // Try to find and select the device in the combo box
            int found = 0;
            for (int i = 0; i < device_count; i++) {
                // Skip the momentum mouse Trackpad
                if (strstr(devices[i].name, "momentum mouse Trackpad") != NULL) {
                    continue;
                }
                
                // Check if the device name starts with the configured name
                // This handles cases where the stored name is truncated
                if (strncmp(devices[i].name, device_name, strlen(device_name)) == 0) {
                    // +1 because index 0 is "Auto-detect"
                    int index = 1;
                    // Count how many devices we've added to the combo box before this one
                    for (int j = 0; j < i; j++) {
                        if ((devices[j].is_mouse == devices[i].is_mouse) && 
                            strstr(devices[j].name, "momentum mouse Trackpad") == NULL) {
                            index++;
                        }
                    }
                    
                    // If this is not a mouse, add the count of all mice
                    if (!devices[i].is_mouse) {
                        for (int j = 0; j < device_count; j++) {
                            if (devices[j].is_mouse && 
                                strstr(devices[j].name, "momentum mouse Trackpad") == NULL) {
                                index++;
                            }
                        }
                    }
                    
                    gtk_combo_box_set_active(GTK_COMBO_BOX(device_combo), index);
                    found = 1;
                    break;
                }
            }
            
            if (!found) {
                // Default to auto-detect
                gtk_combo_box_set_active(GTK_COMBO_BOX(device_combo), 0);
            }
            
            g_free(device_name);
        } else {
            // Default to auto-detect
            gtk_combo_box_set_active(GTK_COMBO_BOX(device_combo), 0);
        }
        
        free_input_devices(devices, device_count);
    } else {
        // If no devices found, just add a placeholder
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(device_combo), "No devices found");
        gtk_combo_box_set_active(GTK_COMBO_BOX(device_combo), 0);
        gtk_widget_set_sensitive(device_combo, FALSE);
    }

    // Add the device selection to the grid
    gtk_grid_attach(GTK_GRID(grid), device_label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), device_combo, 1, 0, 1, 1);
    
    // Sensitivity slider
    GtkWidget *sens_label = gtk_label_new("Sensitivity:");
    gtk_widget_set_halign(sens_label, GTK_ALIGN_END);
    GtkWidget *sens_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.1, 4.0, 0.1);
    gtk_range_set_value(GTK_RANGE(sens_scale), sensitivity);
    gtk_widget_set_hexpand(sens_scale, TRUE);
    gtk_widget_set_halign(sens_scale, GTK_ALIGN_FILL);
    gtk_widget_set_tooltip_text(sens_scale, 
        "The strength of each mouse scroll wheel turn. Higher values make each scroll input have more effect on velocity.");
    gtk_grid_attach(GTK_GRID(grid), sens_label, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), sens_scale, 1, 1, 1, 1);

    // Multiplier slider
    GtkWidget *mult_label = gtk_label_new("Multiplier:");
    gtk_widget_set_halign(mult_label, GTK_ALIGN_END);
    GtkWidget *mult_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.1, 10.0, 0.1);
    gtk_range_set_value(GTK_RANGE(mult_scale), multiplier);
    gtk_widget_set_hexpand(mult_scale, TRUE);
    gtk_widget_set_halign(mult_scale, GTK_ALIGN_FILL);
    gtk_widget_set_tooltip_text(mult_scale, 
        "The consecutive scroll multiplier. Higher values make repeated scrolling accelerate faster.");
    gtk_grid_attach(GTK_GRID(grid), mult_label, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), mult_scale, 1, 2, 1, 1);

    // Friction slider
    GtkWidget *fric_label = gtk_label_new("Friction:");
    gtk_widget_set_halign(fric_label, GTK_ALIGN_END);
    GtkWidget *fric_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.1, 5.0, 0.1);
    gtk_range_set_value(GTK_RANGE(fric_scale), friction);
    gtk_widget_set_hexpand(fric_scale, TRUE);
    gtk_widget_set_halign(fric_scale, GTK_ALIGN_FILL);
    gtk_widget_set_tooltip_text(fric_scale, 
        "The rate at which scrolling slows down over time. Higher values make scrolling stop quicker, lower values make it glide longer.");
    gtk_grid_attach(GTK_GRID(grid), fric_label, 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), fric_scale, 1, 3, 1, 1);

    // Max Velocity slider
    GtkWidget *vel_label = gtk_label_new("Max Velocity:");
    gtk_widget_set_halign(vel_label, GTK_ALIGN_END);
    GtkWidget *vel_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.2, 10.0, 0.1);
    gtk_range_set_value(GTK_RANGE(vel_scale), max_velocity);
    gtk_widget_set_hexpand(vel_scale, TRUE);
    gtk_widget_set_halign(vel_scale, GTK_ALIGN_FILL);
    gtk_widget_set_tooltip_text(vel_scale, 
        "The maximum speed that inertia scrolling can reach. Limits how fast content can scroll.");
    gtk_grid_attach(GTK_GRID(grid), vel_label, 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), vel_scale, 1, 4, 1, 1);

    // Natural scrolling switch
    GtkWidget *natural_label = gtk_label_new("Natural Scrolling:");
    gtk_widget_set_halign(natural_label, GTK_ALIGN_END);
    GtkWidget *natural_switch = gtk_switch_new();

    // Try to detect current GNOME setting
    gboolean natural_detected = detect_gnome_natural_scrolling();

    // Read from config or use detected value
    gboolean natural = FALSE;
    if (g_key_file_has_key(config, CONFIG_GROUP, "natural", NULL)) {
        natural = g_key_file_get_boolean(config, CONFIG_GROUP, "natural", NULL);
    } else {
        natural = natural_detected;
    }

    gtk_switch_set_active(GTK_SWITCH(natural_switch), natural);
    gtk_widget_set_halign(natural_switch, GTK_ALIGN_END);
    gtk_widget_set_tooltip_text(natural_switch, 
        "When enabled, scrolling direction is reversed to match touchpad behavior (content follows finger movement).");
    gtk_grid_attach(GTK_GRID(grid), natural_label, 0, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), natural_switch, 1, 5, 1, 1);
    
    // Grab device switch
    GtkWidget *grab_label = gtk_label_new("Exclusive Grab:");
    gtk_widget_set_halign(grab_label, GTK_ALIGN_END);
    GtkWidget *grab_switch = gtk_switch_new();

    // Read grab setting from config
    gboolean grab = TRUE; // Default to true
    if (g_key_file_has_key(config, CONFIG_GROUP, "grab", NULL)) {
        grab = g_key_file_get_boolean(config, CONFIG_GROUP, "grab", NULL);
    }

    gtk_switch_set_active(GTK_SWITCH(grab_switch), grab);
    gtk_widget_set_halign(grab_switch, GTK_ALIGN_END);
    gtk_widget_set_tooltip_text(grab_switch, 
        "When enabled, mouse input is captured exclusively for better performance, especially at low sensitivity.");
    gtk_grid_attach(GTK_GRID(grid), grab_label, 0, 6, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), grab_switch, 1, 6, 1, 1);
    
    // Mouse Move Drag switch
    GtkWidget *drag_label = gtk_label_new("Mouse Move Drag:");
    gtk_widget_set_halign(drag_label, GTK_ALIGN_END);
    GtkWidget *drag_switch = gtk_switch_new();

    // Read mouse_move_drag setting from config
    gboolean drag = TRUE; // Default to true
    if (g_key_file_has_key(config, CONFIG_GROUP, "mouse_move_drag", NULL)) {
        drag = g_key_file_get_boolean(config, CONFIG_GROUP, "mouse_move_drag", NULL);
    }

    gtk_switch_set_active(GTK_SWITCH(drag_switch), drag);
    gtk_widget_set_halign(drag_switch, GTK_ALIGN_END);
    gtk_widget_set_tooltip_text(drag_switch, 
        "When enabled, moving the mouse during scrolling will slow down the scrolling.");
    gtk_grid_attach(GTK_GRID(grid), drag_label, 0, 7, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), drag_switch, 1, 7, 1, 1);

    // Resolution Multiplier slider
    GtkWidget *res_label = gtk_label_new("Resolution Multiplier:");
    gtk_widget_set_halign(res_label, GTK_ALIGN_END);
    GtkWidget *res_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.5, 20.0, 0.5);
    gdouble resolution_mult = g_key_file_get_double(config, CONFIG_GROUP, "resolution_multiplier", NULL);
    if (resolution_mult == 0) resolution_mult = 10.0; // Default
    gtk_range_set_value(GTK_RANGE(res_scale), resolution_mult);
    gtk_widget_set_hexpand(res_scale, TRUE);
    gtk_widget_set_halign(res_scale, GTK_ALIGN_FILL);
    gtk_widget_set_tooltip_text(res_scale, 
        "Multiplier for virtual trackpad resolution. Higher values increase precision but may cause issues.");
    gtk_grid_attach(GTK_GRID(grid), res_label, 0, 8, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), res_scale, 1, 8, 1, 1);

    // Refresh Rate slider
    GtkWidget *rate_label = gtk_label_new("Refresh Rate (Hz):");
    gtk_widget_set_halign(rate_label, GTK_ALIGN_END);
    GtkWidget *rate_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 30, 2500, 10);
    gint refresh_rate_val = g_key_file_get_integer(config, CONFIG_GROUP, "refresh_rate", NULL);
    if (refresh_rate_val == 0) refresh_rate_val = 200; // Default
    gtk_range_set_value(GTK_RANGE(rate_scale), refresh_rate_val);
    gtk_widget_set_hexpand(rate_scale, TRUE);
    gtk_widget_set_halign(rate_scale, GTK_ALIGN_FILL);
    gtk_widget_set_tooltip_text(rate_scale, 
        "Refresh rate for inertia updates. Lower values reduce CPU usage but will feel less smooth.");
    gtk_grid_attach(GTK_GRID(grid), rate_label, 0, 9, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), rate_scale, 1, 9, 1, 1);

    // Inertia Stop Threshold slider
    GtkWidget *stop_label = gtk_label_new("Inertia Stop Threshold:");
    gtk_widget_set_halign(stop_label, GTK_ALIGN_END);
    GtkWidget *stop_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.5, 50.0, 0.5);
    gdouble stop_threshold = g_key_file_get_double(config, CONFIG_GROUP, "inertia_stop_threshold", NULL);
    if (stop_threshold == 0) stop_threshold = DEFAULT_INERTIA_STOP_THRESHOLD; // Default
    gtk_range_set_value(GTK_RANGE(stop_scale), stop_threshold);
    gtk_widget_set_hexpand(stop_scale, TRUE);
    gtk_widget_set_halign(stop_scale, GTK_ALIGN_FILL);
    gtk_widget_set_tooltip_text(stop_scale,
        "Velocity threshold below which inertia stops. Higher values allow inertia to continue at lower speeds.");
    gtk_grid_attach(GTK_GRID(grid), stop_label, 0, 10, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), stop_scale, 1, 10, 1, 1);

    // Multitouch switch
    GtkWidget *multitouch_label = gtk_label_new("Emulate Multitouch:");
    gtk_widget_set_halign(multitouch_label, GTK_ALIGN_END);
    GtkWidget *multitouch_switch = gtk_switch_new();
    gboolean use_multitouch = TRUE;
    if (g_key_file_has_key(config, CONFIG_GROUP, "multitouch", NULL)) {
        use_multitouch = g_key_file_get_boolean(config, CONFIG_GROUP, "multitouch", NULL);
    }
    gtk_switch_set_active(GTK_SWITCH(multitouch_switch), use_multitouch);
    gtk_widget_set_halign(multitouch_switch, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), multitouch_label, 0, 11, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), multitouch_switch, 1, 11, 1, 1);

    // Horizontal switch
    GtkWidget *horizontal_label = gtk_label_new("Horizontal Scrolling:");
    gtk_widget_set_halign(horizontal_label, GTK_ALIGN_END);
    GtkWidget *horizontal_switch = gtk_switch_new();
    gboolean use_horizontal = FALSE;
    if (g_key_file_has_key(config, CONFIG_GROUP, "horizontal", NULL)) {
        use_horizontal = g_key_file_get_boolean(config, CONFIG_GROUP, "horizontal", NULL);
    }
    gtk_switch_set_active(GTK_SWITCH(horizontal_switch), use_horizontal);
    gtk_widget_set_halign(horizontal_switch, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), horizontal_label, 0, 12, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), horizontal_switch, 1, 12, 1, 1);

    // Debug switch
    GtkWidget *debug_label = gtk_label_new("Debug Logging:");
    gtk_widget_set_halign(debug_label, GTK_ALIGN_END);
    GtkWidget *debug_switch = gtk_switch_new();
    gboolean use_debug = FALSE;
    if (g_key_file_has_key(config, CONFIG_GROUP, "debug", NULL)) {
        use_debug = g_key_file_get_boolean(config, CONFIG_GROUP, "debug", NULL);
    }
    gtk_switch_set_active(GTK_SWITCH(debug_switch), use_debug);
    gtk_widget_set_halign(debug_switch, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), debug_label, 0, 13, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), debug_switch, 1, 13, 1, 1);

    // Apply button
    GtkWidget *apply_button = gtk_button_new_with_label("Apply");
    gtk_widget_set_hexpand(apply_button, TRUE);
    gtk_widget_set_halign(apply_button, GTK_ALIGN_FILL);
    gtk_grid_attach(GTK_GRID(grid), apply_button, 0, 14, 2, 1); // Adjusted row index
    
    // Create an array of widget pointers to pass as data
    GtkWidget *widgets[] = {
        sens_scale, mult_scale, fric_scale, vel_scale, natural_switch, grab_switch, device_combo,
        drag_switch, res_scale, rate_scale, stop_scale,
        multitouch_switch, horizontal_switch, debug_switch
    };
    g_signal_connect(apply_button, "clicked", G_CALLBACK(on_apply_clicked), widgets);

    // Add Expander for log viewer
    GtkWidget *expander = gtk_expander_new("Realtime Logs");
    // Pack it with expand=FALSE initially, its vexpand property will handle dynamic expanding
    gtk_box_pack_start(GTK_BOX(main_box), expander, FALSE, FALSE, 0);

    // Scrolled window inside expander
    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(scrolled_window, -1, 150); // smaller minimum so it fits on small screens
    gtk_container_add(GTK_CONTAINER(expander), scrolled_window);

    // Connect the expander signal to dynamically allocate vertical space when opened
    g_signal_connect(expander, "notify::expanded", G_CALLBACK(on_expander_notify), scrolled_window);

    // Text view inside scrolled window
    GtkWidget *text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(text_view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD_CHAR);
    gtk_container_add(GTK_CONTAINER(scrolled_window), text_view);

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    g_object_set_data(G_OBJECT(buffer), "text_view", text_view);

    gint out_fd;
    gchar *journal_argv[] = {"journalctl", "-u", "momentum_mouse.service", "-f", "-n", "20", NULL};
    GError *error = NULL;
    if (g_spawn_async_with_pipes(NULL, journal_argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, &out_fd, NULL, &error)) {
        GIOChannel *channel = g_io_channel_unix_new(out_fd);
        g_io_channel_set_flags(channel, G_IO_FLAG_NONBLOCK, NULL);
        g_io_add_watch(channel, G_IO_IN | G_IO_HUP | G_IO_ERR, read_log_output, buffer);
    }

    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}

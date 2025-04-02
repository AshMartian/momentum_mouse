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
    gtk_window_set_default_size(GTK_WINDOW(window), 400, 350);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    // Create a box for the main layout (vertical)
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(main_box), 10);
    gtk_container_add(GTK_CONTAINER(window), main_box);
    
    // Add the icon at the top
    GtkWidget *icon_image = NULL;
    
    // Try to load the icon from standard locations
    const gchar *icon_paths[] = {
        "/usr/share/icons/hicolor/128x128/apps/momentum_mouse.png",
        "/usr/share/pixmaps/momentum_mouse.png",
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
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_box_pack_start(GTK_BOX(main_box), grid, TRUE, TRUE, 0);
    
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

    // Apply button
    GtkWidget *apply_button = gtk_button_new_with_label("Apply");
    gtk_widget_set_hexpand(apply_button, TRUE);
    gtk_widget_set_halign(apply_button, GTK_ALIGN_FILL);
    gtk_grid_attach(GTK_GRID(grid), apply_button, 0, 11, 2, 1); // Adjusted row index
    
    // Create an array of widget pointers to pass as data
    GtkWidget *widgets[] = {
        sens_scale, mult_scale, fric_scale, vel_scale, natural_switch, grab_switch, device_combo,
        drag_switch, res_scale, rate_scale, stop_scale // Added stop_scale
    };
    g_signal_connect(apply_button, "clicked", G_CALLBACK(on_apply_clicked), widgets);

    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}

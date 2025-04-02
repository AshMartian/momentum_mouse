#ifndef momentum_mouse_H
#define momentum_mouse_H

// Include the input_event struct definition
#include <linux/input.h>
#include <linux/limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <signal.h> // For sig_atomic_t

// Scroll direction enum
typedef enum {
    SCROLL_DIRECTION_TRADITIONAL = 0,  // Wheel up = content up
    SCROLL_DIRECTION_NATURAL = 1       // Wheel up = content down (like touchpads)
} ScrollDirection;

// Scroll axis enum
typedef enum {
    SCROLL_AXIS_VERTICAL = 0,   // Standard vertical scrolling
    SCROLL_AXIS_HORIZONTAL = 1  // Horizontal scrolling
} ScrollAxis;

#define SCROLL_QUEUE_SIZE 64 // Adjust size as needed

typedef struct {
    int deltas[SCROLL_QUEUE_SIZE];
    int head;
    int tail;
    int count;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} ScrollQueue;

// Structure to represent an input device
typedef struct {
    char path[PATH_MAX];     // Device path (e.g., /dev/input/event7)
    char name[256];          // Device name (e.g., BY Tech Gaming Keyboard Mouse)
    int is_mouse;            // Whether this device is likely a mouse
} InputDevice;

// Global configuration variables
extern int grab_device;  // Whether to grab the device exclusively
extern ScrollDirection scroll_direction;
extern ScrollAxis scroll_axis;     // Whether to scroll vertically or horizontally
extern int debug_mode;  // Whether to output debug messages
extern int daemon_mode; // Whether to run as a daemon
extern double scroll_sensitivity;  // How much inertia is applied per scroll
extern double scroll_multiplier;   // How much the initial scroll is multiplied
extern double scroll_friction;     // How quickly scrolling slows down (higher = faster stop)
extern int auto_detect_direction;
extern int use_multitouch;
extern double current_velocity;    // Current scrolling velocity
extern double current_position;    // Current scrolling position
extern double max_velocity_factor; // Maximum velocity as a factor of screen dimensions
extern double sensitivity_divisor; // Divisor for sensitivity when using touchpad
extern double resolution_multiplier; // Multiplier for virtual trackpad resolution
extern int refresh_rate; // Refresh rate in Hz for inertia updates
extern char *device_override;      // Device path override
extern int mouse_move_drag;        // Whether mouse movement should slow down scrolling

// Global flag for signal handling and thread control
// Defined and initialized at file scope in momentum_mouse.c
extern volatile sig_atomic_t running;

// Shared scroll queue
extern ScrollQueue scroll_queue; // Defined in momentum_mouse.c

// Mutex for protecting inertia state and signals (velocity, position, active, stop/friction flags)
extern pthread_mutex_t state_mutex; // Defined in momentum_mouse.c
// Condition variable for state changes (stop/friction signals, potentially inertia updates)
extern pthread_cond_t state_cond; // Defined in momentum_mouse.c

// Flags for communication between threads (protected by state_mutex)
extern bool stop_requested; // Defined in momentum_mouse.c
extern int pending_friction_magnitude; // Store magnitude for friction request // Defined in momentum_mouse.c

// Thread IDs
extern pthread_t input_thread_id; // Defined in momentum_mouse.c
extern pthread_t inertia_thread_id; // Defined in momentum_mouse.c

// Structure to track boundary reset information
typedef struct {
    struct timeval reset_time;
    double reset_velocity;
    double reset_position;
    int reset_direction;
} BoundaryResetInfo;

extern BoundaryResetInfo boundary_reset_info;
extern int boundary_reset_in_progress;
extern struct timeval last_boundary_reset_time;
extern double inertia_stop_threshold; // Velocity threshold below which inertia stops

// Original event emitter functions
int setup_virtual_device(void);
int emit_scroll_event(int value);
int emit_passthrough_event(struct input_event *ev);
void destroy_virtual_device(void);

// New multitouch emitter functions
int setup_virtual_multitouch_device(void);
int emit_two_finger_scroll_event(int delta);
void end_multitouch_gesture(void);
void destroy_virtual_multitouch_device(void);

// Inertia logic functions
void update_inertia(int delta);
void process_inertia(void);
// void process_inertia_mt(void); // Removed - logic is now in inertia_thread_func
void start_inertia(int initial_velocity);
void stop_inertia(void);
int is_inertia_active(void);
void apply_mouse_friction(int movement_magnitude);

// Input capture functions
int initialize_input_capture(const char *device_override);
int capture_input_event(void);
void cleanup_input_capture(void);

// System settings detection
int detect_scroll_direction(void);

// Configuration file handling
void load_config_file(const char *filename);

// Debug logging
void debug_log(const char *format, ...);

// Device scanning functions
int list_input_devices(InputDevice **devices);
void free_input_devices(InputDevice *devices, int count);
char* find_device_by_name(const char *device_name);

// Thread functions
void* input_thread_func(void* arg);
void* inertia_thread_func(void* arg);

// Function to reset finger positions (used by inertia thread after boundary)
void reset_finger_positions(void);
void jump_finger_positions(int delta); // New function for boundary jump

#endif

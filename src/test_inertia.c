#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "momentum_mouse.h"

// Mock functions to avoid linking with the full application
int emit_two_finger_scroll_event(int delta) {
    printf("SCROLL: %d\n", delta);
    return 0;
}

int emit_scroll_event(int value) {
    printf("SCROLL EVENT: %d\n", value);
    return 0;
}

void end_multitouch_gesture(void) {
    printf("END GESTURE\n");
}

// Global variables needed for testing
int screen_width = 1920;
int screen_height = 1080;
int post_boundary_frames = 0;
int boundary_reset_in_progress = 0;
struct timeval last_boundary_reset_time = {0, 0};
BoundaryResetInfo boundary_reset_info = {{0, 0}, 0.0, 0.0, 0};
ScrollDirection scroll_direction = SCROLL_DIRECTION_TRADITIONAL;
ScrollAxis scroll_axis = SCROLL_AXIS_VERTICAL;  // Default to vertical scrolling
double scroll_sensitivity = 1.0;
double scroll_multiplier = 1.0;
double scroll_friction = 2.0;
double max_velocity_factor = 0.8;
double sensitivity_divisor = 1.0;
int debug_mode = 1;
int mouse_move_drag = 1;

// Test case for direction changes during inertia
void test_direction_change(void) {
    printf("=== TEST: Direction Change During Inertia ===\n");
    
    // Reset inertia state
    stop_inertia();
    
    // Simulate scrolling up (negative delta in traditional mode)
    printf("Simulating scroll up (delta=-1)...\n");
    update_inertia(-1);
    
    // Print initial state
    printf("Initial velocity: %.2f\n", current_velocity);
    
    // Simulate time passing and inertia processing
    printf("Processing inertia for 10 frames...\n");
    for (int i = 0; i < 10; i++) {
        process_inertia_mt();
        usleep(50000); // 50ms between frames
        printf("Frame %d: velocity=%.2f, position=%.2f\n", i, current_velocity, current_position);
    }
    
    // Now simulate scrolling in the opposite direction
    printf("\nSimulating scroll down (delta=1) during inertia...\n");
    update_inertia(1);
    
    // Print state after direction change
    printf("Velocity after direction change: %.2f\n", current_velocity);
    
    // Continue processing inertia
    printf("Processing inertia for 10 more frames...\n");
    for (int i = 0; i < 10; i++) {
        process_inertia_mt();
        usleep(50000); // 50ms between frames
        printf("Frame %d: velocity=%.2f, position=%.2f\n", i, current_velocity, current_position);
    }
    
    printf("Test completed.\n\n");
}

// Test case for mouse movement during inertia
void test_mouse_movement_during_inertia(void) {
    printf("=== TEST: Mouse Movement During Inertia ===\n");
    
    // Reset inertia state
    stop_inertia();
    
    // Simulate scrolling
    printf("Simulating scroll (delta=-1)...\n");
    update_inertia(-1);
    
    // Print initial state
    printf("Initial velocity: %.2f\n", current_velocity);
    
    // Process inertia for a few frames
    printf("Processing inertia for 5 frames...\n");
    for (int i = 0; i < 5; i++) {
        process_inertia_mt();
        usleep(50000);
        printf("Frame %d: velocity=%.2f, position=%.2f\n", i, current_velocity, current_position);
    }
    
    // Simulate mouse movement
    printf("\nSimulating mouse movement...\n");
    apply_mouse_friction(10); // 10 pixels of movement
    
    // Print state after mouse movement
    printf("Velocity after mouse movement: %.2f\n", current_velocity);
    
    // Continue processing inertia
    printf("Processing inertia for 5 more frames...\n");
    for (int i = 0; i < 5; i++) {
        process_inertia_mt();
        usleep(50000);
        printf("Frame %d: velocity=%.2f, position=%.2f\n", i, current_velocity, current_position);
    }
    
    printf("Test completed.\n\n");
}

// Test with mouse move drag disabled
void test_mouse_movement_drag_disabled(void) {
    printf("=== TEST: Mouse Movement with Drag Disabled ===\n");
    
    // Reset inertia state
    stop_inertia();
    
    // Disable mouse move drag
    mouse_move_drag = 0;
    
    // Simulate scrolling
    printf("Simulating scroll (delta=-1)...\n");
    update_inertia(-1);
    
    // Print initial state
    printf("Initial velocity: %.2f\n", current_velocity);
    
    // Process inertia for a few frames
    printf("Processing inertia for 5 frames...\n");
    for (int i = 0; i < 5; i++) {
        process_inertia_mt();
        usleep(50000);
        printf("Frame %d: velocity=%.2f, position=%.2f\n", i, current_velocity, current_position);
    }
    
    // Simulate mouse movement
    printf("\nSimulating mouse movement with drag disabled...\n");
    apply_mouse_friction(10); // 10 pixels of movement
    
    // Print state after mouse movement
    printf("Velocity after mouse movement: %.2f\n", current_velocity);
    
    // Continue processing inertia
    printf("Processing inertia for 5 more frames...\n");
    for (int i = 0; i < 5; i++) {
        process_inertia_mt();
        usleep(50000);
        printf("Frame %d: velocity=%.2f, position=%.2f\n", i, current_velocity, current_position);
    }
    
    // Re-enable mouse move drag for other tests
    mouse_move_drag = 1;
    
    printf("Test completed.\n\n");
}

int main(void) {
    // Seed random number generator
    srand(time(NULL));
    
    // Run tests
    test_direction_change();
    test_mouse_movement_during_inertia();
    test_mouse_movement_drag_disabled();
    
    return 0;
}

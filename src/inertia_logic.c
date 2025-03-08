#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <sys/time.h>
#include "inertia_scroller.h"

// Use a double for finer precision.
static double current_velocity = 0.0;
static int inertia_active = 0;
static struct timeval last_time = {0};

// Helper to get time difference in seconds between two timeval structs.
static double time_diff_in_seconds(struct timeval *start, struct timeval *end) {
    double seconds = (double)(end->tv_sec - start->tv_sec);
    double microseconds = (double)(end->tv_usec - start->tv_usec) / 1000000.0;
    return seconds + microseconds;
}

// Called when a new physical scroll event is captured.
// This updates the current velocity based on incoming scroll events.
void update_inertia(int delta) {
    current_velocity += (double)delta;
    inertia_active = 1;
    // Reset the timer on new input.
    gettimeofday(&last_time, NULL);
}

// Optionally, explicitly start inertia with an initial velocity.
void start_inertia(int initial_velocity) {
    current_velocity = (double)initial_velocity;
    inertia_active = 1;
    gettimeofday(&last_time, NULL);
}

// Call this to cancel any ongoing inertia fling.
void stop_inertia(void) {
    current_velocity = 0.0;
    inertia_active = 0;
    last_time.tv_sec = 0;
    last_time.tv_usec = 0;
}

// This function should be called periodically in the main loop.
// It emits synthetic scroll events based on the current velocity and decays that velocity over time.
void process_inertia(void) {
    if (!inertia_active) {
        return;
    }
    
    struct timeval now;
    gettimeofday(&now, NULL);
    double dt = time_diff_in_seconds(&last_time, &now);
    last_time = now;
    
    // If the velocity is negligible, stop the inertia.
    if (fabs(current_velocity) < 0.5) {
        stop_inertia();
        return;
    }
    
    // Emit a synthetic scroll event with the current velocity (rounded to the nearest integer).
    int event_val = (int)round(current_velocity);
    if (emit_scroll_event(event_val) < 0) {
        fprintf(stderr, "Failed to emit scroll event in inertia processing.\n");
    }
    
    // Apply an exponential decay to simulate friction.
    // Adjust the friction coefficient (here set to 2.0) to tune deceleration.
    const double friction = 2.0;
    current_velocity *= exp(-friction * dt);
    
    // Sleep a little to approximate a 60 FPS update (the dt measurement handles timing accuracy).
    usleep(16000);
}

// This function is similar to process_inertia() but emits multitouch events instead
void process_inertia_mt(void) {
    if (!inertia_active) {
        return;
    }
    
    struct timeval now;
    gettimeofday(&now, NULL);
    double dt = time_diff_in_seconds(&last_time, &now);
    last_time = now;
    
    // If the velocity is negligible, stop the inertia.
    if (fabs(current_velocity) < 0.5) {
        stop_inertia();
        return;
    }
    
    // Emit a synthetic multitouch scroll event with the current velocity (rounded to the nearest integer).
    int event_val = (int)round(current_velocity);
    if (emit_two_finger_scroll_event(event_val) < 0) {
        fprintf(stderr, "Failed to emit multitouch scroll event in inertia processing.\n");
    }
    
    // Apply an exponential decay to simulate friction.
    // Adjust the friction coefficient (here set to 2.0) to tune deceleration.
    const double friction = 2.0;
    current_velocity *= exp(-friction * dt);
    
    // Sleep a little to approximate a 60 FPS update (the dt measurement handles timing accuracy).
    usleep(16000);
}

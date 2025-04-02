#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <sys/time.h>
#include <pthread.h> // Add this
#include <errno.h>   // Add this for ETIMEDOUT
#include <stdbool.h> // Ensure this is included
#include "momentum_mouse.h"

// Forward declarations for functions used in this file
extern void end_multitouch_gesture(void);
extern int screen_width;
extern int screen_height;
extern int post_boundary_frames;  // Flag from event_emitter_mt.c

// Use a double for finer precision.
// Keep these global for now, but remember they need state_mutex protection
double current_velocity = 0.0;  // Make accessible to other files
int inertia_active = 0; // Made non-static for mutex access in is_inertia_active
struct timeval last_time = {0}; // Made non-static for mutex access
// Make current_position accessible to other files that need to reset it
double current_position = 0.0; // Keep only this position variable

// Threshold for detecting a significant direction change vs minor overshoot
#define DIRECTION_CHANGE_VELOCITY_THRESHOLD 10.0

// Helper to get time difference in seconds between two timeval structs.
static double time_diff_in_seconds(struct timeval *start, struct timeval *end) {
    double seconds = (double)(end->tv_sec - start->tv_sec);
    double microseconds = (double)(end->tv_usec - start->tv_usec) / 1000000.0;
    return seconds + microseconds;
}

// Called when a new physical scroll event is captured.
// This updates the current velocity based on incoming scroll events.
void update_inertia(int delta) {
    // Invert delta for natural scrolling
    if (scroll_direction == SCROLL_DIRECTION_NATURAL) {
        delta = -delta;
    }
    
    // Get the current time for timing calculations
    struct timeval now;
    gettimeofday(&now, NULL);
    
    // Check if we're in a boundary reset
    if (boundary_reset_in_progress) {
        // Calculate time since last boundary reset
        double time_since_reset = time_diff_in_seconds(&last_boundary_reset_time, &now);
        
        // If we're in the early stages of a boundary reset (first 100ms)
        if (time_since_reset < 0.1) {
            if (debug_mode) {
                printf("BOUNDARY: Ignoring scroll input during boundary reset (%.3fs after reset)\n", 
                       time_since_reset);
            }
            
            // Don't update velocity during early boundary reset
            // Just update the last_time to prevent time gaps
            last_time = now;
            return;
        }
        
        // For inputs slightly after the reset (100-300ms), blend them in gradually
        if (time_since_reset < 0.3) {
            // Scale the delta based on how far we are into the reset
            double scale_factor = (time_since_reset - 0.1) / 0.2;  // 0.0 to 1.0
            delta = (int)(delta * scale_factor);
            
            if (debug_mode) {
                printf("BOUNDARY: Scaling scroll input during boundary reset by %.2f\n", scale_factor);
            }
            
            // If delta becomes zero after scaling, just update time and return
            if (delta == 0) {
                last_time = now;
                return;
            }
        }
    }
    
    // Check if this is a new scroll sequence or continuing an existing one
    double dt = 0.0;
    if (last_time.tv_sec != 0) {
        dt = time_diff_in_seconds(&last_time, &now);
    }
    last_time = now;
    
    
    // Store the old velocity for smoothing
    double old_velocity = current_velocity;
    
    // If this is a direction change during active inertia, handle it specially
    // Only trigger if the current velocity is significant enough
    if (inertia_active &&
        fabs(current_velocity) > DIRECTION_CHANGE_VELOCITY_THRESHOLD && ((current_velocity > 0 && delta < 0) || (current_velocity < 0 && delta > 0))) {
        if (debug_mode) {
            printf("Direction change detected during inertia: velocity=%.2f, delta=%d\n",
                   current_velocity, delta);
        }
         
        // Stop inertia completely. The rest of the function will handle
        // starting the new movement as if it were the beginning of a scroll sequence.
        stop_inertia();
         
        // DO NOT set velocity/position here.
        // DO NOT set inertia_active = 1 here.
        // DO NOT return here. Let the rest of the function execute.
    }
     
    // Determine if this is a continuation of scrolling in the same direction
    // For initial scroll, use base sensitivity without multiplier
    // Increase base factor for more initial impact
    double velocity_factor = 60.0 * (scroll_sensitivity / sensitivity_divisor); // Increased base factor
    
    if (inertia_active) {
        // If scrolling in the same direction as current velocity and within a short time window
        if (((current_velocity > 0 && delta > 0) || (current_velocity < 0 && delta < 0)) && dt < 0.3) {
            // Enhance the effect for consecutive scrolls in the same direction
            // Apply the multiplier only for consecutive scrolls
            // Also increase base factor here
            velocity_factor = (60.0 + (fabs(current_velocity) / 3.0)) *  // Increased base factor
                             (scroll_sensitivity / sensitivity_divisor) * scroll_multiplier;
            
            if (debug_mode) {
                printf("Consecutive scroll in same direction, applying multiplier: %.2f, velocity factor: %.2f\n", 
                       scroll_multiplier, velocity_factor);
            }
        }
    }
    
    // Apply the velocity change
    // Calculate target velocity
    double target_velocity = current_velocity + (double)delta * velocity_factor;
    
    // Smooth the velocity change - blend old and new velocities
    double blend_factor = 0.7;  // 70% new, 30% old
    current_velocity = (target_velocity * blend_factor) + (old_velocity * (1.0 - blend_factor));
    
    // Cap the velocity based on screen dimensions
    double max_velocity;
    if (scroll_axis == SCROLL_AXIS_VERTICAL) {
        max_velocity = screen_height * max_velocity_factor;
    } else {
        max_velocity = screen_width * max_velocity_factor;
    }
    
    // Apply the cap
    if (current_velocity > max_velocity) {
        current_velocity = max_velocity;
        if (debug_mode) {
            printf("Capped velocity to maximum: %.2f\n", max_velocity);
        }
    } else if (current_velocity < -max_velocity) {
        current_velocity = -max_velocity;
        if (debug_mode) {
            printf("Capped velocity to minimum: %.2f\n", -max_velocity);
        }
    }
    
    // Update position - use a larger factor for more responsive initial movement
    // For initial scroll, don't apply multiplier
    if (!inertia_active) {
        // Initial scroll - don't apply multiplier, use increased base factor
        current_position += (double)delta * 40.0 * (scroll_sensitivity / sensitivity_divisor); // Increased base factor
    } else {
        // Consecutive scroll in same direction - apply multiplier, use increased base factor
        current_position += (double)delta * 40.0 * (scroll_sensitivity / sensitivity_divisor) * // Increased base factor
                           (((current_velocity > 0 && delta > 0) || (current_velocity < 0 && delta < 0)) ? 
                            scroll_multiplier : 1.0);
    }
    
    if (debug_mode) {
        printf("Updated velocity: %.2f, position: %.2f\n", current_velocity, current_position);
    }
    
    inertia_active = 1;
}

// Optionally, explicitly start inertia with an initial velocity.
// NOTE: This function should also be called under state_mutex if used externally.
void start_inertia(int initial_velocity) {
    current_velocity = (double)initial_velocity;
    inertia_active = 1;
    gettimeofday(&last_time, NULL);
}

// Call this to cancel any ongoing inertia fling.
// MUST be called with state_mutex HELD.
void stop_inertia(void) {
    current_velocity = 0.0;
    inertia_active = 0;
    last_time.tv_sec = 0;
    last_time.tv_usec = 0;
    // Gesture ending is handled in inertia_thread_func after calling this
}

// Check if inertia is currently active (Thread-safe version)
int is_inertia_active(void) {
    pthread_mutex_lock(&state_mutex);
    int active = inertia_active;
    pthread_mutex_unlock(&state_mutex);
    return active;
}

// Apply friction based on mouse movement
// MUST be called with state_mutex HELD.
void apply_mouse_friction(int movement_magnitude) {
    if (!inertia_active || !mouse_move_drag) {
        return;
    }
    
    // Calculate friction factor based on movement magnitude
    // Make it much gentler - small movements = very small friction
    // Adjust friction based on sensitivity and scroll_friction
    double friction_factor = (0.01 + (movement_magnitude * 0.0001)) * scroll_friction / sqrt(scroll_sensitivity);
    
    // Cap the friction factor to a lower value
    // Adjust cap based on sensitivity and scroll_friction
    double max_friction = 0.05 * scroll_friction / sqrt(scroll_sensitivity);
    if (friction_factor > max_friction) friction_factor = max_friction;  // Reduced from 0.95
    

    // Apply the friction by reducing velocity
    current_velocity *= (1.0 - friction_factor);
    
    // if (debug_mode && movement_magnitude > 10) {
    //     printf("Mouse friction: movement=%d, factor=%.3f, velocity: %.2f -> %.2f\n", 
    //            movement_magnitude, friction_factor, old_velocity, current_velocity);
    // }

    // Only stop inertia if velocity becomes extremely small
    if (fabs(current_velocity) < inertia_stop_threshold) {
        if (debug_mode) {
            printf("Velocity too low (%.2f < %.2f), stopping inertia\n",
                   current_velocity, inertia_stop_threshold);
        }
        stop_inertia();
    }
    
    // Update the last time to prevent time-based friction from being applied immediately
    gettimeofday(&last_time, NULL); // Already under state_mutex
}


// Helper to get future time as timespec for timedwait
static void get_future_time(struct timespec *ts, int milliseconds) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    ts->tv_sec = tv.tv_sec + (milliseconds / 1000);
    ts->tv_nsec = (tv.tv_usec * 1000) + ((milliseconds % 1000) * 1000000);
    ts->tv_sec += ts->tv_nsec / 1000000000;
    ts->tv_nsec %= 1000000000;
}


// Inertia processing thread function
void* inertia_thread_func(void* arg) {
    (void)arg; // Mark parameter as unused
    printf("Inertia thread started.\n");
    int dequeued_delta;
    bool state_changed_this_cycle; // Track if queue/signal processing happened
    int event_val_to_emit = 0; // Store event value calculated under lock
    bool should_emit_event = false; // Flag to control emission
    // bool needs_boundary_reset_action = false; // Removed - Boundary actions handled in emitter

    // Ensure last_time is initialized before first use
    pthread_mutex_lock(&state_mutex);
    if (last_time.tv_sec == 0 && last_time.tv_usec == 0) {
         gettimeofday(&last_time, NULL);
    }
    pthread_mutex_unlock(&state_mutex);


    while (running) {
        state_changed_this_cycle = false;
        should_emit_event = false;
        event_val_to_emit = 0;
        // needs_boundary_reset_action = false; // Removed

        // --- 1. Wait for and Process Queue/Signals ---
        pthread_mutex_lock(&scroll_queue.mutex);
        // Wait only if queue is empty AND no stop/friction signal is pending
        pthread_mutex_lock(&state_mutex);
        bool signals_pending = stop_requested || (pending_friction_magnitude > 0);
        pthread_mutex_unlock(&state_mutex);

        while (scroll_queue.count == 0 && !signals_pending && running) {
            // Wait for data or timeout (e.g., 10ms) to allow inertia processing
            struct timespec wait_time;
            // Use a shorter wait time if inertia is active to ensure timely updates
            pthread_mutex_lock(&state_mutex);
            int wait_ms = inertia_active ? 5 : 10; // 5ms if active, 10ms otherwise
            pthread_mutex_unlock(&state_mutex);

            get_future_time(&wait_time, wait_ms);
            int rc = pthread_cond_timedwait(&scroll_queue.cond, &scroll_queue.mutex, &wait_time);

            if (rc == ETIMEDOUT) {
                break; // Timeout, proceed to inertia processing
            } else if (rc != 0 && running) {
                 perror("InertiaThread: scroll_queue pthread_cond_timedwait error");
                 // Handle error appropriately, maybe break or continue
            }
            // If woken up, re-check loop condition (queue count, signals, running)
            pthread_mutex_lock(&state_mutex);
            signals_pending = stop_requested || (pending_friction_magnitude > 0);
            pthread_mutex_unlock(&state_mutex);
        }
        // Queue mutex is still held here

        // --- 1a. Process Signals (Stop/Friction) ---
        pthread_mutex_lock(&state_mutex);
        if (stop_requested) {
            if (inertia_active) {
                 stop_inertia(); // Resets velocity, active flag, last_time
                 if (use_multitouch) {
                     // Call end_multitouch_gesture OUTSIDE the lock later
                     should_emit_event = false; // Don't emit on stop frame
                 }
            }
            stop_requested = false; // Reset flag
            state_changed_this_cycle = true;
        }
        if (pending_friction_magnitude > 0) {
             if (debug_mode > 1) printf("InertiaThread: Friction request received (mag=%d).\n", pending_friction_magnitude);
             if (inertia_active && mouse_move_drag) {
                 // apply_mouse_friction needs state_mutex, which we hold
                 apply_mouse_friction(pending_friction_magnitude);
             }
             pending_friction_magnitude = 0; // Reset magnitude
             state_changed_this_cycle = true;
        }
        pthread_mutex_unlock(&state_mutex);


        // --- 1b. Process Scroll Queue ---
        // Dequeue and process all available deltas
        while (scroll_queue.count > 0) {
            dequeued_delta = scroll_queue.deltas[scroll_queue.tail];
            scroll_queue.tail = (scroll_queue.tail + 1) % SCROLL_QUEUE_SIZE;
            scroll_queue.count--;
            state_changed_this_cycle = true;

            // Unlock queue mutex before calling update_inertia (which needs state_mutex)
            pthread_mutex_unlock(&scroll_queue.mutex);

            // --- Process the dequeued delta ---
            pthread_mutex_lock(&state_mutex);
            if (debug_mode > 1) printf("InertiaThread: Processing delta %d\n", dequeued_delta);
            // update_inertia needs state_mutex, which we hold
            update_inertia(dequeued_delta); // Updates velocity, position, active flag, last_time
            pthread_mutex_unlock(&state_mutex);

            // Re-lock queue mutex to check loop condition
            pthread_mutex_lock(&scroll_queue.mutex);
        }
        pthread_mutex_unlock(&scroll_queue.mutex); // Unlock queue mutex


        // --- 2. Process Inertia Calculation (if active) ---
        pthread_mutex_lock(&state_mutex);
        if (inertia_active) {
            // Removed boundary reset timeout check - handled implicitly by emitter logic

            struct timeval now;
            gettimeofday(&now, NULL);
            // Ensure last_time is valid before calculating dt
            double dt = (last_time.tv_sec == 0 && last_time.tv_usec == 0) ? 0.0 : time_diff_in_seconds(&last_time, &now);
            last_time = now; // Update last_time under mutex

            // Prevent huge dt if thread was stalled
            if (dt > 0.1) { // e.g., > 100ms
                if (debug_mode) printf("InertiaThread: Warning - large dt detected: %.3fs, capping to 0.1s\n", dt);
                dt = 0.1;
            }

            // Apply time-based friction
            const double friction = (use_multitouch ? (0.6 * scroll_friction / sqrt(scroll_sensitivity)) : (2.0 * scroll_friction));
            double old_velocity = current_velocity;
            current_velocity *= exp(-friction * dt);
            if (debug_mode > 1 && fabs(old_velocity - current_velocity) > 0.1) {
                 printf("InertiaThread: Time friction (dt=%.4f): %.2f -> %.2f\n", dt, old_velocity, current_velocity);
            }


            // Calculate position delta / event value
            if (use_multitouch) {
                // Calculate potential position change based on current velocity and time delta
                double position_delta = current_velocity * dt; // Use the velocity directly (units/sec * sec)
                current_position += position_delta; // Update position based on inertia

                // Boundary check and clamping are now handled in emit_two_finger_scroll_event
                event_val_to_emit = (int)round(position_delta);
                if (event_val_to_emit != 0) {
                    should_emit_event = true;
                }
            } else {
                // Logic from process_inertia (Wheel events) - Remains the same
                event_val_to_emit = (int)round(current_velocity);
                if (event_val_to_emit != 0) {
                     should_emit_event = true;
                }
                // Note: process_inertia didn't update current_position, add if needed
            }

             // Check if inertia should stop due to low velocity
            if (fabs(current_velocity) < inertia_stop_threshold) {
                if (debug_mode) printf("InertiaThread: Velocity %.2f below threshold %.2f, stopping inertia.\n",
                                       current_velocity, inertia_stop_threshold);
                stop_inertia(); // Resets velocity, active flag, etc. (already under mutex)
                should_emit_event = false; // Don't emit event on the frame we stop
                // Gesture end signal is handled below based on state_changed_this_cycle
            }
        } // end if(inertia_active)

        // Store necessary state before releasing mutex if event emission is needed
        // End gesture if inertia stopped this cycle
        bool should_end_gesture = use_multitouch && !inertia_active && state_changed_this_cycle;
        pthread_mutex_unlock(&state_mutex);

        // --- 3. Emit Event / End Gesture (outside mutex lock) ---
        // Removed boundary reset action block - handled in emitter

        if (should_emit_event && event_val_to_emit != 0) {
             if (debug_mode > 1) printf("InertiaThread: Emitting event value %d\n", event_val_to_emit);
             if (use_multitouch) {
                 // emit_two_finger_scroll_event should NOT access shared state now
                 if (emit_two_finger_scroll_event(event_val_to_emit) < 0) {
                     fprintf(stderr, "InertiaThread: Failed to emit multitouch scroll event.\n");
                     // Optionally signal stop on error? Be careful of loops.
                     // signal_stop_request();
                 }
             } else {
                 if (emit_scroll_event(event_val_to_emit) < 0) {
                     fprintf(stderr, "InertiaThread: Failed to emit scroll event.\n");
                     // signal_stop_request();
                 }
             }
        }

        if (should_end_gesture) {
             end_multitouch_gesture(); // Call outside lock
        }

        // --- 4. Sleep if Idle ---
        // If no state changed and inertia isn't active, sleep a bit longer
        pthread_mutex_lock(&state_mutex);
        bool is_active = inertia_active;
        pthread_mutex_unlock(&state_mutex);
        if (!state_changed_this_cycle && !is_active) {
             usleep(20000); // Sleep 20ms if completely idle
        } else if (!state_changed_this_cycle && is_active) {
             // If inertia is active but no input/signals, rely on timedwait timeout
             // or add a very small sleep if timedwait is too long
             usleep(1000); // Optional 1ms sleep during active inertia
        }

    } // end while(running)

    printf("Inertia thread exiting.\n");
    // Ensure any final gesture is ended if multitouch was used and active
    pthread_mutex_lock(&state_mutex);
    bool final_gesture_end = use_multitouch && inertia_active;
    pthread_mutex_unlock(&state_mutex);
    if (final_gesture_end) {
         end_multitouch_gesture();
    }
    return NULL;
}

# Momentum Mouse: App Exclusions Architecture 

## The Core Problem: The Wayland Wall
`momentum_mouse` is a system-level daemon running as `root` so it can intercept and inject generic `/dev/input/` events directly into the kernel. 

By strict security design, Wayland completely isolates window information to prevent keyloggers or background apps from tracking user behavior. Therefore, a root-level `libevdev` daemon has no native awareness of which window or application is currently in focus. There is no simple `XGetInputFocus` equivalent in native Wayland that a background script can just poll. 

To solve this without reverting to insecure window-manager hacks or `/proc` scanning, we must divide the problem into a "privileged" input layer and an "unprivileged" observability layer that communicate securely.

---

## The Proposed Architecture
We will split the application exclusion logic into three primary components that work in tandem:

### 1. The Root Engine (Daemon)
**File:** `src/momentum_mouse.c`
*   **The Socket:** The daemon will create and listen on a lightweight UNIX Domain Socket (e.g., `/run/momentum_mouse.sock`) with appropriate permissions (`0666` or restricted by a dedicated group).
*   **The Logic:** It will maintain a global string state: `current_active_app`. In the main `libevdev` input loop, if `current_active_app` matches an ID in the user's `[Exclusions]` block, scrolling logic immediately bypasses the inertia calculations ("Pass-through Mode").
*   **The Hard Stop:** If the socket receives a notification that the focus *just transitioned* into an excluded app, the daemon immediately zeroes out its internal velocity buffers. This prevents "leftover" inertia from a web browser from spilling over into a CAD application or game.

### 2. The User-Space Focus Listener (Client)
**File:** `src/window_listener.c` (To be created) -- or a Python equivalent daemon.
*   **Execution:** Runs as the standard desktop user (e.g., `UID 1000`) silently in the background, starting automatically upon desktop login.
*   **The API (AT-SPI2):** Because it runs in the user's session, it can access the **AT-SPI2 (Accessibility Toolkit)** bus. AT-SPI2 is universally implemented across GNOME, KDE, and other modern desktops for screen readers.
*   **The Relay:** The Listener registers a listener for `object:state-changed:focused` D-Bus events. When a focus shift occurs, it extracts the target application's `.desktop` name or `wm_class` and pushes that string down the UNIX socket to our Root Engine.

### 3. The GUI (Configuration)
**File:** `gui/momentum_mouse_gui.c`
*   **The Exclusions Modal:** A new button or settings tab titled **"App Exclusions"**.
*   **App Discovery:** The GUI will read `.desktop` properties from `/usr/share/applications/` and `~/.local/share/applications/` to populate a list of installed, user-facing applications complete with their system icons.
*   **Persistence:** Toggling a checkbox for an app will write its `wm_class` or `.desktop` ID directly into `momentum_mouse.conf` (e.g., `exclusions=blender.desktop,steam_app_1234,org.gnome.Terminal;`).

---

## Implementation Roadmap (Phases)

### Phase 1: The Socket & Daemon Logic
1. Implement a non-blocking `pthread` inside `momentum_mouse.c` dedicated to listening on a UNIX Domain Socket.
2. Add an `exclusions` parsing layer to `config_reader.c` that loads the list into a scalable data structure (e.g., a hash table or linked list).
3. Implement the `halt_inertia()` trigger when receiving an excluded payload over the socket.
4. Integrate the pass-through bypass inside the event emitter loop.

### Phase 2: The User-Space Focus Listener
1. Write a small script/program (`momentum_mouse_window_listener`) using `libatspi` / `Atspi` / `PyGObject` to hook into the user's AT-SPI2 registry bus.
2. Filter the incoming barrage of AT-SPI bounds to only capture high-level `window` focus shifts.
3. Establish the socket connection to the root daemon and send minimalist strings (e.g., `"APP: blender"`).
4. Integrate the Listener into standard `~/.config/autostart/` or systemd `--user` targets.

### Phase 3: The GUI Exclusions Interface
1. Construct the GTK layout for a list of togglable applications.
2. Build a parser to extract `Name=` and `Icon=` metadata from standard FreeDesktop `.desktop` files.
3. Wire the checkmarks to modify the `momentum_mouse.conf` exclusions parameter and trigger a daemon reload.

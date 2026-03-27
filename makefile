CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -levdev -ludev -lm -lX11

SRCS = src/momentum_mouse.c src/input_capture.c src/event_emitter.c src/event_emitter_mt.c src/inertia_logic.c src/system_settings.c src/config_reader.c src/device_scanner.c
OBJS = $(SRCS:.c=.o)
TARGET = momentum_mouse
LISTENER_TARGET = momentum_mouse_window_listener

.PHONY: all backup install uninstall clean tests setup gui_target

all: backup $(TARGET) gui_target $(LISTENER_TARGET)

setup:
	@echo "Installing build dependencies..."
	chmod +x setup.sh
	sudo ./setup.sh

gui_target: $(OBJS)
	$(MAKE) -C gui

backup:
	@if [ -f $(TARGET) ]; then \
		echo "Backing up existing binary to $(TARGET)-prev"; \
		mv -f $(TARGET) $(TARGET)-prev 2>/dev/null || true; \
	fi

prefix = /usr
bindir = $(prefix)/bin
sysconfdir = /etc
systemddir = /lib/systemd/system

install: $(TARGET) gui_target $(LISTENER_TARGET)
	install -d $(DESTDIR)$(bindir)
	install -m 755 $(TARGET) $(DESTDIR)$(bindir)/$(TARGET)
	install -m 755 $(LISTENER_TARGET) $(DESTDIR)$(bindir)/$(LISTENER_TARGET)
	install -m 755 gui/momentum_mouse_gui $(DESTDIR)$(bindir)/momentum_mouse_gui
	install -m 755 gui/run_gui.sh $(DESTDIR)$(bindir)/momentum_mouse_gui_launcher
	install -d $(DESTDIR)$(sysconfdir)
	install -m 644 conf/momentum_mouse.conf.example $(DESTDIR)$(sysconfdir)/momentum_mouse.conf
	install -d $(DESTDIR)$(systemddir)
	install -m 644 debian/momentum_mouse.service $(DESTDIR)$(systemddir)/momentum_mouse.service
	install -d $(DESTDIR)/usr/share/polkit-1/actions/
	install -m 644 gui/org.momentum_mouse.gui.policy $(DESTDIR)/usr/share/polkit-1/actions/
	install -d $(DESTDIR)$(prefix)/share/applications/
	install -m 644 debian/momentum_mouse_gui.desktop $(DESTDIR)$(prefix)/share/applications/
	install -d $(DESTDIR)$(prefix)/share/icons/hicolor/256x256/apps/
	install -m 644 debian/icons/momentum_mouse.png $(DESTDIR)$(prefix)/share/icons/hicolor/256x256/apps/momentum_mouse.png
	install -d $(DESTDIR)$(sysconfdir)/xdg/autostart
	install -m 644 debian/momentum_mouse_listener.desktop $(DESTDIR)$(sysconfdir)/xdg/autostart/momentum_mouse_listener.desktop

uninstall:
	rm -f $(DESTDIR)$(bindir)/$(TARGET)
	rm -f $(DESTDIR)$(bindir)/$(LISTENER_TARGET)
	rm -f $(DESTDIR)$(bindir)/momentum_mouse_gui
	rm -f $(DESTDIR)$(bindir)/momentum_mouse_gui_launcher
	rm -f $(DESTDIR)$(sysconfdir)/momentum_mouse.conf
	rm -f $(DESTDIR)$(systemddir)/momentum_mouse.service
	rm -f $(DESTDIR)$(prefix)/share/applications/momentum_mouse_gui.desktop
	rm -f $(DESTDIR)$(prefix)/share/icons/hicolor/256x256/apps/momentum_mouse.png
	rm -f $(DESTDIR)$(sysconfdir)/xdg/autostart/momentum_mouse_listener.desktop

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

$(LISTENER_TARGET): src/window_listener.c
	$(CC) $(CFLAGS) -o $@ src/window_listener.c $$(pkg-config --cflags --libs atspi-2 gobject-2.0 glib-2.0)

%.o: %.c
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) $(LISTENER_TARGET) test_inertia
	$(MAKE) -C gui clean

test_inertia: src/test_inertia.c src/inertia_logic.o
	$(CC) $(CFLAGS) -Iinclude -o test_inertia src/test_inertia.c src/inertia_logic.o -lm -lpthread

test: tests

tests: test_inertia
	./test_inertia

CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -levdev -ludev -lm -lX11

SRCS = src/momentum_mouse.c src/input_capture.c src/event_emitter.c src/event_emitter_mt.c src/inertia_logic.c src/system_settings.c src/config_reader.c src/device_scanner.c
OBJS = $(SRCS:.c=.o)
TARGET = momentum_mouse

.PHONY: all backup install uninstall clean tests setup gui_target

all: backup $(TARGET) gui_target

setup:
	@echo "Installing build dependencies..."
	chmod +x setup.sh
	sudo ./setup.sh

gui_target: $(OBJS)
	$(MAKE) -C gui

backup:
	@if [ -f $(TARGET) ]; then \
		echo "Backing up existing binary to $(TARGET)-prev"; \
		mv $(TARGET) $(TARGET)-prev 2>/dev/null || true; \
	fi

prefix = /usr
bindir = $(prefix)/bin
sysconfdir = /etc
systemddir = /lib/systemd/system

install: $(TARGET) gui_target
	install -d $(DESTDIR)$(bindir)
	install -m 755 $(TARGET) $(DESTDIR)$(bindir)/$(TARGET)
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

uninstall:
	rm -f $(DESTDIR)$(bindir)/$(TARGET)
	rm -f $(DESTDIR)$(bindir)/momentum_mouse_gui
	rm -f $(DESTDIR)$(bindir)/momentum_mouse_gui_launcher
	rm -f $(DESTDIR)$(sysconfdir)/momentum_mouse.conf
	rm -f $(DESTDIR)$(systemddir)/momentum_mouse.service
	rm -f $(DESTDIR)$(prefix)/share/applications/momentum_mouse_gui.desktop
	rm -f $(DESTDIR)$(prefix)/share/icons/hicolor/256x256/apps/momentum_mouse.png

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
	$(MAKE) -C gui clean

test_inertia: src/test_inertia.c src/inertia_logic.o
	$(CC) $(CFLAGS) -Iinclude -o test_inertia src/test_inertia.c src/inertia_logic.o -lm -lpthread

tests: test_inertia
	./test_inertia

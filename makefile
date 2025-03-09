CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -levdev -ludev -lm -lX11

SRCS = src/inertia_scroller.c src/input_capture.c src/event_emitter.c src/event_emitter_mt.c src/inertia_logic.c src/system_settings.c src/config_reader.c
OBJS = $(SRCS:.c=.o)
TARGET = inertia_scroller

all: backup $(TARGET)

backup:
	@if [ -f $(TARGET) ]; then \
		echo "Backing up existing binary to $(TARGET)-prev"; \
		mv $(TARGET) $(TARGET)-prev 2>/dev/null || true; \
	fi

prefix = /usr/local
bindir = $(prefix)/bin
sysconfdir = /etc
systemddir = /lib/systemd/system

install: $(TARGET)
	install -d $(DESTDIR)$(bindir)
	install -m 755 $(TARGET) $(DESTDIR)$(bindir)/$(TARGET)
	install -d $(DESTDIR)$(sysconfdir)
	install -m 644 conf/inertia_scroller.conf.example $(DESTDIR)$(sysconfdir)/inertia_scroller.conf
	install -d $(DESTDIR)$(systemddir)
	install -m 644 debian/inertia_scroller.service $(DESTDIR)$(systemddir)/inertia_scroller.service

uninstall:
	rm -f $(DESTDIR)$(bindir)/$(TARGET)
	rm -f $(DESTDIR)$(sysconfdir)/inertia_scroller.conf
	rm -f $(DESTDIR)$(systemddir)/inertia_scroller.service

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

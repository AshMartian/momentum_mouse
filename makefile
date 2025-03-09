CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -levdev -ludev -lm -lX11

SRCS = src/momentum_mouse.c src/input_capture.c src/event_emitter.c src/event_emitter_mt.c src/inertia_logic.c src/system_settings.c src/config_reader.c
OBJS = $(SRCS:.c=.o)
TARGET = momentum_mouse

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
	install -m 644 conf/momentum_mouse.conf.example $(DESTDIR)$(sysconfdir)/momentum_mouse.conf
	install -d $(DESTDIR)$(systemddir)
	install -m 644 debian/momentum_mouse.service $(DESTDIR)$(systemddir)/momentum_mouse.service

uninstall:
	rm -f $(DESTDIR)$(bindir)/$(TARGET)
	rm -f $(DESTDIR)$(sysconfdir)/momentum_mouse.conf
	rm -f $(DESTDIR)$(systemddir)/momentum_mouse.service

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

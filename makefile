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

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

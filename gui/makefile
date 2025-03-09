# gui/Makefile
CC = gcc
CFLAGS = -Wall -Wextra -O2

TARGET = momentum_mouse_gui 
SRCS = momentum_mouse_gui.c 

# Check if GTK3 is installed
HAS_GTK := $(shell pkg-config --exists gtk+-3.0 && echo 1 || echo 0)
$(info GTK3 detection result: $(HAS_GTK))

ifeq ($(HAS_GTK), 1)
	CFLAGS += $(shell pkg-config --cflags gtk+-3.0 glib-2.0)
	LIBS = $(shell pkg-config --libs gtk+-3.0 glib-2.0) 
endif

all: check_gtk $(TARGET)

check_gtk:
ifeq ($(HAS_GTK), 0)
	@echo "Warning: GTK3 development libraries not found."
	@echo "Please install with: sudo apt-get install libgtk-3-dev"
	@echo "Building will be attempted but may fail."
endif

$(TARGET): $(SRCS)
ifeq ($(HAS_GTK), 1)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS) $(LIBS)
else
	@echo "Error: Cannot build GUI without GTK3 libraries."
	@echo "Checking pkg-config path: $$(which pkg-config)"
	@echo "GTK3 pkg-config status: $$(pkg-config --exists gtk+-3.0 && echo 'Found' || echo 'Not found')"
	@exit 1 
endif 

clean:
	rm -f $(TARGET)

.PHONY: all clean check_gtk

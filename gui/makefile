# gui/Makefile
CC = gcc
CFLAGS = -Wall -Wextra -O2

TARGET = momentum_mouse_gui 
SRCS = momentum_mouse_gui.c 
PARENT_OBJS = ../src/device_scanner.o

# Check if GTK3 is installed
HAS_GTK := $(shell pkg-config --exists gtk+-3.0 && echo 1 || echo 0)
$(info GTK3 detection result: $(HAS_GTK))

ifeq ($(HAS_GTK), 1)
	CFLAGS += $(shell pkg-config --cflags gtk+-3.0 glib-2.0) -I../include
	LIBS = $(shell pkg-config --libs gtk+-3.0 glib-2.0) 
endif

all: check_gtk $(TARGET)

check_gtk:
ifeq ($(HAS_GTK), 0)
	@echo "Warning: GTK3 development libraries not found."
	@echo "Please install with: sudo apt-get install libgtk-3-dev"
	@echo "Building will be attempted but may fail."
endif

$(TARGET): $(SRCS) $(PARENT_OBJS)
ifeq ($(HAS_GTK), 1)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS) $(PARENT_OBJS) $(LIBS)
	@echo "#!/bin/bash" > run_gui.sh
	@echo "if [ \"\$$(id -u)\" -eq 0 ]; then" >> run_gui.sh
	@echo "    # Already root, just run the GUI" >> run_gui.sh
	@echo "    ./$(TARGET)" >> run_gui.sh
	@echo "else" >> run_gui.sh
	@echo "    # Not root, use pkexec to get elevated privileges" >> run_gui.sh
	@echo "    pkexec env DISPLAY=\$$DISPLAY XAUTHORITY=\$$XAUTHORITY \$$(dirname \"\$$0\")/$(TARGET)" >> run_gui.sh
	@echo "fi" >> run_gui.sh
	@chmod +x run_gui.sh
	@echo "Created run_gui.sh wrapper script"
else
	@echo "Error: Cannot build GUI without GTK3 libraries."
	@echo "Checking pkg-config path: $$(which pkg-config)"
	@echo "GTK3 pkg-config status: $$(pkg-config --exists gtk+-3.0 && echo 'Found' || echo 'Not found')"
	@exit 1 
endif 

clean:
	rm -f $(TARGET)

.PHONY: all clean check_gtk

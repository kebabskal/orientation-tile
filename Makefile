# --no-gnu-unique is needed when building with g++ so the .so can be cleanly
# unloaded/reloaded by Hyprland; clang doesn't accept the flag.
ifeq ($(CXX),g++)
    EXTRA_FLAGS = --no-gnu-unique
else
    EXTRA_FLAGS =
endif

CXXFLAGS ?= -O2
CXXFLAGS += -shared -fPIC -std=c++2b -Wno-c++11-narrowing
INCLUDES = `pkg-config --cflags pixman-1 libdrm hyprland libinput libudev wayland-server xkbcommon`
LIBS =

SRC = main.cpp OrientationTileAlgorithm.cpp
TARGET = orientation-tile.so

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(EXTRA_FLAGS) $(INCLUDES) $^ -o $@ $(LIBS)

clean:
	rm -f ./$(TARGET)

.PHONY: all clean

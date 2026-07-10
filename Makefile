CXX ?= g++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -Wpedantic -O2
CPPFLAGS ?= -Isrc
BUILD_DIR := build
TARGET := $(BUILD_DIR)/toyc
SRCS := $(wildcard src/*.cpp)
HDRS := $(wildcard src/*.h)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS) Makefile
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SRCS) -o $(TARGET)

clean:
	rm -rf $(BUILD_DIR)

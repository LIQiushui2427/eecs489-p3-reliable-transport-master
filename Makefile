# Makefile for building executables from all .c and .cpp files in subdirectories

# Set the compiler and compiler flags
CC := gcc
CXX := g++
CFLAGS := -w
CXXFLAGS := -w

# Find all subdirectories in the current directory
SUBDIRECTORIES := $(wildcard */)

# Get the names of all .c and .cpp files in subdirectories
C_FILES := $(wildcard $(addsuffix *.c, $(SUBDIRECTORIES)))
CPP_FILES := $(wildcard $(addsuffix *.cpp, $(SUBDIRECTORIES)))

# Generate the names of corresponding .o files
C_OBJ_FILES := $(patsubst %.c, %.o, $(C_FILES))
CPP_OBJ_FILES := $(patsubst %.cpp, %.o, $(CPP_FILES))

# Generate the names of corresponding executables
C_EXE_FILES := $(patsubst %.c, %, $(C_FILES))
CPP_EXE_FILES := $(patsubst %.cpp, %, $(CPP_FILES))

# Default target: build all executables
all: $(C_EXE_FILES) $(CPP_EXE_FILES)

# Rule for building executables from .c files
%: %.c
	$(CC) $(CFLAGS) $< -o $@

# Rule for building executables from .cpp files
%: %.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

# Clean target: remove all generated files
clean:
	rm -f $(C_OBJ_FILES) $(CPP_OBJ_FILES) $(C_EXE_FILES) $(CPP_EXE_FILES)

.PHONY: all clean

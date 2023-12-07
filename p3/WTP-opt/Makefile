CXX = g++
CXXFLAGS = -std=c++11 -Wall

SRC_FILES := $(wildcard *.cpp)
EXE_FILES := $(patsubst %.cpp,%,$(SRC_FILES))

.PHONY: all clean

all: $(EXE_FILES)

%: %.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

clean:
	rm -f $(EXE_FILES)
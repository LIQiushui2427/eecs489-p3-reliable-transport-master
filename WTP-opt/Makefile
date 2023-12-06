CXX = g++
CXX_FLAGS = -std=c++11 -Wall -I header/
CPP =  $(wildcard *.cpp)
HEADER = $(wildcard *.h)

all: $(CPP) $(HEADER)
	$(CXX) $(CXX_FLAGS) wSender.cpp -o wSender
	$(CXX) $(CXX_FLAGS) wReceiver.cpp -o wReceiver

.PHONY: clean

clean:
	rm -rf wSender wReceiver

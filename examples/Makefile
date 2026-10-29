CXX = g++
CXXFLAGS = -O3 -mavx2 -mfma -std=c++17 $(shell pkg-config --cflags surreal)
LIBS = $(shell pkg-config --libs surreal)

all: simple_game

simple_game: simple_game.cpp
	$(CXX) $(CXXFLAGS) simple_game.cpp $(LIBS) -o simple_game

clean:
	rm -f simple_game

# Makefile for fuzzy-Max chess engine combining fuzzymax.c and position.cpp

# Compilers and flags
CC      = gcc
CXX     = g++
CFLAGS  = -O2 -Wall -std=c99
CXXFLAGS= -O2 -Wall -std=c++11
LDFLAGS =

# Target name
TARGET  = fuzzymax

# Object files
OBJS    = fuzzymax.o position.o

# Default target
all: $(TARGET)

# Compile fuzzymax.c using gcc
fuzzymax.o: fuzzymax.c
	$(CC) $(CFLAGS) -c fuzzymax.c -o fuzzymax.o

# Compile position.cpp using g++
position.o: position.cpp
	$(CXX) $(CXXFLAGS) -c position.cpp -o position.o

# Link the objects using g++ to ensure C++ libraries are included
$(TARGET): $(OBJS)
	$(CXX) $(LDFLAGS) $(OBJS) -o $(TARGET)

# Clean up generated files
clean:
	rm -f $(TARGET) $(OBJS)

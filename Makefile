# Makefile for fuzzy-Max chess engine combining fuzzymax.cc and position.d

# Compilers and flags
CXX      = g++
CXXFLAGS = -O2 -Wall -std=c++11
DCC      = ldc2
DCF      = -mtriple=arm64-apple-darwin -O -w

# Target name
TARGET   = fuzzymax

# Object files
OBJS     = fuzzymax.o position.o

# Default target
all: $(TARGET)

# Compile fuzzymax.cc using g++
fuzzymax.o: fuzzymax.cc
	$(CXX) $(CXXFLAGS) -c fuzzymax.cc -o fuzzymax.o

# Compile position.d using ldc2
position.o: position.d
	$(DCC) $(DCF) -c position.d -of=position.o

# Link the objects using ldc2 to include D runtime libraries
$(TARGET): $(OBJS)
	$(DCC) $(OBJS) -of=$(TARGET) -L-lstdc++

# Clean up generated files
clean:
	rm -f $(TARGET) $(OBJS)

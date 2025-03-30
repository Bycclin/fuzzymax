CXX      = g++
CXXFLAGS = -O2 -Wall -std=c++11
DCC      = ldc2
DCF      = -mtriple=arm64-apple-darwin -O -w
TARGET   = fuzzymax
OBJS     = fuzzymax.o position.o dmain.o
all: $(TARGET)
fuzzymax.o: fuzzymax.cc
	$(CXX) $(CXXFLAGS) -c fuzzymax.cc -o fuzzymax.o
position.o: position.d
	$(DCC) $(DCF) -c position.d -of=position.o
dmain.o: dmain.d
	$(DCC) $(DCF) -c dmain.d -of=dmain.o
$(TARGET): $(OBJS)
	$(DCC) $(OBJS) -of=$(TARGET) -L-lstdc++
clean:
	rm -f $(TARGET) $(OBJS)

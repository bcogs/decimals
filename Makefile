CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O3
LDTEST   = -lcriterion

all: test_decimals

decimals.o: decimals.cc decimals.h
	$(CXX) $(CXXFLAGS) -c decimals.cc -o decimals.o

test_decimals: test_decimals.cc decimals.o decimals.h
	$(CXX) $(CXXFLAGS) test_decimals.cc decimals.o -o test_decimals $(LDTEST)

test: test_decimals
	./test_decimals

clean:
	rm -f decimals.o test_decimals

.PHONY: all test clean

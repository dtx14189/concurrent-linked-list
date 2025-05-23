CXX = g++  # Compiler
CXXFLAGS = -std=c++17 -O3 -pthread # Compiler flags

# Target: Compile the OpenMP program
opt: optimistic-locking.o
	$(CXX) $(CXXFLAGS) -o opt optimistic-locking.o

# Compile the source file into an object file
optimistic-locking.o: optimistic-locking.cpp
	$(CXX) $(CXXFLAGS) -c optimistic-locking.cpp

# Clean: Remove compiled files
clean:
	rm -f opt optimistic-locking.o

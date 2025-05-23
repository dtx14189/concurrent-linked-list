CXX = g++ 
CXXFLAGS = -std=c++17 -O3 -pthread 

opt: main.cpp concurrent-linked-list.cpp 
	$(CXX) $(CXXFLAGS) -o opt main.cpp concurrent-linked-list.cpp 

clean:
	rm -f opt *.o

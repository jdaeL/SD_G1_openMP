CXX = g++
# -O3 maximiza el rendimiento. -fopenmp activa las directivas paralelas.
CXXFLAGS = -std=c++17 -O3 -fopenmp
TARGET = reorder_analysis
SRC = main.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)
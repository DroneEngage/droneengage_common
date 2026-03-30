CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -g
INCLUDES = -I..
SOURCES = test_read_env_flag.cpp ../helpers/helpers.cpp
TARGET = test_read_env_flag
OBJECTS = $(SOURCES:.cpp=.o)

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJECTS) $(TARGET)

help:
	@echo "Available targets:"
	@echo "  all    - Build the test executable"
	@echo "  test   - Build and run the test"
	@echo "  clean  - Remove built files"
	@echo "  help   - Show this help message"

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -g
INCLUDES = -I..
TARGETS = test_read_env_flag test_cap_schema test_ai_box

.PHONY: all clean test

all: $(TARGETS)

test_read_env_flag: test_read_env_flag.o ../helpers/helpers.o
	$(CXX) $(CXXFLAGS) -o $@ $^

test_cap_schema: test_cap_schema.o cap_schema.o
	$(CXX) $(CXXFLAGS) -o $@ $^

test_ai_box: test_ai_box.o
	$(CXX) $(CXXFLAGS) -o $@ $^

test_read_env_flag.o: test_read_env_flag.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

test_cap_schema.o: test_cap_schema.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

cap_schema.o: ../de_databus/cap_schema.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

test_ai_box.o: test_ai_box.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

../helpers/helpers.o: ../helpers/helpers.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

test: $(TARGETS)
	./test_read_env_flag
	./test_cap_schema
	./test_ai_box

clean:
	rm -f *.o ../helpers/helpers.o $(TARGETS)

help:
	@echo "Available targets:"
	@echo "  all    - Build the test executables"
	@echo "  test   - Build and run the tests"
	@echo "  clean  - Remove built files"
	@echo "  help   - Show this help message"

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -g
INCLUDES = -I..
TARGETS = test_read_env_flag test_cap_schema test_obs test_track_manager test_geo_projector_core test_camera_registry test_overlay_target test_cpu_monitor

.PHONY: all clean test

all: $(TARGETS)

test_read_env_flag: test_read_env_flag.o ../helpers/helpers.o
	$(CXX) $(CXXFLAGS) -o $@ $^

test_cap_schema: test_cap_schema.o cap_schema.o
	$(CXX) $(CXXFLAGS) -o $@ $^

test_obs: test_obs.o
	$(CXX) $(CXXFLAGS) -o $@ $^

test_read_env_flag.o: test_read_env_flag.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

test_cap_schema.o: test_cap_schema.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

cap_schema.o: ../de_databus/cap_schema.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

test_track_manager: test_track_manager.o
	$(CXX) $(CXXFLAGS) -o $@ $^

test_track_manager.o: test_track_manager.cpp ../helpers/track_manager.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

test_geo_projector_core: test_geo_projector_core.o cap_schema.o
	$(CXX) $(CXXFLAGS) -o $@ $^

test_geo_projector_core.o: test_geo_projector_core.cpp ../helpers/geo_projector_core.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

test_camera_registry: test_camera_registry.o
	$(CXX) $(CXXFLAGS) -o $@ $^

test_camera_registry.o: test_camera_registry.cpp ../helpers/camera_registry.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

test_overlay_target: test_overlay_target.o
	$(CXX) $(CXXFLAGS) -o $@ $^

test_overlay_target.o: test_overlay_target.cpp ../helpers/overlay_target.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

test_cpu_monitor: test_cpu_monitor.o de_cpu_monitor.o
	$(CXX) $(CXXFLAGS) -o $@ $^

test_cpu_monitor.o: test_cpu_monitor.cpp ../de_databus/de_cpu_monitor.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

de_cpu_monitor.o: ../de_databus/de_cpu_monitor.cpp ../de_databus/de_cpu_monitor.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

test_obs.o: test_obs.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

../helpers/helpers.o: ../helpers/helpers.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

test: $(TARGETS)
	./test_read_env_flag
	./test_cap_schema
	./test_obs
	./test_track_manager
	./test_geo_projector_core
	./test_camera_registry
	./test_overlay_target
	./test_cpu_monitor
	./test_geo_projector_core

clean:
	rm -f *.o ../helpers/helpers.o $(TARGETS)

help:
	@echo "Available targets:"
	@echo "  all    - Build the test executables"
	@echo "  test   - Build and run the tests"
	@echo "  clean  - Remove built files"
	@echo "  help   - Show this help message"

# Host-side tests. The DSP is portable header-only C++, so it is built and run
# on the development machine: no Daisy attached, no flashing.
#
#   make -C tests          run everything
#   make -C tests render   write demo WAVs to tests/build/ to listen to

CXX ?= c++
CXXFLAGS = -O2 -g -std=c++17 -ffast-math -Wall -Wextra -Wno-unused-parameter -I../src

.PHONY: all dsp ui render clean
all: dsp ui

dsp: build/dsp_test
	@./build/dsp_test

ui: build/ui_test
	@./build/ui_test

render: build/render
	@./build/render

build/dsp_test: dsp_test.cpp harness.h $(wildcard ../src/dsp/*.h) | build
	@$(CXX) $(CXXFLAGS) $< -o $@

build/ui_test: ui_test.cpp harness.h ../src/ui/weather.ui.cpp $(wildcard ../src/ui/*.h) $(wildcard ../src/dsp/*.h) | build
	@$(CXX) $(CXXFLAGS) $< ../src/ui/weather.ui.cpp -o $@

build/render: render.cpp harness.h ../src/ui/weather.ui.cpp $(wildcard ../src/ui/*.h) $(wildcard ../src/dsp/*.h) | build
	@$(CXX) $(CXXFLAGS) $< ../src/ui/weather.ui.cpp -o $@

build:
	@mkdir -p build

clean:
	@rm -rf build

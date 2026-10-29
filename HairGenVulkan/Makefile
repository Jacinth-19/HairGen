# Plain make build (CMake is preferred, see CMakeLists.txt).
CXX      ?= g++
OPT      ?= -O2
CXXFLAGS := -std=c++17 $(OPT) -Ithird_party -Ithird_party/miniz -Wall -Wextra \
            -Wno-unused-parameter -Wno-missing-field-initializers -MMD -MP
LDFLAGS  := -ldl -lpthread
MINIZFLAGS := -std=c11 $(OPT) -Ithird_party/miniz -DMINIZ_NO_ARCHIVE_APIS -w

SRC := src/main.cpp src/assets.cpp src/png.cpp src/renderer.cpp src/vk_loader.cpp src/vk_util.cpp src/web.cpp
OBJ := $(SRC:src/%.cpp=build/%.o)
MINIZ_OBJ := build/miniz.o build/miniz_tdef.o build/miniz_tinfl.o build/miniz_zip.o

.PHONY: all clean shaders
all: hairgen

hairgen: $(OBJ) $(MINIZ_OBJ)
	$(CXX) $(OBJ) $(MINIZ_OBJ) -o $@ $(LDFLAGS)

build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

-include $(OBJ:.o=.d)

build/miniz.o: third_party/miniz/miniz.c | build
	$(CC) $(MINIZFLAGS) -c $< -o $@

build/miniz_tdef.o: third_party/miniz/miniz_tdef.c | build
	$(CC) $(MINIZFLAGS) -c $< -o $@

build/miniz_tinfl.o: third_party/miniz/miniz_tinfl.c | build
	$(CC) $(MINIZFLAGS) -c $< -o $@

build/miniz_zip.o: third_party/miniz/miniz_zip.c | build
	$(CC) $(MINIZFLAGS) -c $< -o $@

build:
	mkdir -p build

shaders:
	./tools/compile_shaders.py

clean:
	rm -rf build hairgen

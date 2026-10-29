cmake_minimum_required(VERSION 3.16)
project(HairGenVulkan LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

add_library(miniz STATIC
  third_party/miniz/miniz.c
  third_party/miniz/miniz_tdef.c
  third_party/miniz/miniz_tinfl.c
  third_party/miniz/miniz_zip.c)
target_compile_definitions(miniz PUBLIC MINIZ_NO_ARCHIVE_APIS MINIZ_NO_STDIO)
target_include_directories(miniz PUBLIC third_party/miniz)
target_compile_options(miniz PRIVATE -w)

add_executable(hairgen
  src/main.cpp
  src/assets.cpp
  src/png.cpp
  src/renderer.cpp
  src/vk_loader.cpp
  src/vk_util.cpp
  src/web.cpp)
target_include_directories(hairgen PRIVATE third_party)
target_link_libraries(hairgen PRIVATE miniz dl pthread)
target_compile_options(hairgen PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)

# Optional: recompile GLSL -> SPIR-V + regenerate src/shaders_generated.hpp.
find_program(GLSLANG_VALIDATOR NAMES glslangValidator glslang HINTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/bin)
if(GLSLANG_VALIDATOR)
  add_custom_target(shaders
    COMMAND ${CMAKE_COMMAND} -E env GLSLANG=${GLSLANG_VALIDATOR}
            python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/compile_shaders.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Recompiling shaders -> src/shaders_generated.hpp")
endif()

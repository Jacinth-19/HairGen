project('hairgen', ['c', 'cpp'],
  version: '1.0.0',
  license: 'MIT',
  meson_version: '>=0.60.0',
  default_options: [
    'cpp_std=c++17',
    'c_std=c11',
    'warning_level=2',
    'buildtype=release',
  ])

# ---------------------------------------------------------------------------
# HairGenVulkan - native Vulkan/C++ port of the "Hair Mesh Rendering" web demo.
#
#   meson setup build
#   ninja -C build
#   ./build/hairgen --out render.png --size 768
#   meson test -C build            # unit + integration tests
#   ninja -C build capture         # stills + animations into media/
#
# The Vulkan loader is resolved at runtime (dlopen), so no Vulkan SDK is needed
# to build - only the headers in third_party/vulkan are used.
# ---------------------------------------------------------------------------

cpp = meson.get_compiler('cpp')
cc = meson.get_compiler('c')

inc = include_directories('third_party', 'src')
dl_dep = cpp.find_library('dl', required: false)
thread_dep = dependency('threads')

# ---- miniz (vendored zlib replacement, used by the PNG writer)
miniz_src = [
  'third_party/miniz/miniz.c',
  'third_party/miniz/miniz_tdef.c',
  'third_party/miniz/miniz_tinfl.c',
  'third_party/miniz/miniz_zip.c',
]
miniz_defs = ['-DMINIZ_NO_ARCHIVE_APIS', '-DMINIZ_NO_STDIO']
miniz_lib = static_library('miniz', miniz_src,
  include_directories: include_directories('third_party/miniz'),
  c_args: miniz_defs,
  install: false)

miniz_dep = declare_dependency(
  link_with: miniz_lib,
  include_directories: include_directories('third_party/miniz'))

# ---- shaders -----------------------------------------------------------------
# The SPIR-V blobs are checked in (src/shaders_generated.hpp) so a plain build
# needs no shader toolchain. Run `ninja -C build shaders` after editing a shader;
# `ninja -C build shaders-check` (also part of a default build) fails loudly if
# the embedded blobs are older than the sources, so they cannot go stale
# unnoticed.
glslang = find_program('glslangValidator',
  dirs: [join_paths(meson.current_source_dir(), 'tools', 'bin')],
  required: false)
python = find_program('python3', required: false)
gen_shaders = []
if glslang.found() and python.found()
  run_target('shaders',
    command: [python, files('tools/compile_shaders.py'), '--glslang', glslang])
  shader_check = custom_target('shaders-check',
    output: 'shaders-check.stamp',
    command: [python, files('tools/compile_shaders.py'), '--check-stale',
              '--stamp', '@OUTPUT@'],
    build_always_stale: true,
    build_by_default: true,
    console: true)
  gen_shaders = [shader_check]
endif

# ---- renderer core (shared by the executable and the tests)
core_src = files(
  'src/assets.cpp',
  'src/png.cpp',
  'src/renderer.cpp',
  'src/vk_loader.cpp',
  'src/vk_util.cpp',
  'src/web.cpp',
)

core_deps = [miniz_dep, thread_dep]
if dl_dep.found()
  core_deps += dl_dep
endif

# vk_util.cpp embeds the generated SPIR-V blob header, so regenerate/rebuild it
# whenever the shaders change (otherwise shader edits silently do nothing).
core_src += [files('src/shaders_generated.hpp'), gen_shaders]

hairgen_core = static_library('hairgen_core', core_src,
  include_directories: inc,
  dependencies: core_deps,
  cpp_args: ['-Wall', '-Wextra', '-Wno-unused-parameter', '-Wno-missing-field-initializers'])

core_link = declare_dependency(
  link_with: hairgen_core,
  include_directories: inc,
  dependencies: core_deps)

executable('hairgen', 'src/main.cpp',
  dependencies: core_link,
  install: true)

# ---- asset bundle ------------------------------------------------------------
# assets/hairgen_assets.bin is generated from the web demo's data files by
# tools/export_assets.js; it is checked in so a build is self contained.
node = find_program('node', required: false)
if node.found()
  run_target('assets',
    command: [node, files('tools/export_assets.js')])
endif

# ---- capture pipeline (pictures / video / GIF) -------------------------------
if python.found()
  run_target('capture',
    command: [python, files('tools/capture.py'), '--binary',
              join_paths(meson.current_build_dir(), 'hairgen')])
endif

subdir('tests')

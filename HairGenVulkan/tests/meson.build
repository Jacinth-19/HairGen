# Unit + integration tests.
#
#   meson test -C build            # everything
#   meson test -C build --suite unit

unit_env = environment()
unit_env.set('HGEN_ASSETS', join_paths(meson.current_source_dir(), '..', 'assets', 'hairgen_assets.bin'))

test_math = executable('test_math', 'test_math.cpp', dependencies: core_link)
test('math', test_math, suite: 'unit')

test_png = executable('test_png', 'test_png.cpp', dependencies: core_link)
test('png', test_png, suite: 'unit')

test_ui = executable('test_ui', 'test_ui.cpp',
  dependencies: core_link,
  )
test('ui', test_ui,
  workdir: join_paths(meson.current_source_dir(), '..'),
  timeout: 120,
  suite: 'unit')

# The frame UBO is declared in GLSL and in C++ and must stay in lockstep; the
# test parses both declarations, so it needs the source root as its cwd.
test_ubo = executable('test_ubo', 'test_ubo.cpp', dependencies: core_link)
test('ubo', test_ubo,
  workdir: join_paths(meson.current_source_dir(), '..'),
  suite: 'unit')

test_assets = executable('test_assets', 'test_assets.cpp', dependencies: core_link)
test('assets', test_assets,
  args: [join_paths(meson.current_source_dir(), '..', 'assets', 'hairgen_assets.bin')],
  suite: 'unit')

# ---- integration: render one small frame headlessly --------------------------
# Skipped automatically when no Vulkan ICD can be loaded (set
# VK_ICD_FILENAMES to e.g. a SwiftShader or lavapipe JSON to enable it).
if get_option('integration_render')
  hairgen_bin = join_paths(meson.current_build_dir(), '..', 'hairgen')
  # Tests inherit the environment, so VK_ICD_FILENAMES / VK_DRIVER_FILES set by
  # the caller still reach the renderer.
  render_env = environment()
  test('render', python,
    args: [join_paths(meson.current_source_dir(), 'integration_render.py'), hairgen_bin],
    suite: 'integration',
    timeout: 180,
    env: render_env)
endif

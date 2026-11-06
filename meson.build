project('draw-triangle',
  'cpp',
  default_options: ['cpp_std=c++23'])

cpp = meson.get_compiler('cpp')
if not cpp.has_header('stb/stb_image.h')
  error('The stb_image.h header is required. Install the stb development package.')
endif

vulkan_dep = dependency('vulkan', version: '>=1.3')
glfw_dep = dependency('glfw3')
glm_dep = dependency('glm')
project_include_dirs = include_directories('.')

glsl_compiler = find_program('glslangValidator')
shaders = ['triangle.vert', 'red_triangle.frag']
shader_targets = []

foreach shader : shaders
  shader_targets += custom_target(
    'generate_' + shader.replace('.', '_'),
    input: shader,
    output: '@BASENAME@.spv',
    command: [glsl_compiler, '-V', '--target-env', 'vulkan1.3', '@INPUT@', '-o', '@OUTPUT@'],
    build_by_default: true,
  )
endforeach

configure_file(input: 'models/viking_room.obj', output: 'viking_room.obj', copy: true)
configure_file(input: 'textures/viking_room.png', output: 'viking_room.png', copy: true)

executable('demo',
  ['main.cc', 'device_buffer.cc'],
  include_directories: project_include_dirs,
  cpp_args: ['-Werror'],
  dependencies: [vulkan_dep, glfw_dep, glm_dep])

buffer_range_tests = executable('buffer_range_tests',
  ['tests/buffer_range_test.cc', 'device_buffer.cc'],
  include_directories: project_include_dirs,
  cpp_args: ['-Werror'],
  dependencies: [vulkan_dep])

test('buffer range checks', buffer_range_tests)

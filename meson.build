project('draw-triangle',
	'cpp',
	default_options : ['cpp_std=c++23', 'build.cpp_std=c++23'])

fs = import('fs')

pkg = import('pkgconfig')
vulkan_dep = dependency('vulkan')
glfw_dep = dependency('glfw3')

glsl_compiler = find_program('glslangValidator')

shaders = [ 'triangle.vert', 'red_triangle.frag' ]
shaders_compiled = []

foreach file : shaders
  shaders_compiled += fs.replace_suffix(file, '.spv')
endforeach

# glsl_compile = custom_target('generate-spv',
# 			     build_by_default : true,
# 			     input : shaders,
# 			     output : shaders_compiled,
# 			     command : [glsl_compiler,
# 					'-V', '@INPUT@', '-o', '@OUTPUT@'])

glsl_generator = generator(glsl_compiler,
			   output    : [ '@BASENAME@.spv' ],
			   arguments : [ '-V', '@INPUT@', '-o', '@OUTPUT@' ])
shaders_processed = glsl_generator.process(shaders)


executable('demo',
           ['main.cc',
            #'utils/utils.cc'
           ],
           cpp_args: ['-Werror'],
           dependencies: [vulkan_dep, glfw_dep])

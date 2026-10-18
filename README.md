# Vulkan Tutorial (C++ RAII)

This project follows the graphics path of the [Vulkan Tutorial](https://vulkan-tutorial.com/) in C++23 with Vulkan-Hpp RAII.
The application renders a rotating, textured Viking Room model. It uses uniform buffers, descriptor sets, depth buffering, mipmaps, and multisampling.

The tutorial's separate compute-shader particle sample is not part of this graphics application.

## Dependencies

- A C++23 compiler, Meson, and Ninja.
- Vulkan 1.3 development files and a Vulkan driver.
- The `VK_LAYER_KHRONOS_validation` layer for debug builds.
- GLFW, GLM, `glslangValidator`, and the `stb/stb_image.h` header.

## Build and run

```sh
meson setup build -Dbuildtype=debug
meson compile -C build
(cd build && ./demo)
```

Run the application from the build directory. Meson places the compiled shaders and copies the model and texture to that directory.

## Asset attribution

The sample model and texture are from the [Vulkan Tutorial resource set](https://vulkan-tutorial.com/Loading_models). The original Viking Room model is by nigelgoh and is licensed under [Creative Commons Attribution 4.0 International](https://creativecommons.org/licenses/by/4.0/).

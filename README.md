# Vulkan Tutorial (C++ RAII)

This project follows the graphics path of the [Vulkan Tutorial](https://vulkan-tutorial.com/) in C++23 with Vulkan-Hpp RAII.
The application renders a rotating, textured Viking Room model. It uses uniform buffers, descriptor sets, depth buffering, mipmaps, and multisampling.

## Development direction

This application is the first client of a small, Vulkan-specific rendering layer. The project will add reusable resource owners and operations in small steps, guided by concrete use cases and tests. It will not add unrelated game systems.

The first refactoring step moves buffer ownership and checked host writes into a reusable type. It keeps GPU copy recording separate from submission and completion.

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
meson test -C build --print-errorlogs
(cd build && ./demo)
```

Run the application from the build directory. Meson places the compiled shaders and copies the model and texture to that directory.

## Asset attribution

The sample model and texture are from the [Vulkan Tutorial resource set](https://vulkan-tutorial.com/Loading_models). The original Viking Room model is by nigelgoh and is licensed under [Creative Commons Attribution 4.0 International](https://creativecommons.org/licenses/by/4.0/).

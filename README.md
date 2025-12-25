# Vulkan Tutorial (C++ RAII)

This project is a reproduction of the famous [Vulkan Tutorial](https://vulkan-tutorial.com/) using the modern C++ Vulkan API (`vulkan.hpp`) and specifically the RAII (Resource Acquisition Is Initialization) wrappers (`vulkan_raii.hpp`).

## Purpose

The original tutorial uses the C API. This project demonstrates how to achieve the same results using safer, more idiomatic C++ with:
- `vk::raii` handles for automatic resource management (no manual `vkDestroy*` calls).
- C++ exceptions for error handling.
- Structs with default initializers (no `memset` or explicit `sType` initialization).

## Building

This project uses `meson` and `ninja`.

```bash
meson setup build
ninja -C build
./build/demo
```

## Dependencies
- Vulkan SDK
- GLFW
- glslangValidator
- Meson & Ninja

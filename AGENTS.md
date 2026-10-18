# Project directions

## Purpose and scope

This file defines the programming style and working rules for this repository.
Read it before planning or changing code. Apply it to new code and to code changed
for the current task. Existing code is not proof that a pattern is correct.

This is a learning project based on the [Vulkan Tutorial](https://vulkan-tutorial.com/).
The goal is a small, understandable Vulkan application written in modern C++.
Keep the Vulkan operations visible. Do not turn the tutorial into a general game
engine or a rendering framework.

These directions do not define what "complete the project" means. Agree with the
user on the required tutorial stage, features, and acceptance checks before that
work starts. A direct user instruction takes priority over these project defaults.

## Current project map

- `main.cc`: application startup, GLFW window, Vulkan resources, and rendering.
- `triangle.vert` and `red_triangle.frag`: GLSL shader sources.
- `models/viking_room.obj` and `textures/viking_room.png`: sample assets. Meson copies them to the build directory.
- `meson.build`: executable, dependencies, and shader compilation.
- `.clang-format`: authoritative C++ formatting configuration.
- `README.md`: project purpose and user-facing build instructions.
- `utils/` and root `utils.hpp`: copied NVIDIA sample utilities. They are not
  part of the active executable source list. Preserve their license notices.

Use the current source and build definitions as evidence. Do not treat backup
files, old assistant conversations, generated files, or local build output as
requirements. Do not enable the copied utilities just to avoid writing a small,
project-specific function. Their API and ownership rules differ from this project.

## Working process for coding agents

1. Read these directions, `README.md`, and the files relevant to the task.
2. Check `git status` and inspect relevant diffs before editing. Preserve existing
   user changes and unrelated untracked files. Do not reset, clean, or overwrite
   them. Do not make commits unless the user asks.
3. For substantial changes, give a short plan: goal, files, required behavior,
   verification steps, and exclusions. Identify uncertainties instead of guessing.
   Wait for the user's approval before implementation, unless the user already
   authorized implementation. A request for directions or a plan is not permission
   to change application code.
4. Make focused changes. Separate required corrections from optional cleanup.
   Do not combine a feature with a repository-wide rename or reformat.
5. Run the applicable checks. Inspect the diff for unintended changes.
6. Report changed files, check results, and remaining limits. Distinguish
   compilation, runtime execution, validation output, and visual inspection.

Do not mark a task complete only because the compiler accepted it. If a check
cannot run, state the reason. Do not invent test results or hide failures.

## Design principles

- Prefer clarity and correctness over short code or clever abstractions.
- Use KISS and YAGNI: choose the simplest design that meets the current requirement.
- Use DRY for repeated behavior, not for unrelated code that looks similar.
- Use SOLID where it makes responsibilities and dependencies clearer. Do not add
  interfaces, inheritance, factories, or dependency injection without a real need.
- Keep functions focused on one operation. Use descriptive helper functions when
  they make a Vulkan step easier to understand or test.
- Use plain structs for data and small resource bundles. Use classes when private
  state and operations must maintain an invariant.
- Keep the current single-executable structure. Split files when a concrete
  responsibility warrants it, not to satisfy an arbitrary file-length limit.
- Keep application state local or owned by the application. Do not add mutable
  globals, singletons, hidden service registries, or generic `utils` collections.
- Add dependencies only when the existing standard library and project libraries
  are not sufficient. Explain the need and obtain approval for substantial additions.

## Language and naming

Use C++23, as configured in Meson. The project uses Vulkan-Hpp, GLFW, and GLM.
Do not change the language standard, build system, or Vulkan binding model as an
incidental part of another task. Do not use a C++23 facility without checking that
both the selected compiler and its standard library support it.

For new project-owned code:

| Item | Convention | Example |
| --- | --- | --- |
| Types | `UpperCamelCase` | `DeviceBuffer`, `QueueFamilyIndices` |
| Functions and methods | `lowerCamelCase` | `createVertexBuffer`, `drawFrame` |
| Variables, parameters, and members | `lowerCamelCase` | `imageIndex`, `physicalDevice` |
| Compile-time configuration constants | `UPPER_SNAKE_CASE` | `MAX_FRAMES_IN_FLIGHT` |
| Namespaces and new file names | `snake_case` | `shader_io`, `device_buffer.cc` |

Preserve external API names. Preserve existing project names unless a rename is
part of the approved task. Do not add Hungarian notation or member-name prefixes.
Use Vulkan terms consistently. Distinguish a frame slot from a swapchain image.
Include units in names when needed, such as `sizeBytes` or `timeoutNanoseconds`.

## C++ programming style

- Use `.cc` for new C++ implementation files and `.hpp` for headers.
- Include the headers that a file uses. Do not depend on transitive includes.
  Make new headers self-contained. Use `#pragma once`, as in the existing headers.
- Do not put `using namespace` directives in headers. Prefer qualified library names.
  Keep implementation-only helpers in an unnamed namespace when practical.
- Initialize values at declaration. Prefer brace initialization and default member
  initializers. Use `nullptr`, not `NULL` or integer zero, for null pointers.
- Use `constexpr` for compile-time values and `const` for values that do not change.
  Prefer `T const &` for read-only references in new declarations.
- Pass small scalar values by value. Pass larger read-only objects by const
  reference. Use `std::span` for non-owning contiguous ranges when it clarifies
  the interface. Use `std::optional` for a genuinely absent value, not a sentinel.
- Use `auto` when the initializer makes the type clear, especially for iterators
  and verbose Vulkan return types. State the type when units or conversions matter.
- Prefer `std::array` for fixed-size collections and `std::vector` for dynamic
  collections. Avoid unnecessary allocations in the frame loop.
- Use `std::size_t` for host container sizes, `vk::DeviceSize` for GPU byte sizes,
  and fixed-width integer types where Vulkan or binary interfaces require them.
- Use named C++ casts, not C-style casts. Check narrowing and overflow before
  conversion. Restrict `reinterpret_cast` to a necessary, explained binary or
  API boundary. Prefer `static_cast` for GLFW's `void *` user pointer.
- Prefer standard algorithms when they make the intent clearer. A simple loop
  is better than a difficult algorithm or ranges expression.
- Do not add raw `new`/`delete`, `malloc`/`free`, or shared ownership by default.
  Use values, RAII, and `std::unique_ptr`. Use `std::shared_ptr` only for a real
  shared-lifetime requirement.
- Add `[[nodiscard]]` to project functions when ignoring their result is an error.
  Do not silence useful warnings with unnecessary casts or `[[maybe_unused]]`.

## Formatting and comments

`.clang-format` is the formatting authority. Do not replace it with an LLVM,
Google, or personal default. It specifies two-space indentation, no tabs, custom
brace wrapping, and a 160-column limit. It also specifies CRLF output. Do not
silently change that setting or normalize unrelated files.

Use braces for control statements, including one-line bodies. Do not assume the
formatter adds them. Format changed code without reformatting unrelated code.
If the installed formatter rejects configuration options, report the version
problem instead of deleting options to make the command pass.

Use short, direct English in comments and documentation. Explain the reason,
invariant, unit, ownership rule, or Vulkan constraint. Do not narrate obvious
statements. Remove stale comments and commented-out implementations in the code
you change. Avoid assistant-history comments such as "Add this function".
Keep TODO comments specific: state the missing behavior and why it remains.

## Ownership and resource lifetime

Use `vk::raii` for owned Vulkan resources. Borrowed handles such as swapchain
images and handles passed to command recording may use `vk::` types. Borrowing
must not transfer ownership. Do not add manual `vkDestroy*` calls for RAII-owned
resources.

At required C API boundaries, such as `glfwCreateWindowSurface`, check the result
and adopt a successfully created handle into its RAII owner immediately. Keep
GLFW initialization and window destruction under RAII as well.

Lifetime rules are part of the design, not just cleanup details:

- Declare dependencies before their dependents. C++ destroys members in reverse
  declaration order. Keep the Vulkan instance and device alive until their child
  resources are destroyed. Keep GLFW alive until its window is destroyed.
- Destroy buffers and images before freeing their bound memory. In a bundle,
  declare memory before the buffer or image so reverse destruction is correct.
- Review replacement and move assignment separately from final destruction.
  Correct member order alone does not make resource replacement safe.
- Keep resources alive until all submitted GPU work that uses them has finished.
  RAII controls CPU ownership; it does not wait for GPU completion.
- Make resource owners non-copyable when copying could duplicate ownership or
  termination. Permit moves only when they preserve all lifetime invariants.
- Keep destructors non-throwing. Handle GPU waits and failures through an explicit
  shutdown path. Initialization failure must also release acquired resources safely.

## Vulkan API style

- Prefer Vulkan-Hpp types, scoped enums, flags, and RAII member functions over the
  raw C API. Use raw types only when an external signature requires them.
- Preserve `VULKAN_HPP_NO_CONSTRUCTORS`. Use value-initialized create-info structs
  with designated initializers in declaration order. Vulkan-Hpp supplies default
  `sType` values; do not use `memset` or add redundant manual initialization.
- Keep Vulkan-Hpp configuration macros consistent across translation units and
  before Vulkan includes. Do not mix incompatible macro settings through helpers.
- Prefer array/range overloads or setters when they remove error-prone pointer/count
  pairs. With explicit pairs, keep the count correct and the storage alive for the
  call. Check container-size conversions to Vulkan's count types.
- Check API version, extension, layer, queue, format, and feature support before
  use. Enable only what the application needs. Do not silently change the current
  Vulkan 1.3 request or add a GPU-specific requirement.
- Use the installed Vulkan-Hpp headers and the Vulkan specification to resolve
  overload, result, and lifetime questions. Tutorial C code is not a drop-in
  implementation for RAII. Do not guess wrapper signatures.

## Synchronization, memory, and shaders

For each GPU operation, make resource use and synchronization explicit. Know which
queue performs the work, which stage and access masks apply, and when reuse is safe.

- Do not reset or rerecord an in-use command buffer. Do not overwrite data that
  an in-flight frame still reads.
- Reset a frame fence only when acquisition permits a submission and the frame
  will proceed to submit. An early return must not leave the next frame waiting
  on an unsignaled fence that has no submission.
- Keep frame-indexed and image-indexed resources distinct. Do not reuse a binary
  presentation semaphore merely because the rendering fence has signaled; prove
  that the previous presentation wait has consumed it.
- Handle out-of-date and suboptimal swapchains through the installed wrapper's
  actual return or exception behavior. Do not suppress unexpected Vulkan failures.
- On swapchain recreation, query current capabilities and rebuild all affected
  resources. Account for changed extent, format, and image count. Handle a zero-size
  framebuffer without busy-waiting, and allow close requests while minimized.
- Keep `waitIdle()` out of steady-state per-frame rendering. It can be appropriate
  for simple initialization transfers, shutdown, or a small recreation path.
  Do not remove a wait without replacing its lifetime and synchronization guarantee.
- Select memory using the reported `memoryTypeCount` and required property flags.
  Check byte ranges and copy sizes. Mapping requires host-visible memory. Flush
  non-coherent writes and invalidate non-coherent reads when required, with correct
  alignment. Do not assume all mapped memory is host-coherent.
- Keep shader bindings, descriptor types, stage flags, vertex formats, and locations
  consistent with the C++ pipeline. A declared shader resource requires allocated,
  written, and bound descriptors before drawing.
- Make uniform layout explicit. Verify GLSL layout rules, C++ alignment, offsets,
  and size. Do not assume any GLM type configuration has the required binary layout.
  Use compile-time layout checks where practical.
- Keep GLM depth-range and matrix conventions consistent with Vulkan and the
  shaders. Do not apply a coordinate correction twice.
- Load SPIR-V as properly aligned 32-bit words. Check file reads, size, and word
  boundaries before shader-module creation. Do not rely on an unchecked cast from
  `std::vector<char>` to `uint32_t *`.

## Errors and diagnostics

Use exceptions for unrecoverable initialization, file, and Vulkan failures, as
specified in `README.md`. Use explicit result handling for expected recoverable
conditions. Use assertions for programmer invariants, not hardware capability,
file, or window-system failures.

Keep application construction and execution inside the top-level exception
boundary. Give errors enough context to identify the operation and resource.
Never allow a C++ exception to cross a GLFW or Vulkan callback boundary. Use the
correct callback ABI and a safe reporting path.

Keep Vulkan validation enabled for development builds. If a required layer is
missing, report it clearly; do not silently disable validation to pass a check.
Do not hide warnings, weaken `-Werror`, or catch and ignore exceptions to obtain a
successful build or run. Keep output useful; avoid unconditional per-frame logs.

## Build and verification

Meson is the source of truth for dependencies and build targets. Use Ninja through
Meson or directly. Shader sources must compile through Meson custom targets; do
not edit generated `.spv` files or depend on a stale binary from another build.
Do not add a parallel CMake build or hard-coded machine-specific include paths.

For a new development build directory:

```sh
meson setup build -Dbuildtype=debug
meson compile -C build
```

If `build/` already exists, inspect its configuration before reuse. Use a separate
build directory if its paths or toolchain are stale. The dependency set includes
Vulkan, GLFW, GLM, and `glslangValidator`, plus Meson, Ninja, and a suitable C++
compiler and standard library.

The current program reads shader files relative to the working directory. Run it
from the build directory so it finds the generated shaders:

```sh
(cd build && ./demo)
```

Until there is a task-approved asset-path change, do not claim that running
`./build/demo` from the repository root is equivalent.

Checks for relevant code changes:

- Compile the executable and both shaders. Keep warning-as-error behavior.
- Run applicable automated tests with `meson test -C build --print-errorlogs` when
  tests exist. A run with no tests is not test coverage.
- For rendering and lifetime changes, run a development build with validation.
  Check startup, rendering, repeated resize, minimize/restore, and normal close.
  Inspect validation output through shutdown, not just the first frame.
- For synchronization changes, test more than one frame and exercise recreation.
  For shader changes, verify the expected visual result as well as compilation.
- Test pure selection or layout logic without a GPU when practical. Add regression
  tests for corrected behavior without introducing a large test framework by default.
- If a display, driver, GPU, or validation layer is unavailable, report exactly
  which runtime checks remain unverified. On Wayland, a window can remain invisible
  until the first frame is presented; window creation alone is not rendering proof.

For this small project, prefer reproducible checks over new infrastructure.
Documentation-only changes need a content and diff review, not an application run.
Keep `README.md` accurate when build commands, dependencies, or behavior change.
Do not commit generated build products, local environment dumps, editor backups,
or assistant logs as part of a coding task.

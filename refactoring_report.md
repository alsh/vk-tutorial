# Refactoring report: from tutorial application to reusable renderer

## 1. Recommendation

Develop a **small, Vulkan-specific rendering library inside this repository**, with the Viking Room application as its first client. Do not start by designing a complete game engine.

The first useful result is not a shorter `main.cc`. It is a set of objects with clear ownership and contracts:

- A buffer owns its allocation and controls host access.
- An upload operation controls staging, submission, and completion.
- A mesh owns geometry, not a camera or a window.
- A renderer draws supplied objects, rather than choosing the scene and animation.
- Presentation controls swapchain images and recreation.
- The application controls input, time, camera, and scene changes.

These responsibilities can remain in one executable while their boundaries are tested. A separate library target becomes useful when a second client or a test actually needs it.

**Recommended next implementation:** establish a small regression baseline, then extract a buffer class with checked host writes. Keep GPU uploads synchronous initially. Extract CPU asset loading as a separate, low-risk change. Use these results to guide the larger application split.

### Scope change

`AGENTS.md` currently says not to develop a rendering framework. The user's current request changes that goal. The next approved implementation should update that scope, while retaining C++23, Meson, Vulkan-Hpp RAII, explicit synchronization, and the existing naming and formatting rules.

This report proposes work. It does not authorize or implement the proposed refactoring.

## 2. Review basis

Reviewed revision: `b01aa0a`, including `main.cc`, both shaders, `meson.build`, `README.md`, and `AGENTS.md`.

Current checks performed for this review:

- `meson compile -C build`: passed; Ninja reported no work to do.
- `meson test -C build --print-errorlogs`: no tests defined.
- The build configuration points to this repository, not its former sibling source directory.

No application was launched for this review. No new runtime, performance, or fault-injection result is claimed. The copied NVIDIA utilities, backups, environment dumps, and local binaries are not design inputs.

Source locations below refer to the reviewed revision. Function names remain useful after line numbers change.

This is an architectural review, not a complete Vulkan correctness audit. Recommendations distinguish current design limits from risks that arise when resources become reusable.

## 3. What the current design provides

The application has a sound starting point for learning and refactoring:

- Vulkan handles already use `vk::raii` ownership.
- `DeviceBuffer` and `DeviceImage` already group resources with their memory.
- Shader matrix offsets and sizes have compile-time checks.
- Host writes support a non-coherent memory fallback.
- Frame-slot resources and image-indexed presentation semaphores are distinguished.
- Swapchain recreation rebuilds the affected render resources.
- Initialization transfers, normal shutdown, and failure paths account for GPU completion.

Preserve these properties. A wrapper that makes these guarantees less clear is not an improvement.

### Main structural problem

`HelloTriangleApplication` occupies approximately 1,800 lines of the 2,030-line `main.cc`. More importantly, it owns almost every state transition in the application.

| Responsibility | Current evidence | Consequence |
| --- | --- | --- |
| Startup and dependency ordering | `initVulkan()`, [main.cc:274](main.cc#L274) | Initialization mixes device setup, file loading, uploads, descriptors, and presentation. |
| Device capabilities and rendering policy | `isDeviceSuitable()`, `chooseSampleCount()`, [main.cc:501](main.cc#L501), [main.cc:586](main.cc#L586) | The device layer assumes this texture format, mipmap method, and depth-rendering path. |
| Buffer ownership and host writes | `DeviceBuffer`, [main.cc:86](main.cc#L86) | Public handles, metadata, and caller-supplied device references leave invariants outside the object. |
| Allocation and transfers | `createBuffer()`, `withOneTimeCommandBuffer()`, `copyBuffer()`, [main.cc:807](main.cc#L807), [main.cc:879](main.cc#L879), [main.cc:909](main.cc#L909) | Callers must coordinate allocation, staging lifetime, submission, and waiting. |
| CPU model parsing | `loadModel()` and index helpers, [main.cc:932](main.cc#L932) | File parsing requires access to the application object even though it does not require Vulkan. |
| CPU image decoding and GPU texture creation | `createTextureImage()`, [main.cc:1236](main.cc#L1236) | File format, image allocation, upload, and mip generation are one operation. |
| Shader and material interface | Descriptor functions, [main.cc:1341](main.cc#L1341), [main.cc:1386](main.cc#L1386) | One texture and one matrix block are built into the application. |
| Render target and pipeline configuration | `createRenderPass()` through `createFramebuffers()`, [main.cc:1469](main.cc#L1469), [main.cc:1687](main.cc#L1687) | Pipeline creation depends directly on application members and swapchain state. |
| Scene policy and GPU data update | `updateUniformBuffer()`, [main.cc:1808](main.cc#L1808) | Time, rotation, camera, projection, and mapped-memory writes are coupled. |
| Frame lifecycle and presentation | `drawFrame()` and `recreateSwapChain()`, [main.cc:1861](main.cc#L1861), [main.cc:1835](main.cc#L1835) | Scene updates and draw recording are coupled to acquisition, fences, and recreation. |

Moving each method group into a file without changing these dependencies would only distribute the mega-class across files.

## 4. Define “generic” through examples

Use **reusable within a Vulkan renderer** as the first meaning of generic. Keep Vulkan types visible where they communicate real constraints. Do not design a multi-API rendering interface yet.

Prove increasing reuse with small examples:

1. The existing mesh and uniform buffers use the same buffer owner.
2. A generated triangle and a file-loaded mesh use the same mesh upload path.
3. Two instances of one mesh have different transforms without duplicated geometry.
4. Two textures can be selected independently of geometry.
5. A second small scene uses the renderer without editing device, allocation, or presentation code.
6. An offscreen target reuses resource and draw code without requiring the window path.

Each example exposes a concrete missing abstraction. If an interface is not needed by the next example, defer it.

The first engine-level vocabulary should be small: `Buffer`, `Image`, `Mesh`, `Texture`, `Material`, `CameraData`, and `DrawItem`. This is not a request to implement all of them in the next change.

## 5. Proposed responsibility boundaries

The following is a destination, not an immediate list of files to create.

| Unit | Owns or controls | Must not absorb |
| --- | --- | --- |
| `Application` | Main loop, scene, camera/input, animation time, startup composition | Buffer internals, descriptor writes, acquire/submit/present details |
| `Window` | GLFW window, callback state, framebuffer size, close state | Renderer pointer, meshes, GPU allocation |
| `VulkanInstance` | Loader context, instance, validation messenger | Assets, frame state |
| Surface bridge | GLFW-to-Vulkan surface creation and ownership | Device allocation or scene policy |
| `Device` | Physical/logical device, queues, enabled capabilities, memory properties | Window callbacks, file paths, materials, all GPU resources |
| `Buffer` / `Image` | Resource ownership, allocation metadata, local validity checks | Scene policy, hidden frame waits or unrelated queue submission |
| `UploadContext` | Staging resources, transfer recording/submission, explicit completion policy | Camera, presentation, model-file parsing |
| CPU asset functions | `MeshData`, `ImageData`, checked file decoding | Vulkan instance, device, window |
| `Mesh` / `Texture` | GPU resources and draw/upload metadata | Global resource registry, animation |
| Presentation resources | Swapchain and image-dependent resource generation | Model loading, scene updates |
| Frame lifecycle | Frame slots, command-buffer reuse, acquire/submit/present protocol | Material semantics, camera calculations |
| `ForwardRenderer` | Current textured draw path, pipelines, descriptor use, draw recording | Choosing the application's scene or using wall-clock time internally |

Do not create a base class or interface for each row. Use free functions for transformations and file loading. Use classes where private state must preserve an invariant. Use small structs for descriptions and borrowed inputs.

### Ownership before interfaces

A useful initial application ownership order is:

```text
Application
  GLFW lifetime
  Window
  VulkanInstance
  Surface
  Device
  Renderer
    uploaded scene resources
    presentation resources
    command pools and frame resources
```

Dependencies are constructed before dependents and destroyed after them. The renderer borrows the device; resources do not keep it alive through shared ownership. Device selection uses the surface, but resource allocation does not need a window.

During the first extraction, keep the composed owners at stable addresses and review their move support explicitly. Do not let a returned resource retain a reference to a temporary device owner.

Within renderer bundles, declaration order still matters: command buffers depend on pools; framebuffers depend on attachment views and render-pass compatibility; buffers and images depend on allocations and the device. A new class boundary must not erase these dependencies.

For now, resource destruction and replacement require completed GPU use. Later asynchronous resource removal needs a separate retirement mechanism. A non-throwing destructor must not secretly call `waitIdle()`.

## 6. Buffer: the first reusable resource object

The proposed buffer is a good first abstraction because the project already has three clients: vertex data, index data, and uniform data. Texture staging provides another client.

Keep the existing `DeviceBuffer` name during the first extraction. `Buffer` below describes the intended role; it does not require a separate repository-wide rename.

### 6.1 Present limitations

`DeviceBuffer::upload()` already hides map, copy, flush, and unmap. Preserve that convenience. Its current limits become important when the type becomes public:

- Ownership handles and metadata are public.
- Creation is an application method rather than part of a reusable resource API.
- `upload()` accepts a separate device for flushing. The API permits a caller to pass the wrong device.
- It writes only from offset zero and does not explicitly reject a non-host-visible allocation.
- `upload` names both a host-memory write and, elsewhere, a staging transfer. Those are different operations.
- `copyBuffer()` copies the entire source at zero offsets. Usage flags and copy ranges are not part of its checked interface.
- Allocation size and logical buffer size are not separate stored concepts.

The current call sites use compatible host-visible buffers and devices. These are limitations of a reusable API, not evidence that all current uploads fail.

### 6.2 First contract

Introduce a move-only `Buffer` with private allocation and buffer handles. It should retain:

- Logical byte size and allocation byte size.
- Actual memory properties, not only requested properties.
- Usage flags.
- A non-owning relationship to the correct device.
- Mapping state only if the selected mapping model requires it.

Expose a borrowed buffer handle for Vulkan binding and descriptors. Do not expose the owned memory handle for arbitrary replacement.

The first public host operation should be a checked byte write, for example `writeBytes(offsetBytes, bytes)`. It should:

1. Reject writes outside the logical buffer range, with overflow-safe checks.
2. Check host visibility before mapping.
3. Hide the mapped pointer and ensure unmapping on errors. Make host writes non-`const` in the project API, even if a wrapper permits otherwise.
4. Flush non-coherent writes correctly.
5. Define zero-length and moved-from behavior.

A byte-oriented interface can accept `std::span<std::byte const>`. Typed convenience helpers can use `std::as_bytes` for trivially copyable data. That does not prove that an arbitrary C++ struct matches a shader layout; keep the uniform layout checks.

For the first version, retain dedicated allocations and the current whole-allocation mapping/flush approach. This is simpler than adding suballocation and atom-aligned partial flushes at the same time.

### 6.3 Mapping and pointer lifetime

Do not expose an unrestricted `void * data()` first. Most current clients only need checked writes.

Research persistent mapping after the basic owner works. The uniform-buffer write is a suitable bounded example. Ask whether persistent mapping removes useful repetition or measurable cost; do not assume a performance gain.

If a later client needs direct mapped access, use a scoped mapping object with a borrowed byte view. Define:

- Whether a second mapping is permitted.
- Whether moves or replacement are forbidden while a view exists.
- When writes are flushed and when GPU access may start.
- When the view becomes invalid.

A returned span does not extend memory lifetime or prevent the caller from retaining it incorrectly. Document that constraint. If flush can fail, use an explicit completion operation; do not throw from a mapping destructor.

### 6.4 Copies: hide mechanics, not completion

Separate three operations:

```text
Buffer.writeBytes(...)                 CPU writes to host-visible memory
recordBufferCopy(commandBuffer, ...)   records GPU work; does not submit it
UploadContext.uploadAndWait(...)       owns staging and blocks until completion
```

These are API sketches, not compiled declarations.

A buffer may provide a `recordCopyTo()` convenience method instead of the free function. In either form, the command recorder and byte ranges must be explicit. A copy method should check both resources, usage flags, ranges, and applicable overlap restrictions. It should not silently allocate a command pool, submit work, and wait for the whole queue.

`UploadContext` can provide the convenient combined operation the user wants: create/fill staging, record the copy, submit, wait, then release staging. The blocking behavior belongs in its name and contract.

Initially restrict this combined upload operation to initialization or another explicitly safe update point. Waiting for the new copy does not by itself make overwriting a buffer used by older submissions safe.

Initially retain the graphics queue and synchronous uploads. Do not add a dedicated transfer queue yet. When batching is introduced, add explicit transfer-to-consumer dependencies for the actual vertex, index, or shader use. Mapping and unmapping are not GPU synchronization.

### 6.5 Moves, replacement, and completion

Review move construction separately from move assignment. In the installed Vulkan-Hpp headers, `vk::raii::Buffer` and `DeviceMemory` move assignment swap handles rather than immediately destroy the destination. Thus, an immediate early-free defect in the current aggregate assignment is not established. However, default aggregate moves do not define a useful empty source or keep scalar metadata consistent with swapped resources. Adding cached mapped pointers would add another lifetime constraint. These are reasons to specify wrapper moves explicitly, not to assume a current rendering failure.

The first version can permit move construction and forbid move assignment. Alternatively, implement replacement with an explicit completed-use precondition and dependency-safe release order. Do not add implicit buffer resizing yet.

Changing ownership does not prove that the GPU has finished. Reuse, replacement, destruction, and mapped writes all need an external completion guarantee. Buffer ownership and frame scheduling must remain separate concepts.

## 7. Split the application in small steps

### A. Extract CPU work

Move the OBJ parser into `loadObj(...) -> MeshData`, with no Vulkan or window dependency. Give it a path or stream rather than a global sample filename. Separate decoded pixels into `ImageData` before GPU texture creation.

The current OBJ reader supports the sample's needs, not every OBJ feature. Test and state the supported subset, including polygon triangulation and material handling. Do not advertise a full asset importer because the Viking Room loads.

Move SPIR-V file reading into a small shader I/O function. Keep `STB_IMAGE_IMPLEMENTATION` in exactly one implementation file.

This creates genuine CPU-only test targets without mocking a Vulkan device.

### B. Extract device/resource/upload code

Start with narrow device access and buffer creation. Do not move every renderer member into a new `Context` class. A device wrapper should answer device questions and create device resources, not provide access to the whole application.

Keep enabled capabilities distinct from available capabilities. Later, replace the current built-in render requirements and maximum-MSAA policy with explicit renderer settings and a supported fallback. That is a separate behavior change, not part of the first file extraction.

### C. Separate scene state from GPU updates

Split `updateUniformBuffer()` into application-side calculations and renderer-side upload. The renderer receives matrices or camera/object data, not a clock.

First preserve the current shader interface. Then test two differently transformed instances. That example will force a decision about per-object shader data: a model-matrix push constant or a dynamic uniform-buffer range are both candidates. Research one; do not build a general material system first.

### D. Extract presentation and frame state

Keep these two indexing domains distinct:

- **Frame slot:** command buffer, acquire semaphore, submission fence, and matching frame-local renderer data.
- **Swapchain image:** image view, per-image depth/MSAA attachments, framebuffer, and presentation-wait semaphore.

The current parallel vectors express these domains implicitly. Small bundles can make them explicit. Generic frame scheduling should not depend on the demo's `UniformBufferObject`; the renderer can maintain its own matching frame data.

Start by moving the current complete recreation behavior into one presentation-resource unit. It is acceptable to rebuild pipeline/render-pass state as today. Later, give the renderer a target description containing relevant formats, sample count, and extent, so pipeline recreation follows actual compatibility changes.

Do not begin by exposing an unrestricted `beginFrame()/endFrame()` pair. Define acquisition failure, skip, and recording-error behavior first. A caller that begins but never submits must not leave a reset fence without a submission. Preserve the existing out-of-date handling and close-while-minimized path.

### E. Make the renderer a client-facing unit

After resources and lifecycle have stable owners, extract the present draw path as `ForwardRenderer`. A first interface can receive a mesh, texture/material binding, camera data, and object transform.

The application should be able to change the scene without editing allocation, descriptor implementation, or swapchain code. This is the meaningful completion criterion for splitting the mega-class.

## 8. Incremental work plan

Keep each row independently reviewable. Use one main design question per change. Prefer a compiling commit with unchanged rendering before starting the next row.

| Step | Bounded work | Research question | Acceptance / stop condition |
| --- | --- | --- | --- |
| 0. Baseline and scope | Update the approved project scope; establish a small repeatable check procedure | What behavior must remain unchanged? | Debug build and shaders pass; bounded startup/render/resize/close check is recorded; unavailable minimize/restore checks are explicit. |
| 1. Buffer owner | Centralize Vulkan/GLM configuration for multiple translation units; extract buffer creation, memory selection, and checked writes; migrate one client first | Which invariants belong in `Buffer` rather than its caller? | Mesh and uniform clients use the same owner; invalid ranges are rejected; move/replacement policy is explicit; rendering is unchanged. |
| 2. CPU assets | Extract `MeshData`, OBJ parsing, image decoding, and shader reading in separate small changes | What is data loading, and what is GPU upload? | Parser and shader-file error tests run without a GPU; the sample uses parameterized asset paths. No new asset format is required. |
| 3. Upload context | Extract one-time submission and staging operations without changing their synchronous policy | Who owns staging until completion? | Vertex/index uploads use one checked path; GPU upload/readback verifies bytes; failure handling keeps submitted resources alive. |
| 4. Thin application | Extract window/device composition and separate camera/animation calculation from buffer writes | Can scene policy change without touching Vulkan setup? | The app passes render inputs; callbacks no longer require a pointer to the demo mega-class; shutdown order is documented and checked. |
| 5. Presentation and frames | Group current image-dependent resources, then move the frame protocol | Who owns each transition during skip, resize, and close? | Repeated resize and normal close pass validation; minimize/restore and close while minimized are tested where supported; no frame/image index substitution occurs. |
| 6. Reusable draw path | Extract `ForwardRenderer`, `Mesh`, and the smallest material binding needed | Can a second scene reuse the renderer? | Two transformed instances share geometry; two textures work; the sample is one renderer client. No registry or ECS is required. |

Steps 1 and 2 can be exchanged if a low-risk CPU-only extraction is preferred first. Upload code depends on the buffer contract. Presentation extraction should follow stable device ownership. Do not introduce asynchronous uploads merely because the synchronous code moved to a class.

### First implementation scope

For the next coding task, take **Step 0 and the first part of Step 1 only**:

- Establish the baseline and common header/macro configuration.
- Extract a private buffer owner and memory-type selection.
- Add checked byte writes and migrate the staging buffer path.
- Add range-selection tests and a bounded GPU smoke check when available.
- Preserve the current mapping frequency, queue, allocation model, and wait policy.

Exclude persistent mapping, readback APIs for clients, asynchronous submission, buffer resizing, allocator replacement, and a renderer-wide reorganization. This is a useful complete step, not a partial engine rewrite.

A plausible first patch touches `device_buffer.hpp`, `device_buffer.cc`, a shared `vulkan_config.hpp`, `main.cc`, `meson.build`, and a small buffer-rules test source. Creation can initially borrow the existing Vulkan-Hpp device directly; a new application-wide `Device` class is not a prerequisite. Keep pure range and memory-selection logic testable without constructing a buffer or opening a window.

## 9. Research experiments after the first extraction

Use small experiments rather than adding every feature to the main renderer.

| Experiment | Evidence to collect | Decision it supports |
| --- | --- | --- |
| Nonzero-offset buffer write and readback | Correct bytes before, inside, and after the written range; completion and invalidate handling | Whether the buffer range contract is correct |
| Persistent mapping for frame-local uniforms | Pointer lifetime, moved-owner restrictions, explicit flush behavior, measured cost if relevant | Whether to add persistent mapping or keep write-only access |
| Batch vertex/index/texture startup uploads | Submission and wait counts, retained staging lifetime, validation output | Whether an upload batch is worth adding |
| Two objects with independent transforms | Correct images across several in-flight frames; shader layout/offset checks | Push constants versus dynamic uniform ranges |
| Remove a mesh after submission | Completion tracking and delayed resource release | When a resource-retirement queue becomes necessary |
| Offscreen render target | Reused draw path with no presentation dependency | Whether presentation and rendering are separated adequately |

A single machine may provide no non-coherent host-visible memory. Test range/alignment calculations as CPU logic and state that hardware limitation; do not claim an unexercised path passed.

## 10. Further goals and their triggers

| Further goal | Start when | Do not add yet |
| --- | --- | --- |
| Reusable image/view/sampler objects | A second texture or render target needs different properties | One universal image-layout state machine |
| Small material and draw descriptions | Two materials or shader variants exist | Reflection-driven universal materials or bindless descriptors |
| Pipeline reuse/cache | Multiple draws share pipeline state or rebuild cost is observed | A large fluent pipeline builder |
| Memory suballocation, possibly VMA | Many allocations, memory pressure, or allocation limits justify it | An unmeasured allocator rewrite or unapproved dependency |
| Asynchronous uploads and retirement | Runtime resource loading/removal is a real requirement | Dedicated transfer queues and multithreaded recording together |
| Offscreen and multiple passes | A concrete post-process or shadow pass is required | A render graph before pass dependencies are understood |
| Resource identifiers and asset cache | Resources are shared, unloaded, or reloaded dynamically | Global singleton managers and shared ownership everywhere |
| Backend abstraction | A second backend is an actual project requirement | A speculative Vulkan/OpenGL/Direct3D interface |

Lighting, a camera controller, profiling, and asset-format support are possible feature tracks. Select one after the structural baseline is stable. Keep feature work separate from behavior-preserving extraction so regressions have a small search area.

An ECS, editor, scripting system, plugin framework, scene graph, and general task scheduler are outside this renderer refactoring plan.

## 11. Verification and working rules

For each extraction:

1. Keep the sample available as a regression client.
2. Compile all changed translation units and both shaders with warnings treated as errors.
3. Test pure logic without a GPU: memory-type selection, byte ranges, supported OBJ cases, invalid shader files, and layout assumptions.
4. Use a small GPU integration test for mapping, copies, readback, and resource replacement when available. Do not create a fake Vulkan framework merely to unit-test wrappers.
5. Run rendering validation through shutdown for lifetime or synchronization changes. A clean first frame is not enough.
6. Use finite runtime limits, including for tests that wait while minimized. A frame-count limit alone does not bound a test that cannot present.
7. Check the visual result for shader, transform, or render-target changes.
8. Make a local commit per coherent extraction or experiment only when requested. Do not combine a file move with a new synchronization strategy and unrelated formatting.

When files are split, apply Vulkan-Hpp configuration consistently before Vulkan includes in every translation unit. Apply GLM configuration consistently before GLM includes. Keep shader resource interfaces and CPU binary layouts explicit. These are build-interface requirements, not cosmetic cleanup.

## 12. Decisions to retain for now

- One executable and one Vulkan graphics backend.
- Explicit application composition instead of global engine state.
- Vulkan-Hpp RAII as the handle owner; new classes add invariants, not another set of destroy calls.
- Dedicated allocations and synchronous startup uploads until a focused experiment justifies change.
- Explicit GPU completion outside ordinary resource destructors.
- A specific textured forward-rendering path before a generic rendering framework.
- Small, complete research steps with visible acceptance checks.

**Success is not the number of classes or files. Success is that a new scene can use a small renderer interface, while resource ownership and GPU synchronization remain understandable and testable.**

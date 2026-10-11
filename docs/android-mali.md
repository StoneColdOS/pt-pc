# Vulkan 1.1 compat mode (Mali-G57), work in progress

Branch `mali-g57-vk11`. Target: a Redmi Note 12S (Helio G96, Mali-G57 MC2), whose driver is Vulkan 1.1 and offers none of
`VK_KHR_dynamic_rendering`, `VK_KHR_synchronization2`, `VK_KHR_timeline_semaphore` and `VK_EXT_descriptor_indexing`.

The renderer is written for Vulkan 1.3. Compat mode (`Context::compat_vk11`, set when the chosen GPU reports less than 1.3)
creates the device with core 1.0/1.1 features only. It is Android only; PC and the Adreno path are unchanged.

## Stages

1. **Device creation** (`vk_context.cpp`): accept a 1.1 GPU, no 1.2/1.3 feature structs, `vkQueueSubmit` for one-time
   submits, and log the driver's extensions and features (`pt.log`, lines starting `vulkan: extensions:` and `vulkan: features:`).
2. Render passes and framebuffers in place of dynamic rendering (every pass).
3. Classic synchronization (barriers, fences, binary semaphores) in place of sync2 and timeline semaphores.
4. Textures without the bindless array (descriptor sets per draw or per small batch) and the shaders that index it.
5. BC textures converted or decoded on load (the Mali has no BC; `Context::texture_bc` says which).
6. Shaders compiled for SPIR-V 1.3 / `vulkan1.1`, and performance work (render scale).

## Building a test APK

Pushes to this branch do not build (the workflow only watches `android-port`, so `android-latest` is not overwritten).
Run it by hand: Actions > Android > Run workflow, branch `mali-g57-vk11`, then download the `PT-Android` artifact.

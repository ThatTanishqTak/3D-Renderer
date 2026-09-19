# 3D Renderer — Trident

A **C++20/Vulkan rendering project** consisting of the `Trident` engine and the `Trident-Forge` editor.

The project explores the systems surrounding an interactive renderer: asset loading, scene editing, animation, frame capture and experimental machine-learning integration.

## Features

* Vulkan mesh rendering with physically based lighting.
* Directional and point lights.
* Material colour, metallic and roughness parameters.
* Skybox and text rendering.
* Model importing through Assimp.
* Entity/component scene organisation.
* Skeletal animation playback, pose evaluation and state-machine infrastructure.
* Dockable ImGui editor panels and ImGuizmo transform controls.
* Separate scene and game viewports.
* Scene saving and loading.
* Performance capture and FFmpeg-based viewport recording.
* Dataset capture and experimental ONNX Runtime frame-processing integration.

[View editor screenshots](https://github.com/ThatTanishqTak/3D-Renderer/tree/main/Screenshots).

## Architecture

| Directory        | Responsibility                                                               |
| ---------------- | ---------------------------------------------------------------------------- |
| `Trident/`       | Rendering engine, scene systems, animation, asset loading and AI integration |
| `Trident-Forge/` | Editor application, panels, shaders and assets                               |
| `cmake/`         | Dependency setup and compiler tooling                                        |
| `Scripts/`       | Project utilities                                                            |
| `Dataset/`       | Captured project data                                                        |
| `Screenshots/`   | Editor screenshots                                                           |

`Trident` builds as a static library. `Trident-Forge` is the editor executable.

## Requirements

The current dependency setup primarily targets **Windows x64 with MSVC**.

* Visual Studio 2022 with C++ development tools.
* CMake 3.20 or newer.
* Vulkan SDK with `VULKAN_SDK` configured.
* `glslangValidator` available for shader compilation.
* Git and Git LFS.
* Network access during dependency configuration.

The build combines Git submodules, CMake-fetched dependencies and bundled SDK files. GLFW and ImGui are fetched during configuration; FFmpeg may also be downloaded if its development files are missing.

## Build

```powershell
git clone --recurse-submodules https://github.com/ThatTanishqTak/3D-Renderer.git
cd 3D-Renderer
git lfs pull

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target Trident-Forge
```

Run the editor from its output directory:

```powershell
cd build/Trident-Forge/Release
.\Trident-Forge.exe
```

Keep `Assets` beside the executable. Required ONNX Runtime, FFmpeg and KTX runtime libraries must also be available beside the executable or through `PATH`.

If configuration reports a Git LFS pointer instead of a library, run `git lfs pull` before retrying.

## Using the Editor

* Use the scene hierarchy and inspector to inspect entities and components.
* Use viewport gizmos to adjust transforms.
* Browse assets through the content browser.
* Open and save scenes through the File menu.
* Use the toolbar to control scene playback.
* Configure dataset capture or clip export through the toolbar controls.

Clip export requires a runtime camera and a valid game viewport.

## Experimental ONNX Integration

The engine includes model discovery, inference, diagnostics and frame-dataset recording.

**A trained `.onnx` model is not included in the repository.** The default discovery path looks for `Assets/AI/frame_generator.onnx` under its search roots. A supplied model must match the input/output conventions expected by the implementation.

This integration should be treated as experimental. Its presence does not establish a particular frame-generation quality or performance result.

## Current Limitations

* The build depends on several Windows-specific SDK and library conventions.
* Some material texture channels remain unfinished.
* Animation tooling and AI workflows are still evolving.
* Runtime dependency staging needs further consolidation.
* Rendering performance and model behaviour depend on the supplied scene, assets and hardware.

## License

Licensed under the [Apache License 2.0](LICENSE). Dependencies and bundled third-party components retain their respective licences.

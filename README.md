# 3D-Renderer

3D-Renderer is a C++ real-time rendering project built around a custom engine (`Trident`) and an editor application (`Trident-Forge`). It focuses on modern graphics workflows with Vulkan, interactive tooling through ImGui-based panels, and a content pipeline for assets, shaders, and runtime systems.

## Project Structure

- `Trident/`: Core rendering and runtime engine.
- `Trident-Forge/`: Editor executable that uses `Trident` for scene and tooling workflows.
- `Dataset/`: Project data used by the renderer/editor workflows.
- `Scripts/`: Utility scripts for setup and project tasks.
- `Screenshots/`: Captured editor/runtime visuals.

## Core Technologies

- Graphics API: Vulkan
- Language: C++ (CMake-based project)
- UI/Tools: ImGui + ImGuizmo
- Asset and media stack: Assimp, FFmpeg, stb, tinyexr, KTX
- ML runtime integration: ONNX Runtime

## Build Environment

This project is intended to be built with CMake on Windows using MSVC.

### Prerequisites

- Visual Studio with MSVC toolchain
- CMake 3.20+
- Vulkan SDK (`VULKAN_SDK` environment variable must be set)

### Configure

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
```

### Build

```bash
cmake --build build --config Release
```

## What You Get

- A reusable rendering engine library (`Trident`)
- An editor/runtime executable (`Trident-Forge`)
- Shader and asset staging integrated into the build pipeline
- Runtime dependency copying for editor execution

## Screenshots

![Screenshot 1](/Screenshots/Screenshot1.png?raw=true)
![Screenshot 2](/Screenshots/Screenshot2.png?raw=true)
![Screenshot 3](/Screenshots/Screenshot3.png?raw=true)
![Screenshot 4](/Screenshots/Screenshot4.png?raw=true)

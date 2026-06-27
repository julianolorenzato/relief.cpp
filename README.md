# relief.cpp

A C++ tool for mesh simplification using Quadric Error Metrics (QEM) with UV-aware edge collapses, heightmap baking, and UV island leap-map generation for real-time relief mapping.

## Overview

`relief.cpp` takes a high-resolution 3D mesh and produces:

- A simplified mesh via QEM, with configurable UV seam handling strategies.
- A displacement heightmap encoding the signed distance from the simplified surface to the original.
- An offset map (`Offset_Map`) that encodes UV-space translations and rotations needed to continue a relief-mapping ray across UV island boundaries.

## Features

- **QEM Simplification** — Garland & Heckbert quadric error minimization with UV coordinates propagated through collapses.
- **UV Seam Strategies** — Four boundary modes selectable at runtime:
  - `None` — no seam constraints.
  - `Constraint` — soft penalty quadric perpendicular to the seam plane.
  - `LockSeamVertices` — seam vertices are never collapsed.
  - `SyncSeamTwins` — paired seam vertices are collapsed in sync, preserving UV continuity.
- **Envelope Constraint** — optional guarantee that the simplified mesh never protrudes outside the original surface.
- **Heightmap Baking** — per-texel signed displacement computed in UV space using multi-threaded rasterization.
- **Offset Map Baking** — per-texel leap data for UV island boundary traversal during relief mapping.
- **OBJ and glTF I/O** — load and save meshes in both formats (glTF with embedded textures).
- **Qt/OpenGL GUI** — interactive viewer for the mesh, heightmap, and relief result.

## Requirements

| Dependency | Version | Notes |
|---|---|---|
| CMake | ≥ 3.16 | Build system |
| C++ compiler | C++17 | GCC, Clang, or MSVC |
| Eigen3 | ≥ 3.3 | Auto-fetched if not found |
| TinyGLTF | v2.9.3 | Auto-fetched |
| Qt6 | 6.x | Core, Gui, OpenGL, OpenGLWidgets, Widgets |
| OpenGL | any | System library |
| glm | ≥ 1.0.1 | Auto-fetched if not found |

Qt6 and OpenGL are only required when building the GUI (`BUILD_APP=ON`, the default).

## Building

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

To build only the core library without the Qt GUI:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_APP=OFF
cmake --build build -j$(nproc)
```

The resulting executable is `build/gui/gui`.

## Usage

Launch the GUI and open an OBJ or glTF mesh. The interface exposes:

- Target face count for simplification.
- UV seam boundary mode.
- Envelope constraint toggle.
- Heightmap resolution and output path.
- Offset map resolution and seam band width (in texels).

## Roadmap

- [ ] Include UV error in the QEM collapse cost function.
- [ ] Use surface curvature to determine the post-collapse UV position instead of linear interpolation.

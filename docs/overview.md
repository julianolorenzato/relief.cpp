# Relief Mapping Technique (NOT REVIEWED, IT MIGHT CONTAIN GRAMMATICAL/ORTHOGRAPHICAL ERRORS)

## List of Abbreviations
- **HPM** - High Poly Mesh
- **LPM** - Low Poly Mesh
- **HMAP** - Heightmap
- **NMAP** - Normal Map
- **RMAP** - Relief Map
- **OMAP** - Offset Map

## Overview

Initially, the following items are at our disposal:

- HPM with Texture Coordinates (UVs)

The technique use the following steps

1. Simplify (decrease the number of faces/vertices) the HPM, producing a LPM.
2. Measure the distance between the HPM and LPM to generate a heightmap (texture).
3. Generate NMAP from HMAP using the Sobel Method (Sobel Kernels).
4. Perform Relief Mapping algorithm to find the new UVs based on the HMAP, recoverying the HPM details on top of the LPM.

## Simplification

We use the Quadric Erros Metrics (QEM) algorithm to perform the mesh simplificaitons.

### Quadric Error Metrics
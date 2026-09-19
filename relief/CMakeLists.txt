include(FetchContent)

find_package(Threads REQUIRED)

find_package(Eigen3 3.3 QUIET NO_MODULE)
if(NOT Eigen3_FOUND)
  FetchContent_Declare(eigen3 GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git GIT_TAG 3.4.0)
  set(EIGEN_BUILD_DOC       OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTING         OFF CACHE BOOL "" FORCE)
  set(EIGEN_BUILD_PKGCONFIG OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(eigen3)
endif()

FetchContent_Declare(tinygltf GIT_REPOSITORY https://github.com/syoyo/tinygltf.git GIT_TAG v2.9.3)
set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF CACHE BOOL "" FORCE)
set(TINYGLTF_INSTALL              OFF CACHE BOOL "" FORCE)
set(TINYGLTF_HEADER_ONLY          ON  CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(tinygltf)

FetchContent_Declare(tinyobjloader GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git GIT_TAG v2.0.0rc13)
FetchContent_GetProperties(tinyobjloader)
if(NOT tinyobjloader_POPULATED)
  FetchContent_Populate(tinyobjloader)
endif()

set_source_files_properties(src/mesh/io/gltf.cpp PROPERTIES COMPILE_FLAGS "-w")
set_source_files_properties(src/mesh/io/obj.cpp PROPERTIES COMPILE_FLAGS "-w")

add_library(relief STATIC
    src/mesh.cpp
    src/mesh/io.cpp
    src/mesh/io/obj.cpp
    src/mesh/io/gltf.cpp
    src/simplification.cpp
    src/mesh/edgesel.cpp
    src/heightmap.cpp
    src/uv_atlas.cpp
    src/textures.cpp
    src/inflate.cpp
    src/pipeline.cpp
    src/metrics.cpp
    src/pipeline_search.cpp
)

target_include_directories(relief
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(relief
    PUBLIC
        Eigen3::Eigen
        Threads::Threads
)

# tinygltf/tinyobjloader são usados como header-only via FetchContent e não
# são exportáveis; incluímos apenas seus diretórios de include, sem criar
# dependência de link (a implementação é compilada uma vez em gltf.cpp/obj.cpp
# via TINYGLTF_IMPLEMENTATION/TINYOBJLOADER_IMPLEMENTATION).
target_include_directories(relief PRIVATE
    $<TARGET_PROPERTY:tinygltf,INTERFACE_INCLUDE_DIRECTORIES>
    ${tinyobjloader_SOURCE_DIR}
)

# Ferramenta de diagnóstico (temporária): checa se arestas de borda são
# artefatos de vértices duplicados em costuras de UV/material.
add_executable(edge_diag tools/edge_diag.cpp)
target_link_libraries(edge_diag PRIVATE relief)

# Carrega uma malha e imprime Mesh::logSummary(); usado para comparar antes/
# depois de mudanças nos algoritmos de load/simplificação.
add_executable(load_summary tools/load_summary.cpp)
target_link_libraries(load_summary PRIVATE relief)

# Diagnóstico do algoritmo de brush selection: raycast + flood fill de
# arestas a partir de um ponto/raio fixos, sem depender da GUI.
add_executable(edge_selection_diag tools/edge_selection_diag.cpp)
target_link_libraries(edge_selection_diag PRIVATE relief)

# Sanity-checks metrics::imagePSNR on synthetic buffers and exercises
# pipeline::applyPipeline on a real mesh, without needing a GPU/Qt.
add_executable(metrics_diag tools/metrics_diag.cpp)
target_link_libraries(metrics_diag PRIVATE relief)


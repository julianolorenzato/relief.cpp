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

set_source_files_properties(src/qem_gltf.cpp PROPERTIES COMPILE_FLAGS "-w")

add_library(relief STATIC
    src/qem.cpp
    src/qem_gltf.cpp
    src/heightmap.cpp
    src/uv_atlas.cpp
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

# tinygltf é header-only via FetchContent e não é exportável; incluímos
# apenas seu diretório de includes sem criar dependência de link.
target_include_directories(relief PRIVATE
    $<TARGET_PROPERTY:tinygltf,INTERFACE_INCLUDE_DIRECTORIES>
)


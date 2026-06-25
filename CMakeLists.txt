cmake_minimum_required(VERSION 3.16)
project(qem_simplifier CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
  add_compile_options(/W3 /O2)
else()
  add_compile_options(-Wall -Wextra -O2)
endif()

# Suprime warnings de terceiros (stb_image dentro do tinygltf)
set_source_files_properties(source/qem_gltf.cpp PROPERTIES COMPILE_FLAGS "-w")

include(FetchContent)

# ─── Dependências ─────────────────────────────────────────────────────────────

find_package(Qt6 COMPONENTS Core Gui OpenGL OpenGLWidgets Widgets QUIET)
if(NOT Qt6_FOUND)
  find_package(Qt5 COMPONENTS Core Gui OpenGL Widgets REQUIRED)
  set(QT_LIBRARIES Qt5::Core Qt5::Gui Qt5::OpenGL Qt5::Widgets)
else()
  set(QT_LIBRARIES Qt6::Core Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Widgets)
endif()

find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

find_package(glm CONFIG QUIET)
if(NOT glm_FOUND)
  FetchContent_Declare(
      glm
      GIT_REPOSITORY https://github.com/g-truc/glm.git
      GIT_TAG        1.0.1
  )
  set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(glm)
endif()

# ─── Eigen3 (header-only via FetchContent) ────────────────────────────────────

find_package(Eigen3 3.3 QUIET NO_MODULE)
if(NOT Eigen3_FOUND)
  FetchContent_Declare(
      eigen3
      GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
      GIT_TAG        3.4.0
  )
  set(EIGEN_BUILD_DOC     OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTING       OFF CACHE BOOL "" FORCE)
  set(EIGEN_BUILD_PKGCONFIG OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(eigen3)
endif()

# ─── tinygltf (header-only via FetchContent) ──────────────────────────────────

FetchContent_Declare(
    tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG        v2.9.3
)
set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF CACHE BOOL "" FORCE)
set(TINYGLTF_INSTALL OFF CACHE BOOL "" FORCE)
set(TINYGLTF_HEADER_ONLY ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(tinygltf)

# ─── Executável GUI ───────────────────────────────────────────────────────────

add_executable(qem_gui
    source/main_gui.cpp
    source/qem.cpp
    source/qem_gltf.cpp
    source/glwidget.cpp
    source/overlayglwidget.cpp
    source/reliefglwidget.cpp
    source/mainwindow.cpp
    source/heightmap.cpp
    source/heightmap_worker.cpp
    source/uv_atlas.cpp
    source/texture_prep.cpp
    source/texture_prep_worker.cpp
    source/shaders/shaders.qrc
)

target_link_libraries(qem_gui
    ${QT_LIBRARIES}
    OpenGL::GL
    tinygltf
    Eigen3::Eigen
    Threads::Threads
)

target_include_directories(qem_gui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ─── Instalação ───────────────────────────────────────────────────────────────

install(TARGETS qem_gui)

# Uso:
#   cmake -B build && cmake --build build
#   ./build/qem_gui

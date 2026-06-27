cmake_minimum_required(VERSION 3.16)
project(relief CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
  add_compile_options(/W3 /O2)
else()
  add_compile_options(-Wall -Wextra -O2)
endif()

option(BUILD_APP "Compilar o executável Qt/OpenGL" ON)

# ── Subdiretórios ─────────────────────────────────────────────────────────────

add_subdirectory(relief)

if(BUILD_APP)
  add_subdirectory(gui)
endif()

# ── Install rules ─────────────────────────────────────────────────────────────

include(GNUInstallDirs)

if(BUILD_APP)
  install(TARGETS gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

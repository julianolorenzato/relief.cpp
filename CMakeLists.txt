cmake_minimum_required(VERSION 3.16)
project(relief.cpp CXX)

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

# ── Doxygen Documentation ─────────────────────────────────────────────────────
find_package(Doxygen REQUIRED dot OPTIONAL_COMPONENTS mscgen dia)
if(DOXYGEN_FOUND)
  set(DOXYGEN_GENERATE_HTML YES)
  set(DOXYGEN_GENERATE_LATEX NO)
  set(DOXYGEN_EXTRACT_ALL YES)
  set(DOXYGEN_EXTRACT_PRIVATE YES)
  set(DOXYGEN_EXTRACT_STATIC YES)
  set(DOXYGEN_EXTRACT_LOCAL_CLASSES YES)
  set(DOXYGEN_EXTRACT_LOCAL_METHODS YES)
  set(DOXYGEN_USE_MDFILE_AS_MAINPAGE README.md)
  set(DOXYGEN_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/docs)

  doxygen_add_docs(docs
    ${CMAKE_SOURCE_DIR}/README.md
    ${CMAKE_CURRENT_SOURCE_DIR}/relief
    ${CMAKE_CURRENT_SOURCE_DIR}/gui
    COMMENT "Generating API documentation with Doxygen"
  )
else()
  message(STATUS "Doxygen not found, unable to generate API documentation")
endif()

# ── Subdirectories ─────────────────────────────────────────────────────────────
add_subdirectory(relief)

if(BUILD_APP)
  add_subdirectory(gui)
endif()

# ── Install rules ─────────────────────────────────────────────────────────────
include(GNUInstallDirs)

if(BUILD_APP)
  install(TARGETS gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

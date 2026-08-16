find_package(Qt6 COMPONENTS Core Gui OpenGL OpenGLWidgets Widgets REQUIRED)
find_package(OpenGL REQUIRED)

include(FetchContent)
find_package(glm CONFIG QUIET)
if(NOT glm_FOUND)
  FetchContent_Declare(glm GIT_REPOSITORY https://github.com/g-truc/glm.git GIT_TAG 1.0.1)
  set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(glm)
endif()

add_executable(gui
    include/gui/mainwindow.h
    include/gui/orbital3dview.h
    include/gui/simplifier_module.h
    include/gui/heightmap_module.h
    include/gui/texture_prep_module.h
    include/gui/relief_module.h
    include/gui/relief_view.h
    include/gui/relief_test_module.h
    include/gui/texture_inspector_dialog.h
    src/main.cpp
    src/mainwindow.cpp
    src/orbital3dview.cpp
    src/simplifier_module.cpp
    src/heightmap_module.cpp
    src/texture_prep_module.cpp
    src/relief_module.cpp
    src/relief_view.cpp
    src/relief_test_module.cpp
    src/texture_inspector_dialog.cpp
    src/shaders/shaders.qrc
)

set_target_properties(gui PROPERTIES
    AUTOMOC ON
    AUTORCC ON
    AUTOUIC ON
)

target_include_directories(gui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(gui
    PRIVATE
        relief
        Qt6::Core Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Widgets
        OpenGL::GL
        glm::glm
)

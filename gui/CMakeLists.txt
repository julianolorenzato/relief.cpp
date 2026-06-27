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
    include/gui/glwidget.h
    include/gui/overlayglwidget.h
    include/gui/reliefglwidget.h
    include/gui/heightmap_worker.h
    include/gui/texture_prep_worker.h
    src/main.cpp
    src/mainwindow.cpp
    src/glwidget.cpp
    src/overlayglwidget.cpp
    src/reliefglwidget.cpp
    src/heightmap_worker.cpp
    src/texture_prep.cpp
    src/texture_prep_worker.cpp
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

find_package(Qt6 COMPONENTS Core Gui OpenGL OpenGLWidgets Widgets REQUIRED)
find_package(OpenGL REQUIRED)

include(FetchContent)
find_package(glm CONFIG QUIET)
if(NOT glm_FOUND)
    FetchContent_Declare(
        glm
        GIT_REPOSITORY
        https://github.com/g-truc/glm.git GIT_TAG 1.0.1
    )
    set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glm)
endif()

add_executable(gui
    include/gui/mainwindow.h
    include/gui/global_context.h
    include/gui/module.h
    include/gui/geometry.h
    include/gui/views/orbital3dview.h
    include/gui/views/mesh_view.h
    include/gui/views/old_relief_view.h
    include/gui/modules/editor_module.h
    include/gui/modules/textures_module.h
    include/gui/modules/old_module.h
    include/gui/views/relief_view.h
    include/gui/modules/relief_sandbox_module.h
    include/gui/modules/normal_map_module.h
    include/gui/modules/relief_module.h
    src/main.cpp
    src/mainwindow.cpp
    src/global_context.cpp
    src/module.cpp
    src/geometry.cpp
    src/views/orbital3dview.cpp
    src/views/mesh_view.cpp
    src/views/old_relief_view.cpp
    src/modules/editor_module.cpp
    src/modules/textures_module.cpp
    src/modules/old_module.cpp
    src/views/relief_view.cpp
    src/modules/relief_sandbox_module.cpp
    src/modules/normal_map_module.cpp
    src/modules/relief_module.cpp
    shaders/shaders.qrc
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

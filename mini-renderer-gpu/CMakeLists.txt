cmake_minimum_required(VERSION 3.20)
project(mini-renderer-gpu LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ---- SDL2 ----
FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-2.30.9
    GIT_SHALLOW    TRUE
)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL2)

# ---- GLAD2 (OpenGL 3.3 Core loader) ----
FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG        v2.0.6
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  cmake
)
FetchContent_MakeAvailable(glad)
glad_add_library(glad_gl REPRODUCIBLE LOADER API gl:core=3.3)

# ---- Executable ----
set(SOURCES
    src/main.cpp
    src/gl_renderer.cpp
    src/timer.cpp
)

add_executable(mini-renderer-gpu ${SOURCES})

target_include_directories(mini-renderer-gpu PRIVATE src)

target_link_libraries(mini-renderer-gpu PRIVATE
    SDL2-static
    SDL2main
    glad_gl
    opengl32
)

# Copy shaders next to the executable
add_custom_command(TARGET mini-renderer-gpu POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/shaders"
        "$<TARGET_FILE_DIR:mini-renderer-gpu>/shaders"
)

if(WIN32)
    set_target_properties(mini-renderer-gpu PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

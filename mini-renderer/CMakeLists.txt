cmake_minimum_required(VERSION 3.20)
project(mini-renderer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ----------------------------------------
# SDL2 を自動取得
# ----------------------------------------
include(FetchContent)
FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-2.30.9
    GIT_SHALLOW    TRUE
)
set(SDL_STATIC  ON  CACHE BOOL "" FORCE)
set(SDL_SHARED  OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL2)

# ----------------------------------------
# ソース
# ----------------------------------------
set(SOURCES
    src/main.cpp
    src/framebuffer.cpp
    src/renderer.cpp
)

add_executable(mini-renderer ${SOURCES})

target_include_directories(mini-renderer PRIVATE src)
target_link_libraries(mini-renderer PRIVATE SDL2-static SDL2main)

# Windows: コンソールではなくウィンドウアプリとして起動
if(WIN32)
    set_target_properties(mini-renderer PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

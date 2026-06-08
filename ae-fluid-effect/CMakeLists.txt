cmake_minimum_required(VERSION 3.20)
project(ae-fluid-effect LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ----------------------------------------------------------------
# AE SDK パス（インストール後に設定）
# Adobe After Effects SDK: https://developer.adobe.com
# ----------------------------------------------------------------
set(AE_SDK_DIR "C:/AdobeSDK/AfterEffectsSDK" CACHE PATH "AE SDK root")

set(AE_SDK_HEADERS
    "${AE_SDK_DIR}/Headers"
    "${AE_SDK_DIR}/Headers/SP"
    "${AE_SDK_DIR}/Resources"
    "${AE_SDK_DIR}/Util"
)

# ----------------------------------------------------------------
# ソース
# ----------------------------------------------------------------
set(SOURCES
    src/fluid_effect.cpp
    src/vol_loader.cpp
    src/tile_renderer.cpp
)

# AE プラグインは DLL (.aex)
add_library(ae-fluid-effect SHARED ${SOURCES})

target_include_directories(ae-fluid-effect PRIVATE
    src
    ${AE_SDK_HEADERS}
)

# AE SDK ライブラリ（Windows）
target_link_libraries(ae-fluid-effect PRIVATE
    "${AE_SDK_DIR}/Resources/AEFX_SuiteHandlerTemplate.cpp"
)

# .aex 拡張子に変更
set_target_properties(ae-fluid-effect PROPERTIES
    SUFFIX ".aex"
    PREFIX ""
)

# ----------------------------------------------------------------
# インストール先（AE のプラグインフォルダ）
# ----------------------------------------------------------------
install(TARGETS ae-fluid-effect
    DESTINATION "$ENV{PROGRAMFILES}/Adobe/After Effects/Support Files/Plug-ins/FluidStudy"
)

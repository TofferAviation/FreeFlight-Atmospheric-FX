cmake_minimum_required(VERSION 3.20)
project(FFAtmo VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FFATMO_BUILD_PLUGIN "Build the X-Plane plugin" ON)
option(FFATMO_BUILD_TESTS "Build the platform-independent tests" ON)
option(FFATMO_BUILD_IMGUI_PANEL "Compile the six-page ImGui panel source" OFF)
option(FFATMO_BUILD_COMPANION "Build the native Windows companion application" ON)
set(XPLANE_SDK "" CACHE PATH "Path to the extracted X-Plane SDK folder")
set(IMGUI_DIR "" CACHE PATH "Path to Dear ImGui (contains imgui.h)")

add_library(ffatmo_core STATIC
    src/AircraftProfile.cpp
    src/AtmosphereModel.cpp
    src/CompanionBridge.cpp
    src/Settings.cpp
)
target_include_directories(ffatmo_core PUBLIC src)
set_target_properties(ffatmo_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(ffatmo_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(ffatmo_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(FFATMO_BUILD_COMPANION AND WIN32)
    if(NOT EXISTS "${IMGUI_DIR}/imgui.h")
        message(FATAL_ERROR "Set IMGUI_DIR to Dear ImGui (must contain imgui.h and backends/)")
    endif()
    add_executable(FFAtmoCompanion WIN32
        app/ImGuiCompanion.cpp
        app/UpdateService.cpp
        app/FFAtmoAssets.rc
        app/TelemetryProvider.cpp
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
        ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
    )
    target_include_directories(FFAtmoCompanion PRIVATE src app "${IMGUI_DIR}" "${IMGUI_DIR}/backends")
    target_link_libraries(FFAtmoCompanion PRIVATE ffatmo_core shell32 ole32 windowscodecs d3d11 dxgi winhttp)
    target_compile_definitions(FFAtmoCompanion PRIVATE UNICODE _UNICODE NOMINMAX)
    if(MSVC)
        target_compile_options(FFAtmoCompanion PRIVATE /W4 /permissive-)
        target_link_options(FFAtmoCompanion PRIVATE
            "/MANIFESTDEPENDENCY:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'"
        )
    endif()
    set_target_properties(FFAtmoCompanion PROPERTIES
        OUTPUT_NAME "FFAtmoCompanion"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/package/FFAtmo"
    )

    add_executable(FFAtmoUpdater WIN32 app/UpdaterMain.cpp)
    target_link_libraries(FFAtmoUpdater PRIVATE shell32 winhttp bcrypt)
    set_target_properties(FFAtmoUpdater PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/package/FFAtmo"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/package/FFAtmo"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/package/FFAtmo"
    )
endif()

if(FFATMO_BUILD_TESTS)
    enable_testing()
    add_executable(ffatmo_core_tests tests/CoreTests.cpp)
    target_link_libraries(ffatmo_core_tests PRIVATE ffatmo_core)
    add_test(NAME ffatmo_core_tests COMMAND ffatmo_core_tests)
endif()

if(FFATMO_BUILD_IMGUI_PANEL)
    if(NOT EXISTS "${IMGUI_DIR}/imgui.h")
        message(FATAL_ERROR "Set IMGUI_DIR to the Dear ImGui source folder")
    endif()
    add_library(ffatmo_imgui_panel STATIC src/Panel.cpp)
    target_include_directories(ffatmo_imgui_panel PUBLIC src "${IMGUI_DIR}")
    target_link_libraries(ffatmo_imgui_panel PUBLIC ffatmo_core)
endif()

if(FFATMO_BUILD_PLUGIN)
    if(NOT EXISTS "${XPLANE_SDK}/CHeaders/XPLM/XPLMPlugin.h")
        message(FATAL_ERROR "Set XPLANE_SDK to the SDK folder containing CHeaders and Libraries")
    endif()

    add_library(FFAtmo MODULE
        src/ParticleInstance.cpp
        src/Plugin.cpp
        src/PublishedDatarefs.cpp
        src/SimDatarefs.cpp
    )
    target_include_directories(FFAtmo PRIVATE
        src
        "${XPLANE_SDK}/CHeaders/XPLM"
    )
    target_link_libraries(FFAtmo PRIVATE ffatmo_core)
    target_compile_definitions(FFAtmo PRIVATE
        XPLM400=1 XPLM303=1 XPLM301=1 XPLM300=1 XPLM210=1 XPLM200=1 XPLM_64=1
    )

    set(FFATMO_PACKAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/package/FFAtmo")
    if(WIN32)
        target_compile_definitions(FFAtmo PRIVATE IBM=1 NOMINMAX _CRT_SECURE_NO_WARNINGS)
        target_link_libraries(FFAtmo PRIVATE "${XPLANE_SDK}/Libraries/Win/XPLM_64.lib")
        set_target_properties(FFAtmo PROPERTIES
            PREFIX "" SUFFIX ".xpl" OUTPUT_NAME "win"
            LIBRARY_OUTPUT_DIRECTORY "${FFATMO_PACKAGE_DIR}/64"
            RUNTIME_OUTPUT_DIRECTORY "${FFATMO_PACKAGE_DIR}/64"
        )
    elseif(APPLE)
        target_compile_definitions(FFAtmo PRIVATE APL=1)
        set_target_properties(FFAtmo PROPERTIES
            PREFIX "" SUFFIX ".xpl" OUTPUT_NAME "mac"
            LIBRARY_OUTPUT_DIRECTORY "${FFATMO_PACKAGE_DIR}/64"
        )
    else()
        target_compile_definitions(FFAtmo PRIVATE LIN=1)
        set_target_properties(FFAtmo PROPERTIES
            PREFIX "" SUFFIX ".xpl" OUTPUT_NAME "lin"
            LIBRARY_OUTPUT_DIRECTORY "${FFATMO_PACKAGE_DIR}/64"
        )
    endif()
endif()

cmake_minimum_required(VERSION 3.20)
project(FFAtmo VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(FFATMO_APP_VERSION "${PROJECT_VERSION}" CACHE STRING "Version reported by the companion and updater")
set(FFATMO_GIT_REVISION "unknown" CACHE STRING "Git revision embedded in diagnostics builds")

option(FFATMO_BUILD_PLUGIN "Build the X-Plane plugin" ON)
option(FFATMO_BUILD_TESTS "Build the platform-independent tests" ON)
option(FFATMO_BUILD_IMGUI_PANEL "Compile the six-page ImGui panel source" OFF)
option(FFATMO_BUILD_COMPANION "Build the native Windows companion application" ON)
option(FFATMO_BUILD_GEOMETRY_DEBUG "Build the standalone ACF geometry-debug plugin" OFF)
option(FFATMO_BUILD_RECORDER_DEBUG "Build the standalone simulator snapshot recorder" OFF)

set(XPLANE_SDK "" CACHE PATH "Path to the extracted X-Plane SDK folder")
set(IMGUI_DIR "" CACHE PATH "Path to Dear ImGui (contains imgui.h)")

find_package(Threads REQUIRED)

add_library(ffatmo_core STATIC
    src/AircraftProfile.cpp
    src/AtmosphereModel.cpp
    src/CompanionBridge.cpp
    src/Settings.cpp
    src/acf/AcfGeometry.cpp
    src/diagnostics/ReplayFile.cpp
)
target_include_directories(ffatmo_core PUBLIC src)
target_link_libraries(ffatmo_core PUBLIC Threads::Threads)
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
        app/GithubAuth.cpp
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
    target_link_libraries(FFAtmoCompanion PRIVATE ffatmo_core shell32 ole32 windowscodecs d3d11 dxgi winhttp crypt32)
    target_compile_definitions(FFAtmoCompanion PRIVATE UNICODE _UNICODE NOMINMAX)
    target_compile_definitions(FFAtmoCompanion PRIVATE FFATMO_APP_VERSION="${FFATMO_APP_VERSION}")
    if(MSVC)
        target_compile_options(FFAtmoCompanion PRIVATE /W4 /permissive-)
        target_link_options(FFAtmoCompanion PRIVATE
            "/MANIFESTDEPENDENCY:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'"
        )
    endif()
    set_target_properties(FFAtmoCompanion PROPERTIES
        OUTPUT_NAME "FFAtmoCompanion"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/package/FFAtmo"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/package/FFAtmo"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/package/FFAtmo"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/package/FFAtmo"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_CURRENT_SOURCE_DIR}/package/FFAtmo"
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

    add_executable(ffatmo_acf_tests tests/AcfParserTests.cpp)
    target_link_libraries(ffatmo_acf_tests PRIVATE ffatmo_core)
    add_test(NAME ffatmo_acf_tests COMMAND ffatmo_acf_tests)

    add_executable(ffatmo_replay_tests tests/ReplayTests.cpp)
    target_link_libraries(ffatmo_replay_tests PRIVATE ffatmo_core)
    add_test(NAME ffatmo_replay_tests COMMAND ffatmo_replay_tests)
endif()

if(FFATMO_BUILD_IMGUI_PANEL)
    if(NOT EXISTS "${IMGUI_DIR}/imgui.h")
        message(FATAL_ERROR "Set IMGUI_DIR to the Dear ImGui source folder")
    endif()
    add_library(ffatmo_imgui_panel STATIC src/Panel.cpp)
    target_include_directories(ffatmo_imgui_panel PUBLIC src "${IMGUI_DIR}")
    target_link_libraries(ffatmo_imgui_panel PUBLIC ffatmo_core)
endif()

if(FFATMO_BUILD_PLUGIN OR FFATMO_BUILD_GEOMETRY_DEBUG OR FFATMO_BUILD_RECORDER_DEBUG)
    if(NOT EXISTS "${XPLANE_SDK}/CHeaders/XPLM/XPLMPlugin.h")
        message(FATAL_ERROR "Set XPLANE_SDK to the SDK folder containing CHeaders and Libraries")
    endif()
endif()

if(FFATMO_BUILD_PLUGIN)
    add_library(FFAtmo MODULE
        src/ParticleInstance.cpp
        src/ParticleInstanceCompat.cpp
        src/Plugin.cpp
        src/PublishedDatarefs.cpp
        src/SimDatarefs.cpp
    )
    target_include_directories(FFAtmo PRIVATE src "${XPLANE_SDK}/CHeaders/XPLM")
    target_link_libraries(FFAtmo PRIVATE ffatmo_core)
    target_compile_definitions(FFAtmo PRIVATE
        XPLM400=1 XPLM303=1 XPLM302=1 XPLM301=1 XPLM300=1 XPLM210=1 XPLM200=1 XPLM_64=1
    )
    set(FFATMO_PACKAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/package/FFAtmo")
    if(WIN32)
        target_compile_definitions(FFAtmo PRIVATE IBM=1 NOMINMAX _CRT_SECURE_NO_WARNINGS)
        target_link_libraries(FFAtmo PRIVATE "${XPLANE_SDK}/Libraries/Win/XPLM_64.lib")
        set_target_properties(FFAtmo PROPERTIES
            PREFIX "" SUFFIX ".xpl" OUTPUT_NAME "win"
            LIBRARY_OUTPUT_DIRECTORY "${FFATMO_PACKAGE_DIR}/64"
            LIBRARY_OUTPUT_DIRECTORY_DEBUG "${FFATMO_PACKAGE_DIR}/64"
            LIBRARY_OUTPUT_DIRECTORY_RELEASE "${FFATMO_PACKAGE_DIR}/64"
            LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${FFATMO_PACKAGE_DIR}/64"
            LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${FFATMO_PACKAGE_DIR}/64"
            RUNTIME_OUTPUT_DIRECTORY "${FFATMO_PACKAGE_DIR}/64"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG "${FFATMO_PACKAGE_DIR}/64"
            RUNTIME_OUTPUT_DIRECTORY_RELEASE "${FFATMO_PACKAGE_DIR}/64"
            RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${FFATMO_PACKAGE_DIR}/64"
            RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${FFATMO_PACKAGE_DIR}/64"
        )
    elseif(APPLE)
        target_compile_definitions(FFAtmo PRIVATE APL=1)
        set_target_properties(FFAtmo PROPERTIES PREFIX "" SUFFIX ".xpl" OUTPUT_NAME "mac"
            LIBRARY_OUTPUT_DIRECTORY "${FFATMO_PACKAGE_DIR}/64")
    else()
        target_compile_definitions(FFAtmo PRIVATE LIN=1)
        set_target_properties(FFAtmo PROPERTIES PREFIX "" SUFFIX ".xpl" OUTPUT_NAME "lin"
            LIBRARY_OUTPUT_DIRECTORY "${FFATMO_PACKAGE_DIR}/64")
    endif()
endif()

if(FFATMO_BUILD_GEOMETRY_DEBUG)
    add_library(FFAtmoEngineDebug MODULE
        src/GeometryDebugPlugin.cpp
        src/GeometryDebugOverlay.cpp
    )
    target_include_directories(FFAtmoEngineDebug PRIVATE src "${XPLANE_SDK}/CHeaders/XPLM")
    target_link_libraries(FFAtmoEngineDebug PRIVATE ffatmo_core)
    target_compile_definitions(FFAtmoEngineDebug PRIVATE
        XPLM400=1 XPLM303=1 XPLM302=1 XPLM301=1 XPLM300=1 XPLM210=1 XPLM200=1 XPLM_64=1
    )
    set(FFATMO_DEBUG_PACKAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/package/FFAtmoEngineDebug")
    if(WIN32)
        target_compile_definitions(FFAtmoEngineDebug PRIVATE IBM=1 NOMINMAX _CRT_SECURE_NO_WARNINGS)
        target_link_libraries(FFAtmoEngineDebug PRIVATE "${XPLANE_SDK}/Libraries/Win/XPLM_64.lib" opengl32)
        set_target_properties(FFAtmoEngineDebug PROPERTIES
            PREFIX "" SUFFIX ".xpl" OUTPUT_NAME "win"
            LIBRARY_OUTPUT_DIRECTORY "${FFATMO_DEBUG_PACKAGE_DIR}/64"
            LIBRARY_OUTPUT_DIRECTORY_DEBUG "${FFATMO_DEBUG_PACKAGE_DIR}/64"
            LIBRARY_OUTPUT_DIRECTORY_RELEASE "${FFATMO_DEBUG_PACKAGE_DIR}/64"
            LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${FFATMO_DEBUG_PACKAGE_DIR}/64"
            LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${FFATMO_DEBUG_PACKAGE_DIR}/64"
            RUNTIME_OUTPUT_DIRECTORY "${FFATMO_DEBUG_PACKAGE_DIR}/64"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG "${FFATMO_DEBUG_PACKAGE_DIR}/64"
            RUNTIME_OUTPUT_DIRECTORY_RELEASE "${FFATMO_DEBUG_PACKAGE_DIR}/64"
            RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${FFATMO_DEBUG_PACKAGE_DIR}/64"
            RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${FFATMO_DEBUG_PACKAGE_DIR}/64"
        )
    elseif(APPLE)
        target_compile_definitions(FFAtmoEngineDebug PRIVATE APL=1)
        find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)
        target_link_libraries(FFAtmoEngineDebug PRIVATE ${OPENGL_FRAMEWORK})
        set_target_properties(FFAtmoEngineDebug PROPERTIES PREFIX "" SUFFIX ".xpl" OUTPUT_NAME "mac"
            LIBRARY_OUTPUT_DIRECTORY "${FFATMO_DEBUG_PACKAGE_DIR}/64")
    else()
        target_compile_definitions(FFAtmoEngineDebug PRIVATE LIN=1)
        target_link_libraries(FFAtmoEngineDebug PRIVATE GL)
        set_target_properties(FFAtmoEngineDebug PROPERTIES PREFIX "" SUFFIX ".xpl" OUTPUT_NAME "lin"
            LIBRARY_OUTPUT_DIRECTORY "${FFATMO_DEBUG_PACKAGE_DIR}/64")
    endif()
endif()

if(FFATMO_BUILD_RECORDER_DEBUG)
    add_library(FFAtmoEngineRecorder MODULE
        src/RecorderDebugPlugin.cpp
        src/host/XPlaneSnapshotSource.cpp
    )
    target_include_directories(FFAtmoEngineRecorder PRIVATE src "${XPLANE_SDK}/CHeaders/XPLM")
    target_link_libraries(FFAtmoEngineRecorder PRIVATE ffatmo_core)
    target_compile_definitions(FFAtmoEngineRecorder PRIVATE
        XPLM400=1 XPLM303=1 XPLM302=1 XPLM301=1 XPLM300=1 XPLM210=1 XPLM200=1 XPLM_64=1
        FFATMO_RECORDER_BUILD="engine-v1-recorder-v1"
        FFATMO_GIT_REVISION="${FFATMO_GIT_REVISION}"
    )
    set(FFATMO_RECORDER_PACKAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/package/FFAtmoEngineRecorder")
    if(WIN32)
        target_compile_definitions(FFAtmoEngineRecorder PRIVATE IBM=1 NOMINMAX _CRT_SECURE_NO_WARNINGS)
        target_link_libraries(FFAtmoEngineRecorder PRIVATE "${XPLANE_SDK}/Libraries/Win/XPLM_64.lib")
        set_target_properties(FFAtmoEngineRecorder PROPERTIES
            PREFIX "" SUFFIX ".xpl" OUTPUT_NAME "win"
            LIBRARY_OUTPUT_DIRECTORY "${FFATMO_RECORDER_PACKAGE_DIR}/64"
            LIBRARY_OUTPUT_DIRECTORY_DEBUG "${FFATMO_RECORDER_PACKAGE_DIR}/64"
            LIBRARY_OUTPUT_DIRECTORY_RELEASE "${FFATMO_RECORDER_PACKAGE_DIR}/64"
            LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${FFATMO_RECORDER_PACKAGE_DIR}/64"
            LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${FFATMO_RECORDER_PACKAGE_DIR}/64"
            RUNTIME_OUTPUT_DIRECTORY "${FFATMO_RECORDER_PACKAGE_DIR}/64"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG "${FFATMO_RECORDER_PACKAGE_DIR}/64"
            RUNTIME_OUTPUT_DIRECTORY_RELEASE "${FFATMO_RECORDER_PACKAGE_DIR}/64"
            RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${FFATMO_RECORDER_PACKAGE_DIR}/64"
            RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${FFATMO_RECORDER_PACKAGE_DIR}/64"
        )
    elseif(APPLE)
        target_compile_definitions(FFAtmoEngineRecorder PRIVATE APL=1)
        set_target_properties(FFAtmoEngineRecorder PROPERTIES PREFIX "" SUFFIX ".xpl" OUTPUT_NAME "mac"
            LIBRARY_OUTPUT_DIRECTORY "${FFATMO_RECORDER_PACKAGE_DIR}/64")
    else()
        target_compile_definitions(FFAtmoEngineRecorder PRIVATE LIN=1)
        set_target_properties(FFAtmoEngineRecorder PROPERTIES PREFIX "" SUFFIX ".xpl" OUTPUT_NAME "lin"
            LIBRARY_OUTPUT_DIRECTORY "${FFATMO_RECORDER_PACKAGE_DIR}/64")
    endif()
endif()

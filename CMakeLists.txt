# -----------------------------------------------------------------------------
# MW2 (2009) 64-bit FPS / FOV Unlocker
#
# Windows x64 only. The 64-bit requirement is not cosmetic: a 32-bit process
# cannot inject a DLL into, or read/write the memory of, a 64-bit process. The
# unlocker and its injector must both be x64.
# -----------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.20)

project(mw2_x64_unlocker LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "This project targets Windows only.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR
        "This project targets 64-bit Windows only.\n"
        "Re-configure with a 64-bit toolchain, e.g.\n"
        "    cmake -S . -B build -A x64")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # Static CRT so neither the DLL nor the injector needs a runtime redist.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/W4 /permissive- /EHsc)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# ---------------------------------------------------------------------------
# Unlocker DLL (loaded inside the game process)
# ---------------------------------------------------------------------------
add_library(mw2_unlocker SHARED
    src/dllmain.cpp
    src/log.cpp
    src/memory.cpp
    src/scanner.cpp
    src/patcher.cpp
    src/config.cpp
    src/features.cpp
)
target_include_directories(mw2_unlocker PRIVATE src)
target_link_libraries(mw2_unlocker PRIVATE user32)

if(MSVC)
    # Keep ASLR/high-entropy ASLR consistent with the host game.
    target_link_options(mw2_unlocker PRIVATE /DYNAMICBASE /HIGHENTROPYVA)
endif()

set_target_properties(mw2_unlocker PROPERTIES OUTPUT_NAME "mw2_unlocker")

# ---------------------------------------------------------------------------
# Injector (needs to be x64 to touch the 64-bit game process)
# ---------------------------------------------------------------------------
add_executable(injector injector/main.cpp)
set_target_properties(injector PROPERTIES OUTPUT_NAME "injector")

# Drop a copy of the config next to the built binaries for convenience.
add_custom_command(TARGET mw2_unlocker POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/unlocker.ini"
        "$<TARGET_FILE_DIR:mw2_unlocker>/unlocker.ini"
    COMMENT "Copying default unlocker.ini next to the DLL")

# ---------------------------------------------------------------------------
# One-click launcher: a single self-contained EXE.
#
# The DLL and the default config are embedded as resources, so the user gets one
# file to double-click. At run time it extracts them to
# %LOCALAPPDATA%\MW2Unlocker, finds the running game, and injects.
# ---------------------------------------------------------------------------
set(LAUNCHER_RC "${CMAKE_CURRENT_BINARY_DIR}/launcher_resources.rc")

add_custom_command(
    OUTPUT "${LAUNCHER_RC}"
    COMMAND ${CMAKE_COMMAND}
            "-DOUT=${LAUNCHER_RC}"
            "-DDLL=$<TARGET_FILE:mw2_unlocker>"
            "-DINI=${CMAKE_SOURCE_DIR}/config/unlocker.ini"
            -P "${CMAKE_SOURCE_DIR}/launcher/generate_rc.cmake"
    DEPENDS mw2_unlocker
            "${CMAKE_SOURCE_DIR}/config/unlocker.ini"
            "${CMAKE_SOURCE_DIR}/launcher/generate_rc.cmake"
    COMMENT "Embedding mw2_unlocker.dll into the launcher resources"
    VERBATIM)

add_executable(MW2Unlocker launcher/main.cpp "${LAUNCHER_RC}")
set_target_properties(MW2Unlocker PROPERTIES OUTPUT_NAME "MW2Unlocker")
add_dependencies(MW2Unlocker mw2_unlocker)

# UNICODE/_UNICODE so the Win32 macros (RT_RCDATA, MAKEINTRESOURCE) resolve to
# their wide variants, matching the explicit ...W calls the launcher uses.
target_compile_definitions(MW2Unlocker PRIVATE UNICODE _UNICODE)

if(MSVC)
    target_link_options(MW2Unlocker PRIVATE /DYNAMICBASE /HIGHENTROPYVA)
endif()

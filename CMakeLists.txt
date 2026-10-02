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
# Build number
#
# So a user can say which build they are running. The number is a counter kept
# in the build tree: it is seeded from the commit count, so a fresh tree starts
# somewhere sensible and the number never goes backwards, and then it goes up by
# one on every *build*. Taking the commit count on its own was what left it stuck
# on the same number while the tree was being worked on - two binaries from
# different afternoons both said "build 39" - and a counter that only moved on
# configure has the same problem from the other side, because editing a source
# does not re-run CMake: the next binary would carry the previous number again.
#
# The stamping itself is cmake/bump_build.cmake, run by the mw2_build_number
# target below; MW2Unlocker depends on that target, so the header is written
# before the launcher is compiled.
#
# The short commit hash rides along for reference, with a "+" when the tree has
# uncommitted changes, and a tree with no git at all falls back rather than
# failing.
# ---------------------------------------------------------------------------
execute_process(COMMAND git rev-list --count HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE MW2_BUILD_COMMITS
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE MW2_BUILD_RESULT)

execute_process(COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE MW2_BUILD_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)

if(NOT MW2_BUILD_RESULT EQUAL 0 OR MW2_BUILD_COMMITS STREQUAL "")
    set(MW2_BUILD_COMMITS "0")
    set(MW2_BUILD_HASH "nogit")
endif()

string(STRIP "${MW2_BUILD_HASH}" MW2_BUILD_HASH)

# The counter, read here only to say where it has got to - the increment and the
# header belong to the build. It lives in the build directory rather than the
# source tree, so stamping never leaves a tracked file dirty, and so each working
# copy counts on its own.
set(MW2_COUNTER_FILE "${CMAKE_BINARY_DIR}/build_number.txt")
set(MW2_BUILD_NUMBER "")
if(EXISTS "${MW2_COUNTER_FILE}")
    file(READ "${MW2_COUNTER_FILE}" MW2_BUILD_NUMBER)
    string(STRIP "${MW2_BUILD_NUMBER}" MW2_BUILD_NUMBER)
endif()
if(NOT MW2_BUILD_NUMBER MATCHES "^[0-9]+$")
    set(MW2_BUILD_NUMBER "${MW2_BUILD_COMMITS}")
endif()
message(STATUS "MW2 unlocker: the build counter is at ${MW2_BUILD_NUMBER}; "
               "the next build is stamped with the number after it")

# The stamping step. ALL, so it runs on every build even when nothing else does,
# and the launcher depends on it so it has happened before the compile that
# includes the header.
add_custom_target(mw2_build_number ALL
    COMMAND ${CMAKE_COMMAND}
            "-DMW2_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
            "-DMW2_BINARY_DIR=${CMAKE_BINARY_DIR}"
            "-DMW2_BUILD_COMMITS=${MW2_BUILD_COMMITS}"
            "-DMW2_BUILD_HASH=${MW2_BUILD_HASH}"
            -P "${CMAKE_SOURCE_DIR}/cmake/bump_build.cmake"
    COMMENT "Stamping the build number"
    VERBATIM)

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

add_executable(MW2Unlocker
    launcher/main.cpp
    launcher/gui.cpp
    launcher/update.cpp
    "${LAUNCHER_RC}")

# A window, not a console: double-clicking must not open a terminal alongside it.
# stdio still works when a caller supplies handles, so running from a shell or
# with output redirected still captures the same diagnostics.
set_target_properties(MW2Unlocker PROPERTIES
    OUTPUT_NAME "MW2Unlocker"
    WIN32_EXECUTABLE TRUE)

add_dependencies(MW2Unlocker mw2_unlocker)
# The build number, stamped before this project compiles: gui.cpp includes the
# header it writes.
add_dependencies(MW2Unlocker mw2_build_number)

# UNICODE/_UNICODE so the Win32 macros (RT_RCDATA, MAKEINTRESOURCE) resolve to
# their wide variants, matching the explicit ...W calls the launcher uses.
target_compile_definitions(MW2Unlocker PRIVATE UNICODE _UNICODE)

# src/ipc.h defines the live channel this launcher and the injected DLL talk
# over. Sharing the header is what stops the two drifting apart: the window
# class, the message name and the value struct have exactly one definition.
# The generated folder holds build_info.h. The mw2_build_number target writes it
# at build time, and it has already run by the time this project compiles.
target_include_directories(MW2Unlocker PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")

# comctl32 for the control window's dialog, dwmapi for the dark caption bar,
# uxtheme to ask for the dark variant of the edit controls, winhttp for the
# "Check for update" request, and shell32 to open the download page in the
# browser.
target_link_libraries(MW2Unlocker PRIVATE comctl32 dwmapi uxtheme winhttp shell32)

if(MSVC)
    target_link_options(MW2Unlocker PRIVATE /DYNAMICBASE /HIGHENTROPYVA)
endif()

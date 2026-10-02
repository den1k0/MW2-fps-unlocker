# Stamps the build number. Run from the build, not from configure, so every build
# is numbered - editing a source file does not re-run CMake, so a configure-time
# counter would hand the next binary the same number as the last one. That is the
# whole point of the counter: two binaries from the same afternoon must not both
# say "build 40".
#
# Called by the mw2_build_number target in CMakeLists.txt, which MW2Unlocker
# depends on, so the header exists and is current before the launcher compiles.
#
# Inputs, all -D:
#   MW2_SOURCE_DIR    the source tree (for the template and the git check)
#   MW2_BINARY_DIR    the build tree (counter and generated header live here)
#   MW2_BUILD_COMMITS the commit count, to seed a fresh tree
#   MW2_BUILD_HASH    the short commit hash, or "nogit"
#
# Outputs:
#   <binary>/build_number.txt   the counter, incremented
#   <binary>/generated/build_info.h

if(NOT MW2_SOURCE_DIR OR NOT MW2_BINARY_DIR)
    message(FATAL_ERROR
        "bump_build.cmake needs -DMW2_SOURCE_DIR= and -DMW2_BINARY_DIR=")
endif()

# The counter. It is seeded from the commit count once, and after that it only
# ever goes up - a corrupted or missing file falls back to the seed rather than
# starting at zero.
set(counter "${MW2_BINARY_DIR}/build_number.txt")
set(number "")
if(EXISTS "${counter}")
    file(READ "${counter}" number)
    string(STRIP "${number}" number)
endif()
if(NOT number MATCHES "^[0-9]+$")
    set(number "${MW2_BUILD_COMMITS}")
endif()
if(NOT number MATCHES "^[0-9]+$")
    set(number "0")
endif()

math(EXPR number "${number} + 1")
file(WRITE "${counter}" "${number}\n")

# A "+" whenever the tree has uncommitted changes, recomputed here rather than
# passed in: the configure that set MW2_BUILD_HASH may have been hours ago, and
# the point of the mark is that two different binaries cannot share a label.
set(hash "${MW2_BUILD_HASH}")
if(NOT hash)
    set(hash "nogit")
endif()
execute_process(COMMAND git status --porcelain
    WORKING_DIRECTORY "${MW2_SOURCE_DIR}"
    OUTPUT_VARIABLE dirty
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE gitResult)
if(gitResult EQUAL 0 AND NOT dirty STREQUAL "")
    set(hash "${hash}+")
endif()

string(TIMESTAMP date "%Y-%m-%d" UTC)

set(MW2_BUILD_NUMBER "${number}")
set(MW2_BUILD_DATE "${date}")
set(MW2_BUILD_TEXT "build ${number}   ${date}")
set(MW2_BUILD_LONG "build ${number} (${hash}, ${date})")

file(MAKE_DIRECTORY "${MW2_BINARY_DIR}/generated")
configure_file("${MW2_SOURCE_DIR}/launcher/build_info.h.in"
               "${MW2_BINARY_DIR}/generated/build_info.h"
               @ONLY)

message(STATUS "MW2 unlocker: ${MW2_BUILD_LONG}")

# Asserts the compiled template instance .so exists. The compile itself is
# enforced by the GameTemplate build target (an ABI break fails the build);
# this ctest entry keeps the full-suite run honest if the .so silently
# fails to link or the target was dropped.
if(NOT EXISTS "${INSTANCE_SO}")
    message(FATAL_ERROR "template instance not built: ${INSTANCE_SO}")
endif()
message(STATUS "template instance OK: ${INSTANCE_SO}")

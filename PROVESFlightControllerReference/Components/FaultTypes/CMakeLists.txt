####
# F Prime CMakeLists.txt:
#
# SOURCES: list of source files (to be compiled)
# AUTOCODER_INPUTS: list of files to be passed to the autocoders
# DEPENDS: list of libraries that this module depends on
#
# More information in the F´ CMake API documentation:
# https://fprime.jpl.nasa.gov/latest/docs/reference/api/cmake/API/
#
####

# Types only, with no dependencies of its own. This module is separate from
# FaultManager on purpose: the four producers (ModeManager, ThermalManager,
# Watchdog, AuthenticationRouter) need the FaultReport port type, while
# FaultManager needs ModeManager's ForceSafeModeWithReason. Keeping the types
# here is what stops that from being a dependency cycle between two modules.
register_fprime_library(
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/FaultTypes.fpp"
)

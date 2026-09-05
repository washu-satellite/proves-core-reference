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

# The shared fault types live in Components/FaultTypes so that the producers can
# depend on them without depending on this component, which itself depends on
# ModeManager for ForceSafeModeWithReason.
register_fprime_library(
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/FaultManager.fpp"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/FaultTable.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/FaultManager.cpp"
)

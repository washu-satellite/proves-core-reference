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

# The wire codec is a separate pure module (no fpp, so fpp-depend cannot
# discover it); RunInterval is header-only and resolves through the
# project-root include path.
register_fprime_library(
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/DriverBoardHandler.fpp"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/DriverBoardLink.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/DriverBoardHandler.cpp"
    DEPENDS
        PROVESFlightControllerReference_Components_DriverBoardProtocol
)

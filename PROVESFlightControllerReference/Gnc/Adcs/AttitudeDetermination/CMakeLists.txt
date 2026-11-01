####
# Adcs/AttitudeDetermination
####
register_fprime_module(
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/AttitudeDetermination.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/TriadSolver.cpp"
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/TriadTypes.fpp"
        "${CMAKE_CURRENT_LIST_DIR}/AttitudeDetermination.fpp"
    HEADERS
        "${CMAKE_CURRENT_LIST_DIR}/AttitudeDetermination.hpp"
        "${CMAKE_CURRENT_LIST_DIR}/TriadSolver.hpp"
    DEPENDS
        PROVESFlightControllerReference_Gnc_Types
        eigen_embedded
)

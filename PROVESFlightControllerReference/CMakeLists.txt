# This CMake file is intended to register project-wide objects.
# This allows for reuse between deployments, or other projects.

# Keep me first
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/project/config")
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/Components")
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/ComCcsdsUart/")
# S-band is not flown: the subtopology is not built (topology.fpp keeps its import commented out)
#add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/ComCcsdsSband/")
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/ComCcsdsLora/")
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/ReferenceDeployment/")

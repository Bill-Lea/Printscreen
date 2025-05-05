include("${CMAKE_CURRENT_LIST_DIR}/CommonLibSSE-targets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CommonLibSSE.cmake")
include(CMakeFindDependencyMacro)

find_dependency(fmt CONFIG)
find_dependency(spdlog CONFIG)

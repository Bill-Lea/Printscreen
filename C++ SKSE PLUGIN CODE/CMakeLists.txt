cmake_minimum_required(VERSION 3.21)
# Add this policy setting right after the minimum required version
cmake_policy(SET CMP0091 NEW)

project(PrintScreen VERSION 1.7.0 LANGUAGES CXX)

# Set C++ standard before anything else
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ========== Set Build Options ==========
# Use static runtime libraries across all components
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Setup output folder logic
if(DEFINED ENV{SKYRIM_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_FOLDER}/Data")
    set(OUTPUT_FOLDER "$ENV{SKYRIM_FOLDER}/Data")
endif()

if(DEFINED ENV{SKYRIM_MODS_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_MODS_FOLDER}")
    set(OUTPUT_FOLDER "$ENV{SKYRIM_MODS_FOLDER}/${PROJECT_NAME}")
endif()

# ========== Find Dependencies ==========
# Update the library name to match the vcpkg registry
find_package(CommonLibSSE NAMES CommonLibSSE CommonLibSSE-NG CONFIG REQUIRED)
find_package(directxtex CONFIG REQUIRED)
find_package(directxtk CONFIG REQUIRED)

# ========== Create Plugin ==========
# Modify this to match either approach depending on your CommonLibSSE version
if(TARGET CommonLibSSE::CommonLibSSE)
    add_commonlibsse_plugin(${PROJECT_NAME} 
        SOURCES plugin.cpp
    )
else()
    # Alternative approach if add_commonlibsse_plugin isn't available
    add_library(${PROJECT_NAME} SHARED plugin.cpp)
    target_include_directories(${PROJECT_NAME} PRIVATE ${CommonLibSSE_INCLUDE_DIRS})
endif()

# Use C++20 for better compatibility
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)

# MSVC-specific compiler options
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE 
        /EHsc
        /MP
        # Use static runtime library
        $<$<CONFIG:Debug>:/MTd>
        $<$<NOT:$<CONFIG:Debug>>:/MT>
    )
    
    # Separate link options from compile options
    target_link_options(${PROJECT_NAME} PRIVATE
        "/VERBOSE:LIB"  # This will show which libraries being searched
        # Resolve conflicts for static linking
        $<$<CONFIG:Debug>:/NODEFAULTLIB:MSVCRT>
        $<$<NOT:$<CONFIG:Debug>>:/NODEFAULTLIB:MSVCRTD>
        $<$<CONFIG:Debug>:/NODEFAULTLIB:MSVCPRT>
        $<$<NOT:$<CONFIG:Debug>>:/NODEFAULTLIB:MSVCPRTD>
    )
endif()

# Setup precompiled headers properly - avoid using static flags here
target_precompile_headers(${PROJECT_NAME} PRIVATE 
    PCH.h
)

# Ensure consistent precompiled header runtime library setting
set_target_properties(${PROJECT_NAME} PROPERTIES COMPILE_PDB_NAME "${PROJECT_NAME}")

# ========== Link Libraries ==========
# Use a more flexible approach to linking that works with different CommonLibSSE versions
# Add static linking preference
set(BUILD_SHARED_LIBS OFF)

if(TARGET CommonLibSSE::CommonLibSSE)
    target_link_libraries(${PROJECT_NAME} PRIVATE 
        CommonLibSSE::CommonLibSSE
        Microsoft::DirectXTex
        Microsoft::DirectXTK
        d3d11
        dxgi
        windowscodecs
        dxguid
    )
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE 
        ${CommonLibSSE_LIBRARIES}
        Microsoft::DirectXTex
        Microsoft::DirectXTK
        d3d11
        dxgi
        windowscodecs
        dxguid
    )
endif()

# ========== Define Preprocessor Macros ==========
target_compile_definitions(${PROJECT_NAME} PRIVATE 
    UNICODE 
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

# ========== Output and Deployment ==========
if(DEFINED OUTPUT_FOLDER)
    set(DLL_FOLDER "${OUTPUT_FOLDER}/SKSE/Plugins")
    
    # VS-friendly message
    message(STATUS "SKSE plugin will be copied to: ${DLL_FOLDER}")

    add_custom_command(
        TARGET "${PROJECT_NAME}"
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${DLL_FOLDER}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
        VERBATIM
    )

    # PDB for debug builds
    add_custom_command(
        TARGET "${PROJECT_NAME}"
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E $<$<CONFIG:Debug>:copy_if_different> "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_PDB_FILE_NAME:${PROJECT_NAME}>"
        VERBATIM
    )
endif()
cmake_minimum_required(VERSION 3.24)
project(PrintScreen VERSION 2.0.0 LANGUAGES CXX)

# Ensure we're building for x64
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This project requires x64 architecture")
endif()

# Force static linking of runtime libraries
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# policies...
cmake_policy(SET CMP0091 NEW)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

# C++ standard, build type
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# Find packages
find_package(CommonLibSSE CONFIG REQUIRED)
find_package(directxtex CONFIG REQUIRED)
find_package(directxtk CONFIG REQUIRED)

# plugin target
add_library(${PROJECT_NAME} SHARED
    plugin.cpp
    Config.cpp
    ScreenCapture.cpp 
    PapyrusInterface.cpp
    printscreen.rc
)

# precompiled headers
target_precompile_headers(${PROJECT_NAME} PRIVATE PCH.h)

# includes
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# IMPORTANT: Minimal linking to avoid conflicts
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        CommonLibSSE::CommonLibSSE
        Microsoft::DirectXTex
        Microsoft::DirectXTK
        d3d11
        dxgi
        dxguid
        windowscodecs
        ole32
        oleaut32
        uuid
        propsys
        comctl32
        kernel32
        user32
        gdi32
        winspool
        comdlg32
        advapi32
        shell32
        version
)

# MSVC compiler flags & definitions for CommonLibSSE-NG compatibility
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE 
        /W3  # Reduced warning level to avoid SKSE header warnings
        /permissive-
        /Zc:preprocessor
        /Zc:__cplusplus
        /bigobj
        /EHsc
    )
    
    # CRITICAL: Minimal definitions to avoid conflicts
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        _UNICODE 
        UNICODE 
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )
endif()

# CommonLibSSE-NG specific definitions
target_compile_definitions(${PROJECT_NAME} PRIVATE
    SKSE_SUPPORT_XBYAK=1
)

# Auto-deployment to mod folder
if(DEFINED ENV{SKYRIM_MODS_FOLDER})
    if(IS_DIRECTORY "$ENV{SKYRIM_MODS_FOLDER}")
        set(OUTPUT_FOLDER "$ENV{SKYRIM_MODS_FOLDER}/${PROJECT_NAME}")
        set(DLL_FOLDER "${OUTPUT_FOLDER}/SKSE/Plugins")
        message(STATUS "Will deploy to: ${DLL_FOLDER}")
        
        add_custom_command(
            TARGET "${PROJECT_NAME}"
            POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${DLL_FOLDER}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
            COMMAND "${CMAKE_COMMAND}" -E echo "Deployed DLL to ${DLL_FOLDER}"
            VERBATIM
        )
        
        # Copy PDB files for debug builds
        if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
            add_custom_command(
                TARGET "${PROJECT_NAME}"
                POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_PDB_FILE_NAME:${PROJECT_NAME}>"
                VERBATIM
            )
        endif()
    endif()
else()
    # Try common Skyrim installation paths
    set(POSSIBLE_SKYRIM_PATHS
        "C:/Program Files (x86)/Steam/steamapps/common/Skyrim Special Edition"
        "C:/Program Files/Steam/steamapps/common/Skyrim Special Edition"
        "C:/Games/Steam/steamapps/common/Skyrim Special Edition"
        "C:/SteamLibrary/steamapps/common/Skyrim Special Edition"
    )
    
    foreach(SKYRIM_PATH ${POSSIBLE_SKYRIM_PATHS})
        if(IS_DIRECTORY "${SKYRIM_PATH}")
            set(SKYRIM_DATA_FOLDER "${SKYRIM_PATH}/Data")
            if(IS_DIRECTORY "${SKYRIM_DATA_FOLDER}")
                set(DLL_FOLDER "${SKYRIM_DATA_FOLDER}/SKSE/Plugins")
                message(STATUS "Found Skyrim: ${SKYRIM_PATH}")
                message(STATUS "Auto-deploying to: ${DLL_FOLDER}")
                
                add_custom_command(
                    TARGET "${PROJECT_NAME}"
                    POST_BUILD
                    COMMAND "${CMAKE_COMMAND}" -E make_directory "${DLL_FOLDER}"
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
                    VERBATIM
                )
                break()
            endif()
        endif()
    endforeach()
endif()

# Print configuration info
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "CommonLibSSE found: ${CommonLibSSE_FOUND}")
message(STATUS "DirectXTex found: ${directxtex_FOUND}")
message(STATUS "DirectXTK found: ${directxtk_FOUND}")

# Add debug information about libraries
get_target_property(LINKED_LIBS ${PROJECT_NAME} LINK_LIBRARIES)
message(STATUS "Linked libraries: ${LINKED_LIBS}")

# Add compile definitions debug info
get_target_property(COMPILE_DEFS ${PROJECT_NAME} COMPILE_DEFINITIONS)
message(STATUS "Compile definitions: ${COMPILE_DEFS}")

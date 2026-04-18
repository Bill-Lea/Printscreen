# src/CMakeLists.txt

cmake_minimum_required(VERSION 3.25)

# -------------------------
# Options / user knobs
# -------------------------
set(PLUGIN_TARGET "Printscreen" CACHE STRING "SKSE plugin DLL name (no prefix)")
set(MO2_MOD_DIR "C:/Modding/Mo2/Mods/Printscreen" CACHE PATH "MO2 mod root for deployment")
set(MO2_SKSE_PLUGIN_DIR "${MO2_MOD_DIR}/SKSE/Plugins" CACHE PATH "MO2 SKSE/Plugins deployment path")
set(PLUGIN_TOML "Printscreen.toml" CACHE STRING "Plugin .toml to deploy (relative to repo root)")
set(PLUGIN_DB_FILES "" CACHE STRING "Semicolon-separated list of DB files to copy (relative to repo root)")
set(PLUGIN_DB_GLOB "*.db" CACHE STRING "Glob for DB files to copy if PLUGIN_DB_FILES is empty")
option(PRINTSCREEN_WITH_LODEPNG "Link to lodepng if available" ON)

# -------------------------
# Language / compiler flags
# -------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(
    /permissive-
    /Zc:__cplusplus
    /Zc:preprocessor
    /Zc:externConstexpr
    /EHsc
    /MP
    /W4
    /utf-8
    /wd4100 /wd4189 /wd4324
  )
endif()

add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -DUNICODE -D_UNICODE -D_CRT_SECURE_NO_WARNINGS)

# -------------------------
# Dependencies (via vcpkg)
# -------------------------
# Your Presets wire the vcpkg toolchain & x64 triplet already.  (CMakePresets.json)
# And your registries include colorglass for commonlibsse-ng.     (vcpkg-configuration.json)
find_package(CommonLibSSE CONFIG REQUIRED)      # CommonLibSSE::CommonLibSSE
find_package(directxtex CONFIG REQUIRED)        # Microsoft::DirectXTex
find_package(directxtk  CONFIG REQUIRED)        # Microsoft::DirectXTK

# Optional PNG (APNG enabled by your vcpkg.json features)
find_package(PNG QUIET)                         # PNG::PNG (optional)
if(PRINTSCREEN_WITH_LODEPNG)
  find_package(lodepng QUIET)
endif()

# -------------------------
# Sources
# -------------------------
set(PROJ_ROOT "${CMAKE_CURRENT_LIST_DIR}/..") # repo root (parent of src)

file(GLOB_RECURSE PRINTSCREEN_SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/*.cc"
  "${CMAKE_CURRENT_LIST_DIR}/*.cxx"
)
# Exclude backup/patched files from build
list(FILTER PRINTSCREEN_SOURCES EXCLUDE REGEX ".*_patched\\.cpp$")
file(GLOB_RECURSE PRINTSCREEN_HEADERS
  "${CMAKE_CURRENT_LIST_DIR}/*.h"
  "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
)

# set(RESOURCES "${CMAKE_CURRENT_LIST_DIR}/version.rc") # optional

# -------------------------
# Target
# -------------------------
add_library(${PLUGIN_TARGET} SHARED
  ${PRINTSCREEN_SOURCES}
  ${PRINTSCREEN_HEADERS}
  ${RESOURCES}
)

# Local include paths: src/ and (optionally) repo root for pch.h or shared headers
target_include_directories(${PLUGIN_TARGET} PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}"
  "${PROJ_ROOT}"
)

# Prefer a local pch.h at repo root, else use SKSE’s PCH
if(EXISTS "${PROJ_ROOT}/pch.h")
  message(STATUS "Using project PCH: ${PROJ_ROOT}/pch.h")
  target_precompile_headers(${PLUGIN_TARGET} PRIVATE "${PROJ_ROOT}/pch.h")
else()
  message(STATUS "Using SKSE PCH: <SKSE/Impl/PCH.h>")
  target_precompile_headers(${PLUGIN_TARGET} PRIVATE <SKSE/Impl/PCH.h>)
endif()

# -------------------------
# Find Windows SDK libraries for x64
# -------------------------
# Force x64 architecture for Windows SDK libraries
# Always use x64 for this project regardless of CMAKE_SIZEOF_VOID_P
set(_WIN_SDK_ARCH "x64")

# Also set up MSVC x64 library path to avoid x86/x64 mismatch
if(MSVC)
  # Get MSVC version directory
  get_filename_component(_MSVC_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
  get_filename_component(_MSVC_DIR "${_MSVC_DIR}" DIRECTORY)  # go up from bin/Hostx64/x64
  get_filename_component(_MSVC_DIR "${_MSVC_DIR}" DIRECTORY)  # go up from bin/Hostx64
  get_filename_component(_MSVC_DIR "${_MSVC_DIR}" DIRECTORY)  # go up from bin
  set(_MSVC_LIB_PATH "${_MSVC_DIR}/lib/x64")
  if(EXISTS "${_MSVC_LIB_PATH}")
    message(STATUS "MSVC x64 Library Path: ${_MSVC_LIB_PATH}")
    link_directories("${_MSVC_LIB_PATH}")
  endif()
endif()

# Get Windows SDK path from environment or use default
if(DEFINED ENV{WindowsSdkDir})
  set(_WIN_SDK_DIR "$ENV{WindowsSdkDir}")
else()
  set(_WIN_SDK_DIR "C:/Program Files (x86)/Windows Kits/10")
endif()

# Get Windows SDK version
if(DEFINED ENV{WindowsSDKVersion})
  string(REGEX REPLACE "\\\\$" "" _WIN_SDK_VER "$ENV{WindowsSDKVersion}")
else()
  # Try to find the latest version
  file(GLOB _SDK_VERSIONS "${_WIN_SDK_DIR}/Lib/10.*")
  if(_SDK_VERSIONS)
    list(SORT _SDK_VERSIONS)
    list(GET _SDK_VERSIONS -1 _LATEST_SDK)
    get_filename_component(_WIN_SDK_VER "${_LATEST_SDK}" NAME)
  else()
    set(_WIN_SDK_VER "10.0.22621.0")
  endif()
endif()

set(_WIN_SDK_LIB_PATH "${_WIN_SDK_DIR}/Lib/${_WIN_SDK_VER}/um/${_WIN_SDK_ARCH}")
set(_WIN_SDK_UCRT_PATH "${_WIN_SDK_DIR}/Lib/${_WIN_SDK_VER}/ucrt/${_WIN_SDK_ARCH}")

message(STATUS "Windows SDK Library Path: ${_WIN_SDK_LIB_PATH}")
message(STATUS "Windows SDK UCRT Path: ${_WIN_SDK_UCRT_PATH}")

# Add Windows SDK x64 library directories explicitly to ensure x64 libs are found first
if(EXISTS "${_WIN_SDK_LIB_PATH}")
  link_directories("${_WIN_SDK_LIB_PATH}")
endif()
if(EXISTS "${_WIN_SDK_UCRT_PATH}")
  link_directories("${_WIN_SDK_UCRT_PATH}")
endif()

# Find Windows SDK libraries explicitly for correct architecture (x64)
find_library(WINDOWSCODECS_LIB windowscodecs PATHS "${_WIN_SDK_LIB_PATH}" NO_DEFAULT_PATH)
find_library(OLE32_LIB ole32 PATHS "${_WIN_SDK_LIB_PATH}" NO_DEFAULT_PATH)
find_library(D3D11_LIB d3d11 PATHS "${_WIN_SDK_LIB_PATH}" NO_DEFAULT_PATH)
find_library(DXGI_LIB dxgi PATHS "${_WIN_SDK_LIB_PATH}" NO_DEFAULT_PATH)
find_library(PROPSYS_LIB propsys PATHS "${_WIN_SDK_LIB_PATH}" NO_DEFAULT_PATH)

# Find additional Windows SDK libraries that CMake defaults might get wrong
find_library(VERSION_LIB version PATHS "${_WIN_SDK_LIB_PATH}" NO_DEFAULT_PATH)
find_library(KERNEL32_LIB kernel32 PATHS "${_WIN_SDK_LIB_PATH}" NO_DEFAULT_PATH)
find_library(USER32_LIB user32 PATHS "${_WIN_SDK_LIB_PATH}" NO_DEFAULT_PATH)
find_library(GDI32_LIB gdi32 PATHS "${_WIN_SDK_LIB_PATH}" NO_DEFAULT_PATH)
find_library(WINSPOOL_LIB winspool PATHS "${_WIN_SDK_LIB_PATH}" NO_DEFAULT_PATH)
find_library(SHELL32_LIB shell32 PATHS "${_WIN_SDK_LIB_PATH}" NO_DEFAULT_PATH)
find_library(OLEAUT32_LIB oleaut32 PATHS "${_WIN_SDK_LIB_PATH}" NO_DEFAULT_PATH)
find_library(UUID_LIB uuid PATHS "${_WIN_SDK_LIB_PATH}" NO_DEFAULT_PATH)
find_library(COMDLG32_LIB comdlg32 PATHS "${_WIN_SDK_LIB_PATH}" NO_DEFAULT_PATH)
find_library(ADVAPI32_LIB advapi32 PATHS "${_WIN_SDK_LIB_PATH}" NO_DEFAULT_PATH)

# Fallback to letting CMake find them if explicit search fails
if(NOT WINDOWSCODECS_LIB)
  set(WINDOWSCODECS_LIB windowscodecs)
endif()
if(NOT OLE32_LIB)
  set(OLE32_LIB ole32)
endif()
if(NOT D3D11_LIB)
  set(D3D11_LIB d3d11)
endif()
if(NOT DXGI_LIB)
  set(DXGI_LIB dxgi)
endif()
if(NOT PROPSYS_LIB)
  set(PROPSYS_LIB propsys)
endif()
if(NOT VERSION_LIB)
  set(VERSION_LIB version)
endif()
if(NOT KERNEL32_LIB)
  set(KERNEL32_LIB kernel32)
endif()
if(NOT USER32_LIB)
  set(USER32_LIB user32)
endif()
if(NOT GDI32_LIB)
  set(GDI32_LIB gdi32)
endif()
if(NOT WINSPOOL_LIB)
  set(WINSPOOL_LIB winspool)
endif()
if(NOT SHELL32_LIB)
  set(SHELL32_LIB shell32)
endif()
if(NOT OLEAUT32_LIB)
  set(OLEAUT32_LIB oleaut32)
endif()
if(NOT UUID_LIB)
  set(UUID_LIB uuid)
endif()
if(NOT COMDLG32_LIB)
  set(COMDLG32_LIB comdlg32)
endif()
if(NOT ADVAPI32_LIB)
  set(ADVAPI32_LIB advapi32)
endif()

message(STATUS "WindowsCodecs: ${WINDOWSCODECS_LIB}")
message(STATUS "Ole32: ${OLE32_LIB}")
message(STATUS "D3D11: ${D3D11_LIB}")
message(STATUS "DXGI: ${DXGI_LIB}")
message(STATUS "Version: ${VERSION_LIB}")
message(STATUS "Kernel32: ${KERNEL32_LIB}")

# Link libs (imported targets also propagate their own include dirs)
# Use explicitly found x64 Windows SDK libraries to avoid x86/x64 mismatch
target_link_libraries(${PLUGIN_TARGET} PRIVATE
  CommonLibSSE::CommonLibSSE
  Microsoft::DirectXTex
  Microsoft::DirectXTK
  ${WINDOWSCODECS_LIB}
  ${OLE32_LIB}
  ${D3D11_LIB}
  ${DXGI_LIB}
  ${PROPSYS_LIB}
  ${VERSION_LIB}
  ${KERNEL32_LIB}
  ${USER32_LIB}
  ${GDI32_LIB}
  ${WINSPOOL_LIB}
  ${SHELL32_LIB}
  ${OLEAUT32_LIB}
  ${UUID_LIB}
  ${COMDLG32_LIB}
  ${ADVAPI32_LIB}
  $<$<BOOL:${PNG_FOUND}>:PNG::PNG>
  $<$<TARGET_EXISTS:lodepng>:lodepng>
)

# --- Robust fallback include for MSVC external headers quirk (optional) ---
# If your environment still "hides" headers like RE/B/BSScript/...,
# this adds the raw vcpkg include folder explicitly.
set(VCPKG_INSTALLED_DIR "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}")
if(EXISTS "${VCPKG_INSTALLED_DIR}/include")
  target_include_directories(${PLUGIN_TARGET} PRIVATE "${VCPKG_INSTALLED_DIR}/include")
else()
  if(EXISTS "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/include")
    target_include_directories(${PLUGIN_TARGET} PRIVATE "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/include")
  endif()
endif()
# If problems persist, consider disabling external headers for this target:
# if(MSVC)
#   target_compile_options(${PLUGIN_TARGET} PRIVATE /experimental:external-)
# endif()

# Ensure DLL has no "lib" prefix with non-MSVC generators
set_target_properties(${PLUGIN_TARGET} PROPERTIES
  OUTPUT_NAME "${PLUGIN_TARGET}"
  PREFIX ""
)

# Place runtime outputs into build/bin for easy discovery
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set_target_properties(${PLUGIN_TARGET} PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY_DEBUG          "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
  RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL     "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)

# -------------------------
# Post-build: copy to MO2
# -------------------------
if(MO2_MOD_DIR AND MO2_SKSE_PLUGIN_DIR)
  add_custom_command(TARGET ${PLUGIN_TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MO2_SKSE_PLUGIN_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:${PLUGIN_TARGET}>"
            "${MO2_SKSE_PLUGIN_DIR}/$<TARGET_FILE_NAME:${PLUGIN_TARGET}>"
  )

  if(PLUGIN_TOML AND EXISTS "${PROJ_ROOT}/${PLUGIN_TOML}")
    add_custom_command(TARGET ${PLUGIN_TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${PROJ_ROOT}/${PLUGIN_TOML}"
              "${MO2_SKSE_PLUGIN_DIR}/${PLUGIN_TOML}"
    )
  endif()

  if(PLUGIN_DB_FILES)
    separate_arguments(_DB_LIST NATIVE_COMMAND "${PLUGIN_DB_FILES}")
    foreach(_DB IN LISTS _DB_LIST)
      if(EXISTS "${PROJ_ROOT}/${_DB}")
        add_custom_command(TARGET ${PLUGIN_TARGET} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
                  "${PROJ_ROOT}/${_DB}"
                  "${MO2_SKSE_PLUGIN_DIR}/${_DB}"
        )
      endif()
    endforeach()
  else()
    file(GLOB _DB_GLOB "${PROJ_ROOT}/${PLUGIN_DB_GLOB}")
    foreach(_DBPATH IN LISTS _DB_GLOB)
      get_filename_component(_DBNAME "${_DBPATH}" NAME)
      add_custom_command(TARGET ${PLUGIN_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_DBPATH}"
                "${MO2_SKSE_PLUGIN_DIR}/${_DBNAME}"
      )
    endforeach()
  endif()
endif()

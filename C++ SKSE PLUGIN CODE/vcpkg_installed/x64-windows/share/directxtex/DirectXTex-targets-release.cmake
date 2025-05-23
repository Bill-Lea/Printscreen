#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Microsoft::DirectXTex" for configuration "Release"
set_property(TARGET Microsoft::DirectXTex APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(Microsoft::DirectXTex PROPERTIES
  IMPORTED_IMPLIB_RELEASE "${_IMPORT_PREFIX}/lib/DirectXTex.lib"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/DirectXTex.dll"
  )

list(APPEND _cmake_import_check_targets Microsoft::DirectXTex )
list(APPEND _cmake_import_check_files_for_Microsoft::DirectXTex "${_IMPORT_PREFIX}/lib/DirectXTex.lib" "${_IMPORT_PREFIX}/bin/DirectXTex.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)

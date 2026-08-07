find_path(glad_INCLUDE_DIR
  NAMES glad/glad.h
  PATH_SUFFIXES include
)

find_file(glad_SOURCE
  NAMES gl.c glad.c
  PATH_SUFFIXES src
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(glad REQUIRED_VARS glad_INCLUDE_DIR)

if(glad_FOUND AND NOT TARGET glad::glad)
  if(glad_SOURCE)
    set_source_files_properties("${glad_SOURCE}" PROPERTIES LANGUAGE CXX)
    add_library(glad STATIC "${glad_SOURCE}")
    target_include_directories(glad PUBLIC "${glad_INCLUDE_DIR}")
    target_compile_definitions(glad INTERFACE OPENMVS_GLAD_EXTERNAL)
    add_library(glad::glad ALIAS glad)
  else()
    add_library(glad::glad INTERFACE IMPORTED)
    set_target_properties(glad::glad PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${glad_INCLUDE_DIR}"
    )
  endif()
endif()

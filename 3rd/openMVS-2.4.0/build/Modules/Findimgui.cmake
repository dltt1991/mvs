find_path(imgui_INCLUDE_DIR
  NAMES imgui.h
  PATH_SUFFIXES include imgui
)

find_path(imgui_BACKENDS_DIR
  NAMES imgui_impl_glfw.h imgui_impl_opengl3.h
  PATH_SUFFIXES backends imgui/backends
)

find_file(imgui_SOURCE
  NAMES imgui.cpp
  PATHS "${imgui_INCLUDE_DIR}"
  NO_DEFAULT_PATH
)

find_path(imgui_GLFW_INCLUDE_DIR
  NAMES GLFW/glfw3.h
  PATH_SUFFIXES include
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(imgui
  REQUIRED_VARS imgui_INCLUDE_DIR imgui_BACKENDS_DIR imgui_SOURCE imgui_GLFW_INCLUDE_DIR
)

if(imgui_FOUND AND NOT TARGET imgui::imgui)
  get_filename_component(imgui_SOURCE_DIR "${imgui_SOURCE}" DIRECTORY)
  add_library(imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp"
  )
  target_include_directories(imgui PUBLIC
    "${imgui_INCLUDE_DIR}"
    "${imgui_BACKENDS_DIR}"
    "${imgui_GLFW_INCLUDE_DIR}"
  )
  add_library(imgui::imgui ALIAS imgui)
endif()

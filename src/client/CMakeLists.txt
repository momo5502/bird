file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS
  *.cpp
  *.hpp
  *.rc
)

list(SORT SRC_FILES)

add_executable(client ${SRC_FILES})

target_precompile_headers(client PRIVATE std_include.hpp)

target_link_libraries(client PRIVATE
  common
  glm
  glfw
  libglew_static
  proto
  stb
  crn
)

set_target_properties(client PROPERTIES OUTPUT_NAME "bird")

set_property(GLOBAL PROPERTY VS_STARTUP_PROJECT client)

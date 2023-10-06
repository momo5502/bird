add_subdirectory(common)
add_subdirectory(proto)

if (MSVC)
  add_subdirectory(client)
endif()

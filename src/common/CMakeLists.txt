file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS
  *.cpp
  *.hpp
)

add_library(common ${SRC_FILES})

momo_assign_source_group(${SRC_FILES})

target_link_libraries(common PRIVATE CURL::libcurl_static)

target_include_directories(common INTERFACE "${CMAKE_CURRENT_LIST_DIR}")

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(common PUBLIC
  Threads::Threads
  libtomcrypt
  libtommath
)

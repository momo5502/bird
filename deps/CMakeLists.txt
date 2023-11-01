include(${CMAKE_CURRENT_LIST_DIR}/cmrc/CMakeRC.cmake)

##########################################

add_subdirectory(glm)
add_subdirectory(glfw)
add_subdirectory(crn)

##########################################

if(NOT MSVC)
  option(GEN_FILES "" ON)
  option(ENABLE_TESTING "" OFF)
  option(ENABLE_PROGRAMS "" OFF)
  add_subdirectory(mbedtls)
endif()

##########################################

option(glew-cmake_BUILD_SHARED "" OFF)
add_subdirectory(glew-cmake)

##########################################

option(HTTP_ONLY "" ON)
option(BUILD_CURL_EXE "" OFF)
option(BUILD_SHARED_LIBS "" OFF)
option(BUILD_STATIC_LIBS "" ON)
option(CURL_ENABLE_EXPORT_TARGET "" OFF)

if(MSVC)
  set(CURL_USE_SCHANNEL ON)
else()
  set(CURL_USE_OPENSSL OFF)
  set(SSL_ENABLED ON)
  set(USE_MBEDTLS ON)
  list(APPEND CURL_LIBS ${MBEDTLS_LIBRARIES})

  get_target_property(MBEDTLS_INCLUDE_DIRS mbedtls INCLUDE_DIRECTORIES)
  include_directories(${MBEDTLS_INCLUDE_DIRS})
endif()

add_subdirectory(curl)

if(NOT MSVC)
  target_link_libraries(libcurl_static PRIVATE
    mbedtls
  )
endif()

##########################################

if(${MOMO_ENABLE_SANITIZER})
  option(protobuf_MSVC_STATIC_RUNTIME "" OFF)
else()
  option(protobuf_MSVC_STATIC_RUNTIME "" ON)
endif()

option(protobuf_BUILD_TESTS "" OFF)
option(protobuf_BUILD_PROTOBUF_BINARIES "" ON)
option(protobuf_BUILD_SHARED_LIBS "" OFF)
option(ABSL_PROPAGATE_CXX_STD "" ON)
add_subdirectory(protobuf)

##########################################

option(FT_DISABLE_ZLIB "" ON)
option(FT_DISABLE_BZIP2 "" ON)
option(FT_DISABLE_PNG "" ON)
option(FT_DISABLE_HARFBUZZ "" ON)
option(FT_DISABLE_BROTLI "" ON)

add_subdirectory(freetype)

##########################################

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/stb"
)

##########################################

add_library(xxHash
  "${CMAKE_CURRENT_LIST_DIR}/xxHash/xxhash.c"
)
target_include_directories(xxHash INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/xxHash"
)

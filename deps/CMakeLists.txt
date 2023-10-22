add_subdirectory(glm)
add_subdirectory(glfw)
add_subdirectory(crn)

##########################################

option(glew-cmake_BUILD_SHARED "" OFF)
add_subdirectory(glew-cmake)

##########################################

option(HTTP_ONLY "" ON)
option(BUILD_CURL_EXE "" OFF)
option(BUILD_SHARED_LIBS "" OFF)
option(BUILD_STATIC_LIBS "" ON)

if(MSVC)
  set(CURL_USE_SCHANNEL ON)
else()
  set(CURL_USE_OPENSSL ON)
endif()

add_subdirectory(curl)

##########################################

option(protobuf_BUILD_TESTS "" OFF)
option(protobuf_BUILD_PROTOBUF_BINARIES "" ON)
option(protobuf_BUILD_SHARED_LIBS "" OFF)
option(ABSL_PROPAGATE_CXX_STD "" ON)
add_subdirectory(protobuf)

##########################################

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/stb"
)

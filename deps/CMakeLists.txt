add_subdirectory(glm)
add_subdirectory(glfw)
add_subdirectory(glew-cmake)
add_subdirectory(crn)

##########################################

option(HTTP_ONLY "" ON)
option(BUILD_CURL_EXE "" OFF)
option(BUILD_SHARED_LIBS "" OFF)
option(BUILD_STATIC_LIBS "" ON)
set(CURL_USE_SCHANNEL ON)
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

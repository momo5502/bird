add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rocktree.pb.h" "${CMAKE_CURRENT_BINARY_DIR}/rocktree.pb.cc"
    COMMAND protoc --cpp_out="${CMAKE_CURRENT_BINARY_DIR}" --proto_path="${CMAKE_CURRENT_LIST_DIR}" "${CMAKE_CURRENT_LIST_DIR}/rocktree.proto"
    DEPENDS protobuf::protoc "${CMAKE_CURRENT_LIST_DIR}/rocktree.proto"
)

add_library(proto
    "${CMAKE_CURRENT_BINARY_DIR}/rocktree.pb.h"
    "${CMAKE_CURRENT_BINARY_DIR}/rocktree.pb.cc"
)

target_link_libraries(proto PUBLIC
    protobuf::libprotobuf
)

target_include_directories(proto INTERFACE
    "${CMAKE_CURRENT_BINARY_DIR}"
)

momo_target_disable_warnings(proto)

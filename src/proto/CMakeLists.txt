add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rocktree.pb.h"
    COMMAND protoc --cpp_out="${CMAKE_CURRENT_BINARY_DIR}" --proto_path="${CMAKE_CURRENT_LIST_DIR}" "${CMAKE_CURRENT_LIST_DIR}/rocktree.proto"
    DEPENDS protobuf::protoc "${CMAKE_CURRENT_LIST_DIR}/rocktree.proto"
)

add_library(proto INTERFACE
    "${CMAKE_CURRENT_BINARY_DIR}/rocktree.pb.h"
)

target_link_libraries(proto INTERFACE
    protobuf::libprotobuf
)

target_include_directories(proto INTERFACE
    "${CMAKE_CURRENT_BINARY_DIR}"
)

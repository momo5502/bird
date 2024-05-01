add_library(crn
    crn.cc
)

target_include_directories(crn INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}"
)

# tinyrpc 测试
# 可执行文件输出到 bin/tinyrpc，与 tests/tinyrpc 目录结构保持一致
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin/tinyrpc)

# 客户端测试
# 注意：test_rpc_service.cc 目前是空文件（服务端尚未实现），暂不参与构建，
#       实现后再在此处添加 add_executable 即可
add_executable(test_rpc_client
    test_rpc_client.cc
    echo_add.poto.pb.cc
    ${PROJECT_SOURCE_DIR}/sylar/tinyrpc/rpc_channel_client.cc
    ${PROJECT_SOURCE_DIR}/sylar/tinyrpc/rpc_connection.cc
)
target_link_libraries(test_rpc_client sylar protobuf yaml-cpp dl pthread)

add_executable(test_rpc_service
    test_rpc_service.cc
    echo_add.poto.pb.cc
    ${PROJECT_SOURCE_DIR}/sylar/tinyrpc/rpc_channel_client.cc
    ${PROJECT_SOURCE_DIR}/sylar/tinyrpc/rpc_connection.cc
)
target_link_libraries(test_rpc_service sylar protobuf yaml-cpp dl pthread)

# 性能测试：先启动服务端 test_rpc_service，再运行 test_rpc_perf
add_executable(test_rpc_perf
    test_rpc_perf.cc
    echo_add.poto.pb.cc
    ${PROJECT_SOURCE_DIR}/sylar/tinyrpc/rpc_channel_client.cc
    ${PROJECT_SOURCE_DIR}/sylar/tinyrpc/rpc_connection.cc
)
target_link_libraries(test_rpc_perf sylar protobuf yaml-cpp dl pthread)

# sylar/rpc 二进制帧协议对照性能测试
add_executable(test_rpc_bin_server
    test_rpc_bin_server.cc
    echo_add.poto.pb.cc
)
target_link_libraries(test_rpc_bin_server sylar protobuf yaml-cpp dl pthread)

add_executable(test_rpc_bin_perf
    test_rpc_bin_perf.cc
    echo_add.poto.pb.cc
)
target_link_libraries(test_rpc_bin_perf sylar protobuf yaml-cpp dl pthread)

# 可执行文件输出到 bin/rpc，与 tests/rpc 目录结构保持一致
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin/rpc)

include_directories(${PROJECT_SOURCE_DIR}/sylar)
set(LISTCALLER calluserservice.cc user.pb.cc)
set(LISTPROVIDER userservice.cc user.pb.cc)

add_executable(caller ${LISTCALLER})
add_executable(provider ${LISTPROVIDER})
target_link_libraries(caller sylar protobuf yaml-cpp zookeeper_mt)
target_link_libraries(provider sylar protobuf yaml-cpp zookeeper_mt dl)
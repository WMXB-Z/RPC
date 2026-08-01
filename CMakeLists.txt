# 顶层 CMakeLists.txt

cmake_minimum_required(VERSION 3.16)

project(sylar-from-suycx LANGUAGES C CXX)

# 开启生成 compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/utils.cmake)

# 默认使用 Debug 构建
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

# 指定 C++ 标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 公共编译选项
# -rdynamic: 将所有符号都加入到符号表中，便于使用dlopen或者backtrace追踪到符号
# -fPIC: 生成位置无关的代码，便于动态链接
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -rdynamic -fPIC -Wall -Werror")

# -Wno-unused-function: 不要警告未使用函数
# -Wno-builtin-macro-redefined: 不要警告内置宏重定义，用于重定义内置的__FILE__宏
# -Wno-deprecated: 不要警告过时的特性
# -Wno-deprecated-declarations: 不要警告使用带deprecated属性的变量，类型，函数
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-function -Wno-builtin-macro-redefined -Wno-deprecated -Wno-deprecated-declarations")

# 调试/发布版本各自的优化选项
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -ggdb")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2 -DNDEBUG")

include_directories(
    ${PROJECT_SOURCE_DIR}
    /usr/local/include
)

option(BUILD_TEST "构建测试程序" ON)

# 源文件列表
set(LIB_SRC
    sylar/log.cpp
    sylar/util.cpp
    sylar/mutex.cc
    sylar/env.cc
    sylar/config.cc
    sylar/thread.cc
    sylar/fiber.cc
    sylar/scheduler.cc
    sylar/iomanager.cc
    sylar/timer.cc
    sylar/fd_manager.cc
    sylar/hook.cc
    sylar/address.cc
    sylar/socket.cc
    sylar/bytearray.cc
    sylar/tcp_server.cc
    sylar/http/http-parser/http_parser.c
    sylar/http/http.cc
    sylar/http/http_parser.cc
    sylar/stream.cc
    sylar/streams/socket_stream.cc
    sylar/http/http_session.cc
    sylar/http/servlet.cc
    sylar/http/http_server.cc
    sylar/uri.cc
    sylar/http/http_connection.cc
    sylar/daemon.cc
    sylar/rpc/rpcconfig.cc
    sylar/rpc/rpccontroller.cc
    sylar/rpc/rpcheader.pb.cc
    sylar/rpc/rpcchannel.cc
    sylar/rpc/rpcapplication.cc
    sylar/rpc/zookeeperutil.cc
    sylar/rpc/rpcprovider.cc
)

# 创建动态库
add_library(sylar SHARED ${LIB_SRC})
# 指明 sylar 所依赖的库（链接 libsylar 时会一并解析这些库中的符号）
target_link_libraries(sylar PRIVATE yaml-cpp zookeeper_mt dl protobuf pthread)
# 重新定义 __FILE__ 为相对路径，避免输出过程暴露绝对路径
force_redefine_file_macro_for_sources(sylar)

# 测试程序需要链接的库
set(LIBS
    sylar
    pthread
    dl
    yaml-cpp
    protobuf
    zookeeper_mt
)

if(BUILD_TEST)
    set(DIR ${PROJECT_SOURCE_DIR}/tests)
    sylar_add_executable(test_log "${DIR}/test_log.cpp" sylar "${LIBS}")
    sylar_add_executable(test_util "${DIR}/test_util.cpp" sylar "${LIBS}")
    sylar_add_executable(test_env "${DIR}/test_env.cc" sylar "${LIBS}")
    sylar_add_executable(test_config "${DIR}/test_config.cc" sylar "${LIBS}")
    sylar_add_executable(test_thread "${DIR}/test_thread.cc" sylar "${LIBS}")
    sylar_add_executable(test_fiber "${DIR}/test_fiber.cc" sylar "${LIBS}")
    sylar_add_executable(test_fiber2 "${DIR}/test_fiber2.cc" sylar "${LIBS}")
    sylar_add_executable(test_fiber_swap "${DIR}/test_fiber_swap.cc" sylar "${LIBS}")
    sylar_add_executable(test_thread_swap "${DIR}/test_thread_swap.cc" sylar "${LIBS}")
    sylar_add_executable(test_scheduler "${DIR}/test_scheduler.cc" sylar "${LIBS}")
    sylar_add_executable(test_iomanager "${DIR}/test_iomanager.cc" sylar "${LIBS}")
    sylar_add_executable(test_timer "${DIR}/test_timer.cc" sylar "${LIBS}")
    sylar_add_executable(test_hook "${DIR}/test_hook.cc" sylar "${LIBS}")
    sylar_add_executable(test_address "${DIR}/test_address.cc" sylar "${LIBS}")
    sylar_add_executable(test_socket_tcp_server "${DIR}/test_socket_tcp_server.cc" sylar "${LIBS}")
    sylar_add_executable(test_socket_tcp_client "${DIR}/test_socket_tcp_client.cc" sylar "${LIBS}")
    sylar_add_executable(test_bytearray "${DIR}/test_bytearray.cc" sylar "${LIBS}")
    sylar_add_executable(test_tcp_server "${DIR}/test_tcp_server.cc" sylar "${LIBS}")
    sylar_add_executable(test_http "${DIR}/test_http.cc" sylar "${LIBS}")
    sylar_add_executable(test_http_parser "${DIR}/test_http_parser.cc" sylar "${LIBS}")
    sylar_add_executable(test_http_server "${DIR}/test_http_server.cc" sylar "${LIBS}")
    sylar_add_executable(test_tcpserver_passure "${DIR}/test_tcpserver_passure.cc" sylar "${LIBS}")
    sylar_add_executable(test_tcpserver_bench "${DIR}/test_tcpserver_bench.cc" sylar "${LIBS}")
    sylar_add_executable(test_uri "${DIR}/test_uri.cc" sylar "${LIBS}")
    sylar_add_executable(test_http_connection "${DIR}/test_http_connection.cc" sylar "${LIBS}")
    sylar_add_executable(test_http_sticky "${DIR}/test_http_sticky.cc" sylar "${LIBS}")
    sylar_add_executable(test_daemon "${DIR}/test_daemon.cc" sylar "${LIBS}")
endif()

# 设置执行文件的输出路径
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
# 设置库的输出路径
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

# rpc 调用方/提供方示例（caller、provider）
add_subdirectory(${PROJECT_SOURCE_DIR}/tests/rpc)

# ==================================install=================================
# 设置安装前缀
set(CMAKE_INSTALL_PREFIX /usr/local)

# 添加头文件安装规则
install(DIRECTORY sylar/
    DESTINATION include/sylar
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

# 添加库文件安装规则
install(TARGETS sylar
    LIBRARY DESTINATION lib
)

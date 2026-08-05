/**
 * @file test_rpc_bin_server.cc
 * @brief 按 sylar/rpc 二进制帧协议实现的最小服务端（性能对照用）
 * @note 协议与 sylar/rpc/rpcprovider.cc 一致：
 *       请求帧 = 4字节总长 + 4字节头长 + RpcHeader(service/method/args_size) + 参数
 *       响应   = 直接序列化的 response 字节（无长度前缀，与 sylar/rpc 相同）
 *       说明：省略了 sylar/rpc 的 zookeeper 服务注册，监听地址来自 conf/rpc.yml
 */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <iostream>
#include "sylar/env.h"
#include "sylar/config.h"
#include "sylar/rpc/rpcheader.pb.h"
#include "echo_add.poto.pb.h"
#include "echo_add_service_imp.h"

static ssize_t recvFully(int fd, void *buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        ssize_t n = ::recv(fd, (char *)buf + got, len - got, 0);
        if (n <= 0) {
            return -1;
        }
        got += (size_t)n;
    }
    return (ssize_t)got;
}

static void handleConn(int fd, sylar::tinyrpc::EchoAddService *service) {
    std::vector<char> buf;
    while (true) {
        uint32_t all_size = 0;
        if (recvFully(fd, &all_size, 4) != 4) {
            break;
        }
        buf.resize(all_size);
        if (recvFully(fd, buf.data(), all_size) != (ssize_t)all_size) {
            break;
        }
        uint32_t header_size = 0;
        std::memcpy(&header_size, buf.data(), 4);
        if (header_size > all_size) {
            break;
        }
        sylar_rpc::RpcHeader rpc_header;
        if (!rpc_header.ParseFromArray(buf.data() + 4, header_size)) {
            break;
        }
        const google::protobuf::MethodDescriptor *method =
            service->GetDescriptor()->FindMethodByName(rpc_header.method_name());
        if (!method) {
            break;
        }
        const char *args = buf.data() + 4 + header_size;
        std::unique_ptr<google::protobuf::Message> request(
            service->GetRequestPrototype(method).New());
        std::unique_ptr<google::protobuf::Message> response(
            service->GetResponsePrototype(method).New());
        if (!request->ParseFromArray(args, (int)rpc_header.args_size())) {
            break;
        }
        service->CallMethod(method, nullptr, request.get(), response.get(), nullptr);
        std::string rsp;
        if (!response->SerializeToString(&rsp)) {
            break;
        }
        if (::send(fd, rsp.data(), rsp.size(), 0) != (ssize_t)rsp.size()) {
            break;
        }
    }
    ::close(fd);
}

int main(int argc, char **argv) {
    sylar::EnvMgr::GetInstance()->init(argc, argv);
    {
        std::string exe = sylar::EnvMgr::GetInstance()->getExe();
        std::string exe_dir = exe.substr(0, exe.find_last_of('/'));
        sylar::EnvMgr::GetInstance()->add("c", exe_dir + "/../../conf");
    }
    sylar::Config::Lookup("rpc.bind_ip", std::string("0.0.0.0"), "rpc bind ip");
    sylar::Config::Lookup("rpc.bind_port", (int32_t)8007, "rpc bind port");
    sylar::Config::LoadFromConfDir(sylar::EnvMgr::GetInstance()->getConfigPath());

    std::string ip = sylar::Config::Lookup("rpc.bind_ip", std::string("0.0.0.0"), "rpc bind ip")->getValue();
    int32_t port = sylar::Config::Lookup("rpc.bind_port", (int32_t)8007, "rpc bind port")->getValue();

    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        std::cout << "socket fail" << std::endl;
        return 1;
    }
    int reuse = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
    addr.sin_port = htons((uint16_t)port);
    if (::bind(listen_fd, (sockaddr *)&addr, sizeof(addr)) < 0) {
        std::cout << "bind fail: " << strerror(errno) << std::endl;
        return 1;
    }
    if (::listen(listen_fd, 128) < 0) {
        std::cout << "listen fail" << std::endl;
        return 1;
    }
    std::cout << "bin server listening on " << ip << ":" << port << std::endl;

    // 业务服务（queryAdd/queryEcho 只读请求、写响应，无共享可变状态，可并发调用）
    sylar::tinyrpc::EchoAddServiceImp service;

    std::queue<int> conns;
    std::mutex mtx;
    std::condition_variable cv;
    bool stop = false;
    const int kWorkers = 8;
    std::vector<std::thread> workers;
    for (int i = 0; i < kWorkers; ++i) {
        workers.emplace_back([&]() {
            while (true) {
                int fd = -1;
                {
                    std::unique_lock<std::mutex> lock(mtx);
                    cv.wait(lock, [&]() { return stop || !conns.empty(); });
                    if (stop && conns.empty()) {
                        return;
                    }
                    fd = conns.front();
                    conns.pop();
                }
                handleConn(fd, &service);
            }
        });
    }

    while (true) {
        int fd = ::accept(listen_fd, nullptr, nullptr);
        if (fd < 0) {
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(mtx);
            conns.push(fd);
        }
        cv.notify_one();
    }
    return 0;
}

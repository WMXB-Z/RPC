/**
 * @file test_rpc_bin_perf.cc
 * @brief 按 sylar/rpc 二进制帧协议实现的客户端性能测试（与 test_rpc_perf 同统计口径）
 * @usage
 *   1. 先启动服务端：./bin/tinyrpc/test_rpc_bin_server
 *   2. 再运行本测试：./bin/tinyrpc/test_rpc_bin_perf -n <总请求数> -t <并发线程数>
 */
#include <chrono>
#include <thread>
#include <vector>
#include <algorithm>
#include <numeric>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "sylar/env.h"
#include "sylar/config.h"
#include "sylar/rpc/rpcheader.pb.h"
#include "sylar/tinyrpc/rpc_controller.h"
#include "echo_add.poto.pb.h"

namespace {

// 模拟 sylar/rpc 的 SylarRpcChannel：每次调用新建 TCP 连接（省略 zookeeper 发现，地址来自配置）
class BinRpcChannel : public google::protobuf::RpcChannel {
public:
    BinRpcChannel(const std::string &ip, uint16_t port)
        : m_ip(ip)
        , m_port(port) {
    }

    virtual void CallMethod(const google::protobuf::MethodDescriptor *method,
                            google::protobuf::RpcController *controller,
                            const google::protobuf::Message *request,
                            google::protobuf::Message *response,
                            google::protobuf::Closure *done) override {
        std::string args;
        if (!request->SerializeToString(&args)) {
            controller->SetFailed("serialize args fail");
            return;
        }
        sylar_rpc::RpcHeader header;
        header.set_service_name(method->service()->name());
        header.set_method_name(method->name());
        header.set_args_size((uint32_t)args.size());
        std::string header_str;
        header.SerializeToString(&header_str);

        uint32_t header_size = (uint32_t)header_str.size();
        uint32_t all_size = 4 + header_size + (uint32_t)args.size();
        std::string frame;
        frame.append((const char *)&all_size, 4);
        frame.append((const char *)&header_size, 4);
        frame.append(header_str);
        frame.append(args);

        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            controller->SetFailed("create socket fail");
            return;
        }
        sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(m_port);
        addr.sin_addr.s_addr = inet_addr(m_ip.c_str());
        if (::connect(fd, (sockaddr *)&addr, sizeof(addr)) < 0) {
            ::close(fd);
            controller->SetFailed("connect fail");
            return;
        }
        bool ok = false;
        do {
            if (::send(fd, frame.data(), frame.size(), 0) != (ssize_t)frame.size()) {
                break;
            }
            char buf[4096];
            ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
            if (n <= 0) {
                break;
            }
            if (!response->ParseFromArray(buf, (int)n)) {
                break;
            }
            ok = true;
        } while (false);
        ::close(fd);
        if (!ok) {
            controller->SetFailed("rpc io fail");
            return;
        }
        if (done) {
            done->Run();
        }
    }

private:
    std::string m_ip;
    uint16_t m_port;
};

}  // namespace

int main(int argc, char **argv) {
    sylar::EnvMgr::GetInstance()->init(argc, argv);
    {
        std::string exe = sylar::EnvMgr::GetInstance()->getExe();
        std::string exe_dir = exe.substr(0, exe.find_last_of('/'));
        sylar::EnvMgr::GetInstance()->add("c", exe_dir + "/../../conf");
    }
    sylar::Config::Lookup("rpc.server_ip", std::string("127.0.0.1"), "rpc server ip");
    sylar::Config::Lookup("rpc.server_port", (int32_t)8007, "rpc server port");
    sylar::Config::LoadFromConfDir(sylar::EnvMgr::GetInstance()->getConfigPath());

    std::string ip = sylar::Config::Lookup<std::string>("rpc.server_ip")->getValue();
    int32_t port = sylar::Config::Lookup<int32_t>("rpc.server_port")->getValue();

    uint64_t total = (uint64_t)atoll(sylar::EnvMgr::GetInstance()->get("n", "10000").c_str());
    uint32_t threads = (uint32_t)atoi(sylar::EnvMgr::GetInstance()->get("t", "4").c_str());
    if (total == 0 || threads == 0) {
        std::cout << "usage: test_rpc_bin_perf -n <total> -t <threads>" << std::endl;
        return 1;
    }

    // 预热
    {
        BinRpcChannel channel(ip, (uint16_t)port);
        sylar::tinyrpc::EchoAddService_Stub stub(&channel);
        sylar::tinyrpc::AddRequest req;
        req.set_a(1);
        req.set_b(2);
        sylar::tinyrpc::AddResponse rsp;
        sylar::tinyrpc::TinyRpcController ctl;
        for (int i = 0; i < 200; ++i) {
            ctl.Reset();
            stub.queryAdd(&ctl, &req, &rsp, nullptr);
            if (ctl.Failed()) {
                std::cout << "warmup failed: " << ctl.ErrorText() << std::endl;
                return 1;
            }
        }
    }

    uint64_t per_thread = total / threads;
    uint64_t remain = total % threads;
    std::atomic<uint64_t> ok_count{0}, fail_count{0};
    std::vector<std::vector<int64_t>> latencies(threads);

    auto start = std::chrono::steady_clock::now();
    std::vector<std::thread> workers;
    for (uint32_t t = 0; t < threads; ++t) {
        uint64_t cnt = per_thread + (t < remain ? 1 : 0);
        workers.emplace_back([&, t, cnt, ip, port]() {
            BinRpcChannel channel(ip, (uint16_t)port);
            sylar::tinyrpc::EchoAddService_Stub stub(&channel);
            std::vector<int64_t> &lat = latencies[t];
            lat.reserve((size_t)cnt);
            sylar::tinyrpc::AddRequest req;
            req.set_a(3);
            req.set_b(4);
            for (uint64_t i = 0; i < cnt; ++i) {
                sylar::tinyrpc::AddResponse rsp;
                sylar::tinyrpc::TinyRpcController ctl;
                auto t0 = std::chrono::steady_clock::now();
                stub.queryAdd(&ctl, &req, &rsp, nullptr);
                auto t1 = std::chrono::steady_clock::now();
                int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
                lat.push_back(us);
                if (ctl.Failed() || rsp.sum() != 7) {
                    ++fail_count;
                } else {
                    ++ok_count;
                }
            }
        });
    }
    for (auto &w : workers) {
        w.join();
    }
    auto end = std::chrono::steady_clock::now();

    double elapsed_s = std::chrono::duration<double>(end - start).count();
    std::vector<int64_t> all;
    for (auto &v : latencies) {
        all.insert(all.end(), v.begin(), v.end());
    }
    std::sort(all.begin(), all.end());

    uint64_t ok = ok_count.load();
    uint64_t fail = fail_count.load();
    double qps = elapsed_s > 0 ? (double)ok / elapsed_s : 0.0;
    double avg = all.empty() ? 0.0
                             : (double)std::accumulate(all.begin(), all.end(), (int64_t)0) / (double)all.size();
    int64_t p50 = all.empty() ? 0 : all[all.size() * 50 / 100];
    int64_t p95 = all.empty() ? 0 : all[all.size() * 95 / 100];
    int64_t p99 = all.empty() ? 0 : all[all.size() * 99 / 100];

    std::cout << "========== sylar/rpc 二进制帧协议性能 ==========" << std::endl;
    std::cout << "总请求数   : " << total << std::endl;
    std::cout << "并发线程数 : " << threads << std::endl;
    std::cout << "成功/失败  : " << ok << " / " << fail << "（错误率 "
              << (double)fail / (double)(ok + fail) * 100.0 << "%）" << std::endl;
    std::cout << "总耗时     : " << elapsed_s * 1000.0 << " ms" << std::endl;
    std::cout << "QPS        : " << qps << std::endl;
    std::cout << "平均延迟   : " << avg << " us" << std::endl;
    std::cout << "P50/P95/P99: " << p50 << " / " << p95 << " / " << p99 << " us" << std::endl;
    std::cout << "最小/最大  : " << (all.empty() ? 0 : all.front()) << " / "
              << (all.empty() ? 0 : all.back()) << " us" << std::endl;
    return fail == 0 ? 0 : 1;
}

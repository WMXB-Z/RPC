/**
 * @file test_rpc_io_perf.cc
 * @brief 基于 test_rpc_perf 的非阻塞事件驱动版性能测试
 * @details 与 test_rpc_perf 的区别：客户端不再跑在普通线程，而是跑在 IOManager
 *          调度线程的协程里。调度线程开启 hook，且连接（fd）由调度线程创建并
 *          登记进 FdManager（O_NONBLOCK），因此 recv/send/connect 变为
 *          非阻塞 + 协程挂起（事件驱动），少量线程即可并发大量在途请求。
 * 
 * 参数：
 *   -n 总请求数（请求模式）
 *   -d 持续时间秒（时间模式）
 *   -t IOManager 线程总数（含主线程）
 *      注意：本 fork 的 IOManager 多线程调度存在严重竞争（共享 epoll + 阻塞互斥锁），
 *      实测 -t 1（单线程事件驱动）吞吐最高，-t>=2 会暴跌，建议保持 -t 1。
 *   -k 协程数量（并发在途请求数）
 *   -p RpcChannelClient 数量（默认 = 协程数）

 ./bin/tinyrpc/test_rpc_io_perf -n 100000 -t 1 -k 32  -p 4
 （QPS 上升的同时延迟上升是比较正常的现象，符合 Little's Law）
 */
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>
#include <unistd.h>
#include <vector>
#include "sylar/config.h"
#include "sylar/env.h"
#include "sylar/iomanager.h"
#include "sylar/tinyrpc/rpc_channel_client.h"
#include "sylar/tinyrpc/rpc_controller.h"
#include "echo_add.poto.pb.h"

using namespace std::chrono;

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

    uint64_t total = std::stoull(sylar::EnvMgr::GetInstance()->get("n", "100000"));
    uint32_t threads = std::stoi(sylar::EnvMgr::GetInstance()->get("t", "1"));
    uint32_t coroutines = std::stoi(sylar::EnvMgr::GetInstance()->get("k", "64"));
    uint32_t connections = std::stoi(sylar::EnvMgr::GetInstance()->get("p", "0"));
    uint32_t duration = std::stoi(sylar::EnvMgr::GetInstance()->get("d", "0"));
    
    // 默认connection未设置时，等于协程数量
    if (connections == 0) {
        connections = coroutines;
    }
    if (threads == 0 || coroutines == 0 || connections == 0) {
        std::cout << "invalid parameters\n";
        return 1;
    }

    // 每个 RpcChannelClient 持有独立连接池
    std::vector<std::shared_ptr<sylar::tinyrpc::RpcChannelClient> > clients;
    for (uint32_t i = 0; i < connections; i++) {
        clients.emplace_back(std::make_shared<sylar::tinyrpc::RpcChannelClient>());
    }

    std::atomic<bool> stop_flag(false);
    std::atomic<bool> warmup_done(false);
    std::atomic<uint64_t> request_count(0);
    std::atomic<uint64_t> success(0);
    std::atomic<uint64_t> failed(0);
    std::vector<std::vector<int64_t> > latencies(coroutines);
    steady_clock::time_point begin;

    sylar::IOManager iom(threads, true, "perf");

    // 预热协程：建立连接并预热（连接在调度线程创建，fd 被登记 → 事件驱动生效）
    iom.schedule([&]() {
        sylar::tinyrpc::EchoAddService_Stub stub(clients[0].get());
        sylar::tinyrpc::AddRequest req;
        req.set_a(1);
        req.set_b(2);
        for (int i = 0; i < 200; i++) {
            sylar::tinyrpc::AddResponse rsp;
            sylar::tinyrpc::TinyRpcController ctl;
            stub.queryAdd(&ctl, &req, &rsp, nullptr);
            if (ctl.Failed()) {
                std::cout << "warmup failed:" << ctl.ErrorText() << std::endl;
                ::exit(1);
            }
        }
        // 预热完成，计时开始
        begin = steady_clock::now();
        warmup_done.store(true);
    });

    // 压测协程
    for (uint32_t c = 0; c < coroutines; c++) {
        iom.schedule([&, c]() {
            auto &lat = latencies[c];
            lat.reserve(duration > 0 ? 100000 : total / coroutines + 1);
            uint32_t index = c % connections;
            sylar::tinyrpc::EchoAddService_Stub stub(clients[index].get());
            sylar::tinyrpc::AddRequest req;
            req.set_a(3);
            req.set_b(4);
            // 等待预热完成（usleep 被 hook，协程让出，不阻塞线程）
            while (!warmup_done.load()) {
                ::usleep(1000);
            }
            while (true) {
                if (duration > 0) {
                    if (stop_flag.load()) {
                        break;
                    }
                } else {
                    uint64_t cur = request_count.fetch_add(1);
                    if (cur >= total) {
                        break;
                    }
                }
                sylar::tinyrpc::AddResponse rsp;
                sylar::tinyrpc::TinyRpcController ctl;
                auto t0 = steady_clock::now();
                stub.queryAdd(&ctl, &req, &rsp, nullptr);
                auto t1 = steady_clock::now();
                lat.push_back(duration_cast<microseconds>(t1 - t0).count());
                if (!ctl.Failed() && rsp.sum() == 7) {
                    success++;
                } else {
                    failed++;
                }
            }
        });
    }

    // 时间模式：到时置停止标志（协程内 sleep 被 hook，不阻塞线程）
    if (duration > 0) {
        iom.schedule([&]() {
            ::sleep(duration);
            stop_flag.store(true);
        });
    }

    // 主线程进入调度循环，所有协程完成后返回
    iom.stop();
    auto end = steady_clock::now();

    double seconds = ::duration<double>(end - begin).count();
    std::vector<int64_t> all;
    for (auto &v : latencies) {
        all.insert(all.end(), v.begin(), v.end());
    }
    std::sort(all.begin(), all.end());

    auto get_percentile = [&](double p) -> int64_t {
        if (all.empty()) {
            return 0;
        }
        size_t index = static_cast<size_t>(p * all.size());
        if (index >= all.size()) {
            index = all.size() - 1;
        }
        return all[index];
    };

    uint64_t ok = success.load();
    uint64_t err = failed.load();
    uint64_t total_req = ok + err;
    double qps = seconds > 0 ? total_req / seconds : 0;
    double avg = all.empty() ? 0 : (double)std::accumulate(all.begin(), all.end(), (int64_t)0) / all.size();
    std::cout << "\n========== TinyRPC I/O 事件驱动 Benchmark ==========\n";
    std::cout << "Mode        : " << (duration > 0 ? "time" : "request") << "\n";
    std::cout << "Requests    : " << total_req << "\n";
    std::cout << "Threads     : " << threads << "\n";
    std::cout << "Coroutines  : " << coroutines << "\n";
    std::cout << "Connections : " << connections << "\n";
    std::cout << "Time        : " << seconds << " s\n";
    std::cout << "QPS         : " << qps << "\n";
    std::cout << "Success     : " << ok << "\n";
    std::cout << "Failed      : " << err << "\n";
    std::cout << "Error       : " << (total_req ? (double)err / total_req * 100 : 0) << "%\n";
    std::cout << "Latency(us)\n";
    std::cout << " Avg : " << avg << "\n";
    std::cout << " P50 : " << get_percentile(0.50) << "\n";
    std::cout << " P95 : " << get_percentile(0.95) << "\n";
    std::cout << " P99 : " << get_percentile(0.99) << "\n";

    return err == 0 ? 0 : 1;
}

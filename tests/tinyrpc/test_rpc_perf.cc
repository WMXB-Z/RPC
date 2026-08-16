/**
 * @file test_rpc_perf.cc
 *
 * TinyRPC benchmark
 *
 * 支持:
 * -n 请求数量模式
 * -d 时间模式(秒)
 * -t worker线程数量
 * -p RPC中channel client对象个数(目前不起效果，因为 RpcChannelClient 共享同一个静态连接池)
 *
 * example:
 *
 * ./test_rpc_perf -n 100000 -t 4
 *
 * ./test_rpc_perf -d 10 -t 4 -p 64
 *
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>

#include "sylar/config.h"
#include "sylar/env.h"
#include "sylar/tinyrpc/rpc_channel_client.h"
#include "sylar/tinyrpc/rpc_controller.h"
#include "echo_add.poto.pb.h"
using namespace std::chrono;

int main(int argc, char **argv) {
    sylar::EnvMgr::GetInstance()->init(argc, argv);

    /*
     * 加载配置
     */
    sylar::EnvMgr::GetInstance()->add("c", "/home/sylar-from-suycx/conf");
    sylar::Config::Lookup("rpc.server_ip", std::string("127.0.0.1"));
    sylar::Config::Lookup("rpc.server_port", (int32_t)8008);
    sylar::Config::LoadFromConfDir(sylar::EnvMgr::GetInstance()->getConfigPath());

    // 参数
    uint64_t total = std::stoull(sylar::EnvMgr::GetInstance()->get("n", "100000"));
    uint32_t threads = std::stoi(sylar::EnvMgr::GetInstance()->get("t", "1"));
    uint32_t connections = std::stoi(sylar::EnvMgr::GetInstance()->get("p", "32"));
    uint32_t duration = std::stoi(sylar::EnvMgr::GetInstance()->get("d", "0"));

    // 默认连接数=线程数
    if (connections == 0) {
        connections = threads;
    }

    if (threads == 0 || connections == 0) {
        std::cout << "invalid parameters\n";
        return 1;
    }

    // client连接池
    std::vector<std::shared_ptr<sylar::tinyrpc::RpcChannelClient> > clients;
    for (uint32_t i = 0; i < connections; i++) {
        clients.emplace_back(std::make_shared<sylar::tinyrpc::RpcChannelClient>());
    }

    // 预热：覆盖所有连接池，提前建连，避免正式计时阶段混入 connect 开销
    {
        sylar::tinyrpc::AddRequest req;
        req.set_a(1);
        req.set_b(2);

        for (auto &client : clients) {
            sylar::tinyrpc::EchoAddService_Stub stub(client.get());
            for (int i = 0; i < 100; i++) {
                sylar::tinyrpc::AddResponse rsp;
                sylar::tinyrpc::TinyRpcController ctl;
                stub.queryAdd(&ctl, &req, &rsp, nullptr);
                if (ctl.Failed()) {
                    std::cout << "warmup failed:" << ctl.ErrorText() << std::endl;
                    return 1;
                }
            }
        }
    }

    // benchmark状态
    std::atomic<bool> start_flag(false);
    std::atomic<bool> stop_flag(false);
    std::atomic<uint64_t> request_count(0);
    std::atomic<uint64_t> success(0);
    std::atomic<uint64_t> failed(0);

    // 每线程保存latency
    std::vector<std::vector<int64_t> > latencies(threads);
    std::vector<std::thread> workers;

    // 创建worker
    for (uint32_t i = 0; i < threads; i++) {
        workers.emplace_back([&, i]() {
            auto &lat = latencies[i];
            lat.reserve(duration > 0 ? 100000 : total / threads + 1);

            // 每个线程一个stub
            uint32_t index = i % connections;
            sylar::tinyrpc::EchoAddService_Stub stub(clients[index].get());
            sylar::tinyrpc::AddRequest req;
            req.set_a(3);
            req.set_b(4);

            // 等待统一开始
            while (!start_flag.load()) {
                std::this_thread::yield();
            }

            while (true) {
                // 时间模式
                if (duration > 0) {
                    if (stop_flag.load()) {
                        break;
                    }
                }

                // 请求数模式
                else {
                    uint64_t cur = request_count.fetch_add(1);
                    if (cur >= total) {
                        break;
                    }
                }

                sylar::tinyrpc::AddResponse rsp;
                sylar::tinyrpc::TinyRpcController ctl;
                auto begin = steady_clock::now();
                stub.queryAdd(&ctl, &req, &rsp, nullptr);
                auto end = steady_clock::now();
                int64_t us = duration_cast<microseconds>(end - begin).count();
                lat.push_back(us);
                if (!ctl.Failed() && rsp.sum() == 7) {
                    success++;
                } else {
                    failed++;
                }
            }
        });
    }
    // 开始测试
    auto begin = steady_clock::now();
    start_flag.store(true);

    // 时间模式
    if (duration > 0) {
        std::this_thread::sleep_for(std::chrono::seconds(duration));
        stop_flag.store(true);
    }

    for (auto &t : workers) {
        t.join();
    }

    auto end = steady_clock::now();
    double seconds = ::duration<double>(end - begin).count();

    // 合并latency
    std::vector<int64_t> all;
    for (auto &v : latencies) {
        all.insert(all.end(), v.begin(), v.end());
    }
    std::sort(all.begin(), all.end());

    auto get_percentile = [&](double p) -> int64_t {
        if (all.empty()) return 0;
        size_t index = static_cast<size_t>(p * all.size());
        if (index >= all.size()) index = all.size() - 1;
        return all[index];
    };

    uint64_t ok = success.load();
    uint64_t err = failed.load();
    uint64_t total_req = ok + err;

    double qps = seconds > 0 ? total_req / seconds : 0;
    double avg = all.empty() ? 0 : (double)std::accumulate(all.begin(), all.end(), (int64_t)0) / all.size();
    std::cout << "\n========== TinyRPC Benchmark ==========\n";

    std::cout << "Mode       : " << (duration > 0 ? "time" : "request") << "\n";
    std::cout << "Requests   : " << total_req << "\n";
    std::cout << "Threads    : " << threads << "\n";
    std::cout << "Connections: " << connections << "\n";
    std::cout << "Time       : " << seconds << " s\n";
    std::cout << "QPS        : " << qps << "\n";
    std::cout << "Success    : " << ok << "\n";
    std::cout << "Failed     : " << err << "\n";
    std::cout << "Error      : " << (total_req ? (double)err / total_req * 100 : 0) << "%\n";
    std::cout << "Latency(us)\n";
    std::cout << " Avg : " << avg << "\n";
    std::cout << " P50 : " << get_percentile(0.50) << "\n";
    std::cout << " P95 : " << get_percentile(0.95) << "\n";
    std::cout << " P99 : " << get_percentile(0.99) << "\n";

    return err == 0 ? 0 : 1;
}

/**
 * @file test_rpc_perf.cc
 * @brief tinyrpc 性能测试：QPS、平均延迟、P50/P95/P99、错误率
 * @usage
 *   1. 先启动服务端：./bin/tinyrpc/test_rpc_service
 *   2. 再运行本测试：./bin/tinyrpc/test_rpc_perf -n <总请求数> -t <并发线程数>
 */
#include <chrono>
#include <thread>
#include <vector>
#include <algorithm>
#include <numeric>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include "sylar/env.h"
#include "sylar/config.h"
#include "sylar/tinyrpc/rpc_channel_client.h"
#include "sylar/tinyrpc/rpc_controller.h"
#include "echo_add.poto.pb.h"

int main(int argc, char **argv) {
    sylar::EnvMgr::GetInstance()->init(argc, argv);
    // 配置目录：Env::getConfigPath() 默认以可执行文件目录为基准（bin/tinyrpc/），
    // 而 conf 在项目根目录，故按可执行文件位置向上两级推导（也可用 -c <绝对路径> 覆盖）
    
    sylar::EnvMgr::GetInstance()->add("c", "/home/sylar-from-suycx/conf");
    
    // 先注册配置参数（供 getPool 读取），再加载 yaml 覆盖默认值
    sylar::Config::Lookup("rpc.server_ip", std::string("127.0.0.1"), "rpc server ip");
    sylar::Config::Lookup("rpc.server_port", (int32_t)8008, "rpc server port");
    sylar::Config::LoadFromConfDir(sylar::EnvMgr::GetInstance()->getConfigPath());

    uint64_t total = (uint64_t)atoll(sylar::EnvMgr::GetInstance()->get("n", "10000").c_str());
    uint32_t threads = (uint32_t)atoi(sylar::EnvMgr::GetInstance()->get("t", "4").c_str());
    if (total == 0 || threads == 0) {
        std::cout << "usage: test_rpc_perf -n <total> -t <threads>" << std::endl;
        return 1;
    }

    // 共享同一个 RpcChannelClient（内部使用 RpcConnectionPool 复用连接）
    sylar::tinyrpc::RpcChannelClient channel;
    sylar::tinyrpc::EchoAddService_Stub stub(&channel);

    // 预热：建连 + 代码路径预热
    {
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
        workers.emplace_back([&, t, cnt]() {
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

    std::cout << "========== RPC 性能测试结果 ==========" << std::endl;
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

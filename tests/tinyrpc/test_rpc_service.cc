#include <memory>
#include "http/servlet.h"
#include "sylar/config.h"
#include "sylar/iomanager.h"
#include "sylar/env.h"
#include "sylar/tinyrpc/rpc_server.h"
#include "echo_add_service_imp.h"
#include "sylar/tinyrpc/rpc_func_servlet.h"

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();
sylar::IOManager::ptr worker;

void run() {
    g_logger->setLevel(sylar::LogLevel::WARN);
    sylar::tinyrpc::RpcServer::ptr server(new sylar::tinyrpc::RpcServer(true, worker.get(), sylar::IOManager::GetThis()));
    sylar::Address::ptr addr = sylar::Address::LookupAnyIPAddress("0.0.0.0:8008");
    while (!server->bind(addr)) {
        sleep(2);
    }
    sylar::http::ServletDispatch::ptr sd = server->getServletDispatch();

    sylar::tinyrpc::EchoAddServiceImp::ptr service(new sylar::tinyrpc::EchoAddServiceImp);
    // RpcFuncServlet 持有 Service 对象，通过反射处理该服务的所有方法
    sylar::http::Servlet::ptr servlet(new sylar::tinyrpc::RpcFuncServlet(service));
    // 注册servlet服务，一个 servlet 覆盖该服务下（例如这里的EchoAddService）的所有方法（queryEcho、queryAdd...）
    sd->addGlobServlet("/EchoAddService/*", servlet);
    server->start();
}

int main(int argc, char **argv) {
    sylar::EnvMgr::GetInstance()->init(argc, argv);
    sylar::Config::LoadFromConfDir(sylar::EnvMgr::GetInstance()->getConfigPath());
    g_logger->setLevel(sylar::LogLevel::ERROR);

    sylar::IOManager iom(1, true, "main");
    worker.reset(new sylar::IOManager(8, false, "worker"));
    iom.schedule(run);
    return 0;
}


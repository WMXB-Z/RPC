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
    auto sd = server->getServletDispatch();

    sylar::tinyrpc::EchoAddServiceImp::ptr service(new sylar::tinyrpc::EchoAddServiceImp);
    sylar::http::Servlet::ptr slt(new sylar::tinyrpc::RpcFuncServlet(std::bind(
                                                                        &sylar::tinyrpc::EchoAddServiceImp::CallMethod,
                                                                        service,
                                                                        std::placeholders::_1,
                                                                        std::placeholders::_2,
                                                                        std::placeholders::_3,
                                                                        std::placeholders::_4,
                                                                        std::placeholders::_5)));
    // 注册servlet服务，测试echo服务
    sd->addServlet("/EchoAddService/queryEcho", slt);

    // 注册servlet服务，测试add服务
    sd->addServlet("/EchoAddService/queryAdd", slt);
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


    // 这里的bind等价于：
    // lambda等价形式：
    // [service](参数1, 参数2, 参数3, 参数4, 参数5){
    //     service->CallMethod(
    //         参数1,
    //         参数2,
    //         参数3,
    //         参数4,
    //         参数5
    //     );
    // }
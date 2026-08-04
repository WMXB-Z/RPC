#include <iostream>
#include <ostream>
#include "./echo_add.poto.pb.h"
#include "sylar/tinyrpc/rpc_channel_client.h"
#include "sylar/tinyrpc/rpc_controller.h"
#include "sylar/env.h"
#include "sylar/config.h"

int main(int argc, char** argv){
    // 初始化环境并加载配置（conf/rpc.yml）
    sylar::EnvMgr::GetInstance()->init(argc, argv);
    // todo:这里设置为绝对路径不太好,但由于配置文件的查找默认是在当前工作目录下进行的，使用相对路径可能会找不到
    sylar::EnvMgr::GetInstance()->add("c", "/home/sylar-from-suycx/conf");
    // 配置文件中参数的加载方式是：先注册（明确变量类型、默认值、说明信息），再通过加载yaml文件中的参数覆盖设置值
    sylar::Config::Lookup("rpc.server_ip", std::string("127.0.0.1"), "rpc server ip");
    sylar::Config::Lookup("rpc.server_port", (int32_t)8008, "rpc server port");
    sylar::Config::LoadFromConfDir(sylar::EnvMgr::GetInstance()->getConfigPath());

    // 创建远程rpc方法代理类
    sylar::tinyrpc::EchoAddService_Stub stub(new sylar::tinyrpc::RpcChannelClient() );

    // （1）测试回显服务
    sylar::tinyrpc::EchoRequest echo_request;
    sylar::tinyrpc::EchoResponse echo_response;
    sylar::tinyrpc::TinyRpcController controller;
    echo_request.set_mess("hello,this is client");
    // 调用业务
    stub.queryEcho(&controller, &echo_request, &echo_response, nullptr);
    // 调用完成
    if (!controller.Failed()){
        std::cout << "rpc process is successful " << std::endl;
        std::cout << "the reply is: " << echo_response.mess() << std::endl;
    }else{
        std::cout << "rpc process error : " << controller.ErrorText() << std::endl;
    }

    // （2）测试add服务
    sylar::tinyrpc::AddRequest add_request;
    add_request.set_a(1);
    add_request.set_b(2);
    sylar::tinyrpc::AddResponse add_response;

    controller.Reset();
    stub.queryAdd(&controller, &add_request, &add_response, nullptr);
    if (!controller.Failed()){
        std::cout << "rpc process is successful" << std::endl;
        std::cout << "Sum is :" << add_response.sum() << std::endl;
    }else{
        std::cout << "rpc process error : " << controller.ErrorText() << std::endl;
    }

    // （3）测试sub服务(可以做到与后端框架层解耦，新增服务无需改动)
    // sylar::tinyrpc::SubRequest sub_request;
    // sub_request.set_a(1);
    // sub_request.set_b(2);
    // sylar::tinyrpc::SubResponse sub_response;

    // controller.Reset();
    // stub.querySub(&controller, &sub_request, &sub_response, nullptr);
    // if (!controller.Failed()){
    //     std::cout << "rpc process is successful" << std::endl;
    //     std::cout << "Ans is :" << sub_response.ans() << std::endl;
    // }else{
    //     std::cout << "rpc process error : " << controller.ErrorText() << std::endl;
    // }

    return 0;
}
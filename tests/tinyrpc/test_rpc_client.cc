#include <iostream>
#include <ostream>
#include "./echo_add.poto.pb.h"
#include "sylar/tinyrpc/rpc_channel_client.h"
#include "sylar/tinyrpc/rpc_controller.h"
int main(){
    // todo: 系统的初始化操作Init(argc, argv);这一部分放在服务器启动中
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
    if (0 == echo_response.result().errcode()){
        std::cout << "rpc login response : " << echo_response.success() << std::endl;
        std::cout << "the reply is: " << echo_response.mess() << std::endl;
    }else{
        std::cout << "rpc login response error : " << echo_response.result().errmsg() << std::endl;
    }

    // （2）测试add服务
    sylar::tinyrpc::AddRequest add_request;
    add_request.set_a(1);
    add_request.set_b(2);
    sylar::tinyrpc::AddResponse add_response;

    controller.Reset();
    stub.queryAdd(&controller, &add_request, &add_response, nullptr);
    if (0 == add_response.result().errcode()){
        std::cout << "rpc register response : " << add_response.success() << std::endl;
        std::cout << "Sum is :" << add_response.sum() << std::endl;
    }else{
        std::cout << "rpc register response error : " << add_response.result().errmsg() << std::endl;
    }

    return 0;
}
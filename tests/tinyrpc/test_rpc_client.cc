#include <iostream>
#include <ostream>
#include "./echo_add.poto.pb.h"
#include "sylar/tinyrpc/rpc_channel_client.h"
#include "sylar/tinyrpc/rpc_controller.h"

int main(){
    // todo: 系统的初始化操作Init(argc, argv)
    
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
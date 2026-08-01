#include "rpc_channel_client.h"
#include <google/protobuf/descriptor.h>
#include "rpc_controller.h"
#include "rpc_header.pb.h"
#include <sys/socket.h>
#include "../socket.h"


namespace sylar 
{
namespace tinyrpc 
{
void RpcChannelClient::CallMethod(const google::protobuf::MethodDescriptor *method,
                                  google::protobuf::RpcController *controller, 
                                  const google::protobuf::Message *request,
                                  google::protobuf::Message *response, 
                                  google::protobuf::Closure *done) {
    // ====================================================
    // 获取服务名以及需要调用的方法
    const google::protobuf::ServiceDescriptor *service_info = method->service();
    std::string service_name = service_info->name();
    std::string method_name = method->name();
    // 设置RpcController相关的状态信息

    // 将request进行序列化，转为特定编码格式（这里交由TCP层去实现）

    std::string param_str;
    if (!request->SerializeToString(&param_str)) {
        controller->SetFailed("Serialize reauest error!");
        return;
    }
     // set rpcheader
    RpcHeader rpcheader;
    uint32_t param_size = param_str.size();
    rpcheader.set_service_name(service_name);
    rpcheader.set_method_name(method_name);
    rpcheader.set_args_size(param_size);

    // 序列化 rpcheader部分
    std::string rpcheader_str;
    if (!rpcheader.SerializeToString(&rpcheader_str)){
        controller->SetFailed("Serialize request rpcheader error !!!");
        return;
    }
    uint32_t send_all_size = SEND_RPC_HEADERSIZE + rpcheader_str.size() + args_str.size();
    uint32_t rpcheader_size = rpcheader_str.size();

    // 组装 rpc 请求帧
    std::string rpc_send_str;
    // send_all_size
    rpc_send_str += std::string((char*)&send_all_size, 4);
    // rpcheader_size 
    rpc_send_str += std::string((char*)&rpcheader_size, 4);
    // rpcheader部分的二进制串
    rpc_send_str += rpcheader_str;
    // 参数部分的二进制串
    rpc_send_str += param_str;
// =========================================
    // todo: 进行TCP传输
    auto socket = sylar::Socket::CreateTCPSocket();
    // SYLAR_ASSERT(socket);

    auto addr = sylar::Address::LookupAnyIPAddress("0.0.0.0:12345");
    // SYLAR_ASSERT(addr);

    if(!socket->connect(addr)){
        // SYLAR_ASSERT(ret);
        controller->SetFailed("connect error : " + std::to_string(errno));
        return;
    }
    
    if (-1 == send(clientfd, rpc_send_str.c_str(), rpc_send_str.size(), 0))
    {
        controller->SetFailed("send error : " + std::to_string(errno));
        close(clientfd);
        return;       
    }

    char recv_buf[1024] = {0};
    uint32_t recv_size = 0;
    if ((recv_size = recv(clientfd, recv_buf, 1024, 0)) < 0)
    {
        controller->SetFailed("recv error : " + std::to_string(errno));
        close(clientfd);
        return;
    }
    // ========================================================
    // 接收处理结果，并反序列化的得到response

    // 如果设置了回调函数，则执行
    if (done) {
        done->Run();
    }
}
}  // namespace tinyrpc

}  // namespace sylar
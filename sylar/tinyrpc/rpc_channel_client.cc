#include "rpc_channel_client.h"
#include <memory>
#include <string>
#include "sylar/config.h"
#include "sylar/http/http.h"
#include "sylar/tinyrpc/rpc_connection.h"

namespace sylar {
namespace tinyrpc {

RpcChannelClient::RpcChannelClient()
    : m_pool(new RpcConnectionPool(
          sylar::Config::Lookup("rpc.server_ip", std::string("127.0.0.1"), "rpc server ip")->getValue(),
          sylar::Config::Lookup("rpc.server_port", (int32_t)8008, "rpc server port")->getValue())) {
}

void RpcChannelClient::CallMethod(const google::protobuf::MethodDescriptor *method,
                                  google::protobuf::RpcController *controller, 
                                  const google::protobuf::Message *request,
                                  google::protobuf::Message *response, 
                                  google::protobuf::Closure *done) {
    if(!method || !controller || !request || !response){
        std::cout << "method, controller, request, response must be not nullptr" << std::endl;
        return;
    }

    // 使用本客户端独立的连接池收发 RPC
    if (!m_pool->doRequest(method, controller, request, response, 10 * 1000)) {
        // 若 doRequest 内部已设置具体错误（如超时/连接失败），不要覆盖它
        if (!controller->Failed()) {
            controller->SetFailed("rpc doRequest failed!");
        }
        return;
    }

    // 如果设置了回调函数，则执行
    if (done) {
        done->Run();
    }
    
}   
}  // namespace tinyrpc

}  // namespace sylar
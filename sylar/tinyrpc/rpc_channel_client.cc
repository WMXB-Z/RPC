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
          sylar::Config::Lookup<std::string>("rpc.server_ip")->getValue(),
          sylar::Config::Lookup<int32_t>("rpc.server_port")->getValue()
          ,"", 30, 1000*10, 100000)) {
            // 单纯为了刷QPS而调参，感觉没有多大意义！
            // 比如这里的conn存活时间设置为10s，复用次数设置为10w，仅是为了提高连接的复用（减少conn的释放）。
            // 而且我也试过直接将Http层和Tcp层取消，自定义一个PRC协议帧，利用socket进行TCP传输，QPC直接大幅度提升
            // 但如此一来，Rpc协议的使用就不具有通用性、实用性
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
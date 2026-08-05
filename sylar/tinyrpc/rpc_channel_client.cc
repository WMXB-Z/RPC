#include "rpc_channel_client.h"
#include <memory>
#include <string>
#include "sylar/config.h"
#include "sylar/http/http.h"
#include "sylar/tinyrpc/rpc_connection.h"

namespace sylar {
namespace tinyrpc {
void RpcChannelClient::CallMethod(const google::protobuf::MethodDescriptor *method,
                                  google::protobuf::RpcController *controller, 
                                  const google::protobuf::Message *request,
                                  google::protobuf::Message *response, 
                                  google::protobuf::Closure *done) {
    if(!method || !controller || !request || !response){
        std::cout << "method, controller, request, response must be not nullptr" << std::endl;
        return;
    }

    // 基于TCP进行服务请求
    // todo:每次都新建TCP连接，性能较差。后续可参考项目里的 HttpConnectionPool 做连接复用。
    RpcConnectionPool::ptr pool = RpcConnectionPool::getPool();
    if (!pool->doRequest(method, controller, request, response, 10 * 1000)) {
        controller->SetFailed("rpc doRequest failed!");
        return;
    }

    // 如果设置了回调函数，则执行
    if (done) {
        done->Run();
    }
    
}   
}  // namespace tinyrpc

}  // namespace sylar
#include "rpc_channel_client.h"
#include <memory>
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
    // ====================================================
    // 创建http request报文
    http::HttpRequest::ptr http_req(new http::HttpRequest);
    if(!RpcConnection::putToHttpRequest(method, request, http_req)){
        controller->SetFailed("request message SeralizeToString failed!");
        return;
    }

    // 进行TCP传输，并获得http response响应报文
    // todo:这里加载 服务器配置的方式需要修改
    sylar::Address::ptr addr = sylar::Address::LookupAnyIPAddress("127.0.0.0:8008");
    if (!addr) {
        controller->SetFailed("get addr error");
        return;
    }

    sylar::Socket::ptr sock = sylar::Socket::CreateTCP(addr);
    // todo: sock->setRecvTimeout(...) 或在 TinyRpcController 里加超时字段。
    bool rt  = sock->connect(addr);
    if (!rt) {
        controller->SetFailed("connect failed");
        return;
    }

    // 基于TCP进行服务请求
    // todo:每次都新建TCP连接，性能较差。后续可参考项目里的 HttpConnectionPool 做连接复用。
    http::HttpConnection::ptr conn(new http::HttpConnection(sock));
    if(conn->sendRequest(http_req) <= 0){
        controller->SetFailed("send request error!");
        return;
    }
    http::HttpResponse::ptr http_res = conn->recvResponse();
    if(!http_res){
        controller->SetFailed("recv response is nullptr!");
        return;
    }
    if(http_res->getStatus() != http::HttpStatus::OK){
        controller->SetFailed("http server error!");
        return;
    }
    // 根据http response报文得到 response
    if(!RpcConnection::getFromHttpResponse(http_res, response)){
        controller->SetFailed("response parse from string failed!!");
        return;
    }

    // 如果设置了回调函数，则执行
    // if (done) {
    //     done->Run();
    // }
    
}   
}  // namespace tinyrpc

}  // namespace sylar
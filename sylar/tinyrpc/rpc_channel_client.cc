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
    http::HttpConnection::ptr conn = pool->getConnection();
    if (!conn) { 
        controller->SetFailed("get connection fail"); 
        return; 
    }
    http::HttpRequest::ptr http_req(new http::HttpRequest);
    if(!RpcConnection::putToHttpRequest(method, request, http_req)){
        controller->SetFailed("request message SeralizeToString failed!");
        return;
    }

    // 根据conn自身的 keep-alive 自动设置请求的该req的 m_close（用户显式指定 Connection 头时则由用户设置决定）
    if (http_req->getHeader("connection").empty()) {
        http_req->setClose(!conn->isKeepAlive());
    }

    // 发送http request报文
    // http::HttpConnection::ptr conn(new http::HttpConnection(sock));
    if(conn->sendRequest(http_req) <= 0){
        controller->SetFailed("send request error!");
        return;
    }
    // 接收http Response报文
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

    // 服务端声明将关闭连接（connection: close）时，本地主动 将socket close，
    // ReleasePtr 的 isConnected() 检查会判定不可复用并销毁，死连接不会回池
    if (http_res->isClose()) {
        conn->close();
    }

    // 如果设置了回调函数，则执行
    if (done) {
        done->Run();
    }
    
}   
}  // namespace tinyrpc

}  // namespace sylar
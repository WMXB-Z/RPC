#include "rpc_connection.h"
#include "sylar/http/http_connection.h"
#include "sylar/tinyrpc/rpc_header.pb.h"

namespace sylar {
namespace tinyrpc {
    RpcConnection::RpcConnection(Socket::ptr sock, bool owner) : http::HttpConnection(sock, owner) {}
    bool RpcConnection::putToHttpRequest(const google::protobuf::MethodDescriptor *method
                                        , google::protobuf::RpcController *controller
                                        , const google::protobuf::Message *request
                                        , http::HttpRequest::ptr http_req) {

        // 获取服务名和方法名
        const google::protobuf::ServiceDescriptor* service_desc = method->service();
        // std::string service_name = service_desc->name();
        // std::string method_name = method->name();

        // todo:这里要完成的是 RPC协议帧的封装 以及 编码（序列化）
        std::string param_str;
        if(!request->SerializeToString(&param_str)){
            controller->SetFailed("request message SeralizeToString failed!");
            return false;
        }

        http_req->setMethod(sylar::http::HttpMethod::POST);
        http_req->setPath(service_desc->name() + '/' + method->name());
        http_req->setVersion(0x11);
        http_req->setHeader("service_name", service_desc->name());
        http_req->setHeader("method_name", method->name());
        http_req->setHeader("Connection", "keep-alive");
        http_req->appendBody(param_str);

        http_req->init();

        return true;
    }

    bool RpcConnection::getFromHttpResponse(http::HttpResponse::ptr http_res
                                           , google::protobuf::RpcController *controller
                                           , google::protobuf::Message *response) {
        // todo: 对RPC协议帧进行 反序列化 和拆解的过程
        const std::string& param_str = http_res->getBody();
        if(!response->ParseFromString(param_str)){
            controller->SetFailed("response parse from string failed!!");
            return false;
        }
        return true;
    }

}  // namespace tinyrpc
}  // namespace sylar
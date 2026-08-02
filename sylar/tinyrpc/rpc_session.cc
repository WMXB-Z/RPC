
#include "sylar/http/http_session.h"
#include "sylar/tinyrpc/rpc_session.h"
#include <google/protobuf/message.h>
namespace sylar{
namespace tinyprc{

    RpcSession::RpcSession(Socket::ptr sock, bool owner):http::HttpSession(sock, owner){ }

    /**
    * @brief 根据接收HttpRequest，并拆解得到需要的数据
    */
    static bool getFromHttpRequest(http::HttpRequest::ptr http_req,
                                   google::protobuf::MethodDescriptor *method,
                                   google::protobuf::Message *request){
    // todo:从request中取出method、request
        method

        std::string param_str = http_req->getBody();
        if (!request->ParseFromString(param_str)) {
            std::cout << " rpc session request parse from string error";
            return false;
        }
    }

    /**
    * @brief 将处理后的结果，封装成一个HttpResponse类
    */
    static bool putToHttpResponse(google::protobuf::Message *response,
                                  http::HttpResponse::ptr http_response){

    }

}
}

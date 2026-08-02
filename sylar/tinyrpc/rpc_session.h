#ifndef RPC_SESSION
#define RPC_SESSION

#include <google/protobuf/descriptor.h>
#include "sylar/http/http_session.h"
namespace sylar{
namespace tinyprc{
class RpcSession : public http::HttpSession{
public:
    RpcSession(Socket::ptr sock, bool owner = true);

    /**
    * @brief 将收到的HttpRequest，拆解得到需要的数据
    */
    static bool getFromHttpRequest(http::HttpRequest::ptr http_req,
                                   const google::protobuf::MethodDescriptor *method,
                                   const google::protobuf::Message *request);

    /**
    * @brief 将处理后的结果，封装成一个HttpResponse类
    */
    static bool putToHttpResponse(google::protobuf::Message *response,
                                  http::HttpResponse::ptr http_response);

};
}
}
#endif
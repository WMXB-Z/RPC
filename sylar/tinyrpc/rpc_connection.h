# ifndef __TINY_RPC_CONNECTION_H__
# define __TINY_RPC_CONNECTION_H__

#include <google/protobuf/descriptor.h>
#include <google/protobuf/service.h>
#include <google/protobuf/message.h>
#include <memory>
#include "../streams/socket_stream.h"
#include "sylar/http/http.h"
#include "sylar/http/http_connection.h"

namespace sylar{
namespace tinyrpc{
class RpcConnection: public http::HttpConnection{
public:
    typedef std::shared_ptr<RpcConnection>  ptr;

    /**
     * @brief 构造 RpcConnection时，需要设置本次连接所用的sock
     * 
     * @param sock 本次connetct中，客户端的socket
     */
    RpcConnection(Socket::ptr sock, bool owner = true);

    /**
     * @brief 将待发送的数据，封装成一个HttpRequest类
     */
    static bool putToHttpRequest(const google::protobuf::MethodDescriptor *method,
                google::protobuf::RpcController *controller, 
                const google::protobuf::Message *request,
                http::HttpRequest::ptr http_req);

    /**
     * @brief 将收到的HttpResponse，拆解得到需要的数据
     */
    static bool getFromHttpResponse(http::HttpResponse::ptr http_response,
                                    google::protobuf::RpcController *controller,
                                    google::protobuf::Message *response);

};

}   //namespace tinyrpc
} //namespace sylar
#endif
# ifndef __TINY_RPC_CONNECTION_H__
# define __TINY_RPC_CONNECTION_H__

#include <google/protobuf/descriptor.h>
#include <google/protobuf/service.h>
#include <google/protobuf/message.h>
#include <memory>
#include "../streams/socket_stream.h"

#define SEND_RPC_HEADERSIZE 4
namespace sylar{
namespace tinyrpc{
class RpcConnection: public SocketStream{
public:
    typedef std::shared_ptr<RpcConnection>  ptr;

    /**
     * @brief 构造 RpcConnection时，需要设置本次连接所用的sock
     * 
     * @param sock 本次connetct中，客户端的socket
     */
    RpcConnection(Socket::ptr sock, bool owner = true);

    /**
     * @brief 将request拼接只rpchead中，并序列化
     */
    static bool encode(const google::protobuf::MethodDescriptor *method,
                google::protobuf::RpcController *controller, 
                const google::protobuf::Message *request,
                std::string& rpc_send_str);

    /**
     * @brief 将收到的结果，反序列化为response
     */
    static bool decode(google::protobuf::RpcController *controller,
                google::protobuf::Message *response,
                std::string& rpc_recv_str);
    
    /**
     * @brief 将序列化之后的RPC请求，通过stream进行TCP传输
     */
    bool sendRequest();

    /**
     * @brief 接收对端的回复，并反序列化为RPC响应报文
     * @return true 
     * @return false 
     */
    bool recvResponse();
};

}   //namespace tinyrpc
} //namespace sylar
#endif
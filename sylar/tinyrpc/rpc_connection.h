# ifndef RPC_CONNECTION_H
# define RPC_CONNECTION_H

#include <google/protobuf/descriptor.h>
#include <google/protobuf/service.h>
#include <google/protobuf/message.h>
#include <memory>
#include "sylar/config.h"
#include "sylar/http/http.h"
#include "sylar/http/http_connection.h"

namespace sylar{
namespace tinyrpc{
class RpcConnection: public http::HttpConnection{
friend class RpcConnectionPool;
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
                                const google::protobuf::Message *request,
                                http::HttpRequest::ptr http_req);

    /**
     * @brief 将收到的HttpResponse，拆解得到需要的数据
     */
    static bool getFromHttpResponse(http::HttpResponse::ptr http_response,
                                    google::protobuf::Message *response);
};


// todo：指的注意的是，通过连接池操作RpcConnection有两种方式
// （1）从RpcConnectionPool中获取RpcConnection指针，通过这个指针来操作。
// （2）直接通过RpcConnectionPool中提供的doRequest接口来实现Http报文的读写。（内部其实还是通过获取一个conn指针来处理的）
class RpcConnectionPool: public http::HttpConnectionPool{
public:
    typedef std::shared_ptr<RpcConnectionPool> ptr;

    RpcConnectionPool(const std::string& host
                       ,uint32_t port
                       ,const std::string& vhost = ""
                       ,uint32_t max_size = 30
                       ,uint32_t max_alive_time = 1000*10
                       ,uint32_t max_request = 5);

    /**
     * @brief 从请求池中获取一个连接
     * @note 如果没有可用的连接，则会新建一个连接并加入到池，
     *       但连接总数受 max_size 限制，达到上限且无空闲连接时返回 nullptr；
     *       每次从池中取连接时，都会刷新一遍连接池，把超时、对端半关闭、
     *       已达到复用次数上限的连接删除
     */
    RpcConnection::ptr getConnection();
    
    
    /**
     * @brief 发送HTTP请求(构造HttRequest，间接调用doRequest(HttpRequest::ptr req, uint64_t timeout_ms)接口)
     * @param[in] method 请求类型
     * @param[in] uri 请求的url
     * @param[in] timeout_ms 超时时间(毫秒)
     * @param[in] headers HTTP请求头部参数
     * @param[in] body 请求消息体
     * @return 返回HTTP结果结构体
     */
    http::HttpResult::ptr doRequest(http::HttpMethod http_method
                                    , const std::string& url
                                    , const google::protobuf::MethodDescriptor *method
                                    , google::protobuf::Message *request
                                    , uint64_t timeout_ms
                                    , uint8_t version = 0x11);

    /**
     * @brief 发送HTTP请求（真正的发送和接受操作）
     * @param[in] req 请求结构体
     * @param[in] timeout_ms 超时时间(毫秒)
     * @return 返回HTTP结果结构体
     */
    http::HttpResult::ptr doRequest(http::HttpRequest::ptr req, uint64_t timeout_ms);
    
    /**
     * @brief 发送HTTP请求
     * @param[in] req 请求结构体
     * @param[in] timeout_ms 超时时间(毫秒)
     * @return 返回HttpResponse
     */
    bool doRequest(const google::protobuf::MethodDescriptor *method
                    , google::protobuf::RpcController *controller
                    , const google::protobuf::Message *request
                    , google::protobuf::Message *response
                    , uint64_t timeout_ms);
    
    /**
     * @brief 惰性初始化，首次调用时才建池
     * @return RpcConnectionPool::ptr 
     */
    static RpcConnectionPool::ptr getPool() {
        // 三参数 Lookup：若调用方未注册则自动注册（带默认值），避免单参数 Lookup 返回 nullptr
        static RpcConnectionPool::ptr pool(new RpcConnectionPool(
            sylar::Config::Lookup("rpc.server_ip", std::string("127.0.0.1"), "rpc server ip")->getValue(),
            sylar::Config::Lookup("rpc.server_port", (int32_t)8008, "rpc server port")->getValue()));
        return pool;
    }

private:
    static void ReleasePtr(RpcConnection* ptr, RpcConnectionPool* pool);
private:
std::list<RpcConnection*> m_rpc_conns;
};
}   //namespace tinyrpc
} //namespace sylar
#endif

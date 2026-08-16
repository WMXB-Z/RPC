#include "rpc_connection.h"
#include <memory>
#include "sylar/http/http.h"
#include "sylar/http/http_connection.h"
#include "sylar/util.h"
#include "sylar/log.h"

namespace sylar {
namespace tinyrpc {
static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

RpcConnection::RpcConnection(Socket::ptr sock, bool owner) : http::HttpConnection(sock, owner) {}

bool RpcConnection::putToHttpRequest(const google::protobuf::MethodDescriptor *method,
                                     const google::protobuf::Message *request, 
                                     http::HttpRequest::ptr http_req) {
    // 获取服务名和方法名
    const google::protobuf::ServiceDescriptor *service_desc = method->service();
    // std::string service_name = service_desc->name();
    // std::string method_name = method->name();

    std::string param_str;
    if (!request->SerializeToString(&param_str)) {
        return false;
    }

    http_req->setMethod(sylar::http::HttpMethod::POST);
    http_req->setPath('/' + service_desc->name() + '/' + method->name());
    http_req->setVersion(0x11);
    http_req->setHeader("service_name", service_desc->name());
    http_req->setHeader("method_name", method->name());
    http_req->setBody(param_str);

    return true;
}

bool RpcConnection::getFromHttpResponse(http::HttpResponse::ptr http_res, google::protobuf::Message *response) {
    // todo: 对RPC协议帧进行 反序列化 和拆解的过程
    const std::string &param_str = http_res->getBody();
    if (!response->ParseFromString(param_str)) {
        return false;
    }
    return true;
}

RpcConnectionPool::RpcConnectionPool(const std::string& host
                       ,uint32_t port
                       ,const std::string& vhost
                       ,uint32_t max_size
                       ,uint32_t max_alive_time
                       ,uint32_t max_request)
                       : http::HttpConnectionPool(host,port,vhost,max_size,max_alive_time,max_request){}

RpcConnection::ptr RpcConnectionPool::getConnection() {
    uint64_t now_ms = sylar::GetCurrentMS();
    std::vector<RpcConnection*> invalid_conns;
    RpcConnection* ptr = nullptr;
    bool need_create = false;
    {
        MutexType::Lock lock(m_mutex);
        while(!m_rpc_conns.empty()) {
            auto conn = *m_rpc_conns.begin();
            m_rpc_conns.pop_front();
            if(!conn->isConnected() || !conn->isPeerAlive()) {
                invalid_conns.push_back(conn);
                continue;
            }
            if((conn->m_createTime + m_maxAliveTime) < now_ms) {
                invalid_conns.push_back(conn);
                continue;
            }
            ptr = conn;
            break;
        }

        if(!ptr) {
            if(m_total.load() >= (int32_t)m_maxSize) {
                // 池已满且没有空闲连接，不再新建
                need_create = false;
            } else {
                // 在锁内先占用名额，避免多线程并发超建
                ++m_total;
                need_create = true;
            }
        }
    }

    for(auto i : invalid_conns) {
        delete i;
    }
    m_total -= (int32_t)invalid_conns.size();

    if(!ptr && !need_create) {
        return nullptr;
    }

    if(need_create) {
        IPAddress::ptr addr = Address::LookupAnyIPAddress(m_host);
        if(!addr) {
            SYLAR_LOG_ERROR(g_logger) << "get addr fail: " << m_host;
            --m_total;
            return nullptr;
        }
        addr->setPort(m_port);
        Socket::ptr sock = Socket::CreateTCP(addr);
        if(!sock) {
            SYLAR_LOG_ERROR(g_logger) << "create sock fail: " << *addr;
            --m_total;
            return nullptr;
        }
        if(!sock->connect(addr)) {
            SYLAR_LOG_ERROR(g_logger) << "sock connect fail: " << *addr;
            --m_total;
            return nullptr;
        }

        ptr = new RpcConnection(sock); // 池子里的连接默认长连接
    }

    // 设置共享指针结束时的回调函数
    return RpcConnection::ptr(ptr, std::bind(&RpcConnectionPool::ReleasePtr
                               , std::placeholders::_1, this));

}

http::HttpResult::ptr RpcConnectionPool::doRequest(http::HttpMethod http_method
                                    , const std::string& url
                                    , const google::protobuf::MethodDescriptor *method
                                    , google::protobuf::Message *request
                                    , uint64_t timeout_ms
                                    , uint8_t version){

    std::string param_str;
    if (!request->SerializeToString(&param_str)) {
        return nullptr;
    }                                    
    const google::protobuf::ServiceDescriptor *service_desc = method->service();
    http::HttpRequest::ptr http_req = std::make_shared<http::HttpRequest>();
    http_req->setMethod(http_method);

    http_req->setPath(url); //这里安装url设置访问路径
    http_req->setVersion(version);
    http_req->setHeader("service_name", service_desc->name());
    http_req->setHeader("method_name", method->name());
    http_req->setBody(param_str);                                
    // req->setClose(false);
    bool has_host = false;

    if(!has_host) {
        if(m_vhost.empty()) {
            http_req->setHeader("Host", m_host);
        } else {
            http_req->setHeader("Host", m_vhost);
        }
    }

    return doRequest(http_req, timeout_ms);
}


http::HttpResult::ptr RpcConnectionPool::doRequest(http::HttpRequest::ptr req, uint64_t timeout_ms){
    auto conn = getConnection();
    if(!conn) {
        return std::make_shared<http::HttpResult>((int)http::HttpResult::Error::POOL_GET_CONNECTION
                , nullptr, "pool host:" + m_host + " port:" + std::to_string(m_port));
    }
    auto sock = conn->getSocket();
    if(!sock) {
        return std::make_shared<http::HttpResult>((int)http::HttpResult::Error::POOL_INVALID_CONNECTION
                , nullptr, "pool host:" + m_host + " port:" + std::to_string(m_port));
    }
    sock->setRecvTimeout(timeout_ms);

    // 根据conn自身的 keep-alive 自动设置请求的该req的 m_close（用户显式指定 Connection 头时则由用户设置决定）
    if (req->getHeader("connection").empty()) {
        req->setClose(!conn->isKeepAlive());
    }

    int rt = conn->sendRequest(req);
    if(rt == 0) {
        return std::make_shared<http::HttpResult>((int)http::HttpResult::Error::SEND_CLOSE_BY_PEER
                , nullptr, "send request closed by peer: " + sock->getRemoteAddress()->toString());
    }
    if(rt < 0) {
        return std::make_shared<http::HttpResult>((int)http::HttpResult::Error::SEND_SOCKET_ERROR
                    , nullptr, "send request socket error errno=" + std::to_string(errno)
                    + " errstr=" + std::string(strerror(errno)));
    }
    auto rsp = conn->recvResponse();
    if(!rsp) {
        return std::make_shared<http::HttpResult>((int)http::HttpResult::Error::TIMEOUT
                    , nullptr, "recv response timeout: " + sock->getRemoteAddress()->toString()
                    + " timeout_ms:" + std::to_string(timeout_ms));
    }

    // 服务端声明将关闭连接（connection: close）时，本地主动 将socket close，
    // ReleasePtr 的 isConnected() 检查会判定不可复用并销毁，死连接不会回池
    if (rsp->isClose()) {
        conn->close();
    }
    return std::make_shared<http::HttpResult>((int)http::HttpResult::Error::OK, rsp, "ok");
}

bool RpcConnectionPool::doRequest(const google::protobuf::MethodDescriptor *method
                                  , google::protobuf::RpcController *controller
                                  , const google::protobuf::Message *request
                                  , google::protobuf::Message *response
                                  , uint64_t timeout_ms){
    if(!method || !controller || !request || !response){
        std::cout << "method, controller, request, response must be not nullptr" << std::endl;
        return false;
    }

    // 获取服务名和方法名
    const google::protobuf::ServiceDescriptor *service_desc = method->service();

    std::string param_str;
    if (!request->SerializeToString(&param_str)) {
        controller->SetFailed("request message SeralizeToString failed!");
        return false;
    }

    // 创建Http request报文
    http::HttpRequest::ptr http_req = std::make_shared<http::HttpRequest>();
    http_req->setMethod(sylar::http::HttpMethod::POST);
    http_req->setPath('/' + service_desc->name() + '/' + method->name());
    http_req->setVersion(0x11);
    http_req->setHeader("service_name", service_desc->name());
    http_req->setHeader("method_name", method->name());
    http_req->setBody(param_str);

    // 拿到http response报文，并解析
    auto http_result = doRequest(http_req, timeout_ms);
    if (!http_result || http_result->result != (int)http::HttpResult::Error::OK) {
        controller->SetFailed(http_result ? http_result->error : "rpc doRequest error!");
        return false;
    }
    http::HttpResponse::ptr http_res = http_result->response;

    if(http_res->getStatus() != http::HttpStatus::OK){
        controller->SetFailed("http server error!");
        return false;
    }

    const std::string& ans_str = http_res->getBody();
    if (!response->ParseFromArray(ans_str.data(),ans_str.length())) {
        controller->SetFailed("response parse from string failed!!");
        return false;
    }

    return true;
}

void  RpcConnectionPool::ReleasePtr(RpcConnection* ptr, RpcConnectionPool* pool){
    ++ptr->m_request;   //ptr对应的连接使用次数+1
    if(!ptr->isConnected() || !ptr->isPeerAlive() || !ptr->isKeepAlive()
            || ((ptr->m_createTime + pool->m_maxAliveTime) < sylar::GetCurrentMS())
            || (ptr->m_request >= pool->m_maxRequest)) {
        // isConnected() 只能检查本地 socket 是否关闭；isPeerAlive() 通过 MSG_PEEK
        // 探测对端是否已 FIN，避免把半关闭的连接放回池中影响下次复用
        delete ptr;
        --pool->m_total;
        return;
    }
    MutexType::Lock lock(pool->m_mutex);
    pool->m_rpc_conns.push_back(ptr);
}


}  // namespace tinyrpc
}  // namespace sylar

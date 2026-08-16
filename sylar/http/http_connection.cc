/**
 * @file http_connection.cc
 * @brief HTTP客户端类实现
 */

#include "http_connection.h"
#include "http_parser.h"
#include "../log.h"
#include "../hook.h"

#include <cerrno>
#include <sys/socket.h>

namespace sylar {
namespace http {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

std::string HttpResult::toString() const {
    std::stringstream ss;
    ss << "[HttpResult result=" << result
       << " error=" << error
       << " response=" << (response ? response->toString() : "nullptr")
       << "]";
    return ss.str();
}

HttpConnection::HttpConnection(Socket::ptr sock, bool owner, bool keepalive)
    :SocketStream(sock, owner) , m_keepalive(keepalive){
    m_buff_size = HttpRequestParser::GetHttpRequestBufferSize();
    m_buffer.reset(new char[m_buff_size + 1], [](char *ptr) {
        delete[] ptr;
    });
    m_offset = 0;
    m_createTime =  GetCurrentMS();
}

HttpConnection::~HttpConnection() {
    SYLAR_LOG_DEBUG(g_logger) << "HttpConnection::~HttpConnection";
}

bool HttpConnection::isPeerAlive() {
    Socket::ptr sock = getSocket();
    if(!sock || !sock->isConnected()) {
        return false;
    }

    // 必须临时关闭 hook：Socket::recv 内部的 ::recv 会被 hook 拦截，
    // 在带 SO_RCVTIMEO 的非阻塞 fd 上会注册事件并让出协程，导致探测阻塞超时
    bool hook_enable = sylar::is_hook_enable();
    if(hook_enable) {
        sylar::set_hook_enable(false);
    }
    char c = 0;
    ssize_t n = sock->recv(&c, 1, MSG_PEEK | MSG_DONTWAIT);
    if(hook_enable) {
        sylar::set_hook_enable(true);
    }

    if(n == 0) {
        // 对端已发送 FIN（半关闭/已关闭），连接不可复用
        return false;
    }
    if(n > 0) {
        // 空闲连接上还有未读取的数据，说明上一次响应未完整消费或对端行为异常，为安全不复用
        return false;
    }
    if(errno == EAGAIN || errno == EWOULDBLOCK) {
        // 无数据且本地状态正常，连接可复用
        return true;
    }
    return false;
}

HttpResponse::ptr HttpConnection::recvResponse() {
    HttpResponseParser::ptr parser(new HttpResponseParser);
    do {
        // 先解析缓冲区中已有的数据（上一次粘包剩余的字节）
        size_t nparse = parser->execute(m_buffer.get(), m_offset);
        if (parser->hasError()) {
            close();
            return nullptr;
        }
        m_offset -= (int)nparse;
        if (parser->isFinished()) {
            break;
        }
        if (m_offset == (int)m_buff_size) { //缓存区已经读满了
            close();
            return nullptr;
        }
        // 数据不足一条完整消息，继续从 socket 读取
        int len = read(m_buffer.get() + m_offset, m_buff_size - m_offset);
        if (len <= 0) {
            close();
            return nullptr;
        }
        m_offset += len;
        m_buffer.get()[m_offset] = '\0';
    } while (true);

    return parser->getData();
}

int HttpConnection::sendRequest(HttpRequest::ptr rsp) {
    std::stringstream ss;
    ss << *rsp;
    std::string data = ss.str();
    return writeFixSize(data.c_str(), data.size());
}

HttpResult::ptr HttpConnection::DoGet(const std::string& url
                            , uint64_t timeout_ms
                            , const std::map<std::string, std::string>& headers
                            , const std::string& body) {
    Uri::ptr uri = Uri::Create(url);
    if(!uri) {
        return std::make_shared<HttpResult>((int)HttpResult::Error::INVALID_URL
                , nullptr, "invalid url: " + url);
    }
    return DoGet(uri, timeout_ms, headers, body);
}

HttpResult::ptr HttpConnection::DoGet(Uri::ptr uri
                            , uint64_t timeout_ms
                            , const std::map<std::string, std::string>& headers
                            , const std::string& body) {
    return DoRequest(HttpMethod::GET, uri, timeout_ms, headers, body);
}

HttpResult::ptr HttpConnection::DoPost(const std::string& url
                            , uint64_t timeout_ms
                            , const std::map<std::string, std::string>& headers
                            , const std::string& body) {
    Uri::ptr uri = Uri::Create(url);
    if(!uri) {
        return std::make_shared<HttpResult>((int)HttpResult::Error::INVALID_URL
                , nullptr, "invalid url: " + url);
    }
    return DoPost(uri, timeout_ms, headers, body);
}

HttpResult::ptr HttpConnection::DoPost(Uri::ptr uri
                            , uint64_t timeout_ms
                            , const std::map<std::string, std::string>& headers
                            , const std::string& body) {
    return DoRequest(HttpMethod::POST, uri, timeout_ms, headers, body);
}

HttpResult::ptr HttpConnection::DoRequest(HttpMethod method
                            , const std::string& url
                            , uint64_t timeout_ms
                            , const std::map<std::string, std::string>& headers
                            , const std::string& body) {
    Uri::ptr uri = Uri::Create(url);
    if(!uri) {
        return std::make_shared<HttpResult>((int)HttpResult::Error::INVALID_URL
                , nullptr, "invalid url: " + url);
    }
    return DoRequest(method, uri, timeout_ms, headers, body);
}

HttpResult::ptr HttpConnection::DoRequest(HttpMethod method
                            , Uri::ptr uri
                            , uint64_t timeout_ms
                            , const std::map<std::string, std::string>& headers
                            , const std::string& body) {
    HttpRequest::ptr req = std::make_shared<HttpRequest>();
    req->setPath(uri->getPath());
    req->setQuery(uri->getQuery());
    req->setFragment(uri->getFragment());
    req->setMethod(method);
    bool has_host = false;

    // request前，应根据"connection"设置req的m_close属性
    for(auto& i : headers) {
        if(strcasecmp(i.first.c_str(), "connection") == 0) {
            if(strcasecmp(i.second.c_str(), "keep-alive") == 0) {
                req->setClose(false);
            }else if(strcasecmp(i.second.c_str(), "close") == 0){
                req->setClose(true);
            }
        }
        if(!has_host && strcasecmp(i.first.c_str(), "host") == 0) {
            has_host = !i.second.empty();
        }
        //在toString时connection会根据m_close自定添加，但这里依然添加是为了让
        req->setHeader(i.first, i.second);
    }

    if(!has_host) {
        req->setHeader("Host", uri->getHost());
    }
    req->setBody(body);
    return DoRequest(req, uri, timeout_ms);
}

HttpResult::ptr HttpConnection::DoRequest(HttpRequest::ptr req
                            , Uri::ptr uri
                            , uint64_t timeout_ms) {
    Address::ptr addr = uri->createAddress();
    if(!addr) {
        return std::make_shared<HttpResult>((int)HttpResult::Error::INVALID_HOST
                , nullptr, "invalid host: " + uri->getHost());
    }
    Socket::ptr sock = Socket::CreateTCP(addr);
    if(!sock) {
        return std::make_shared<HttpResult>((int)HttpResult::Error::CREATE_SOCKET_ERROR
                , nullptr, "create socket fail: " + addr->toString()
                        + " errno=" + std::to_string(errno)
                        + " errstr=" + std::string(strerror(errno)));
    }
    if(!sock->connect(addr)) {
        return std::make_shared<HttpResult>((int)HttpResult::Error::CONNECT_FAIL
                , nullptr, "connect fail: " + addr->toString());
    }
    sock->setRecvTimeout(timeout_ms);
    HttpConnection::ptr conn = std::make_shared<HttpConnection>(sock);  

    // todo：由于本方法是static,这里进行pool中相关的处理是不合理的！
    // 根据conn自身的 keep-alive 自动设置请求的该req的 m_close（用户显式指定 Connection 头时则由用户设置决定）
    // if (req->getHeader("connection").empty()) {
    //     req->setClose(!conn->isKeepAlive());
    // }

    int rt = conn->sendRequest(req);
    if(rt == 0) {
        return std::make_shared<HttpResult>((int)HttpResult::Error::SEND_CLOSE_BY_PEER
                , nullptr, "send request closed by peer: " + addr->toString());
    }
    if(rt < 0) {
        return std::make_shared<HttpResult>((int)HttpResult::Error::SEND_SOCKET_ERROR
                    , nullptr, "send request socket error errno=" + std::to_string(errno)
                    + " errstr=" + std::string(strerror(errno)));
    }
    auto rsp = conn->recvResponse();
    if(!rsp) {
        return std::make_shared<HttpResult>((int)HttpResult::Error::TIMEOUT
                    , nullptr, "recv response timeout: " + addr->toString()
                    + " timeout_ms:" + std::to_string(timeout_ms));
    }

    // 服务端声明将关闭连接（connection: close）时，本地主动 将socket close，
    // ReleasePtr 的 isConnected() 检查会判定不可复用并销毁，死连接不会回池
    // if (rsp->isClose()) {
    //     conn->close();
    // }

    return std::make_shared<HttpResult>((int)HttpResult::Error::OK, rsp, "ok");
}


HttpConnectionPool::HttpConnectionPool(const std::string& host
                                        ,uint32_t port
                                        ,const std::string& vhost
                                        ,uint32_t max_size
                                        ,uint32_t max_alive_time
                                        ,uint32_t max_request)
    :m_host(host)
    ,m_port(port)
    ,m_vhost(vhost)
    ,m_maxSize(max_size)
    ,m_maxAliveTime(max_alive_time)
    ,m_maxRequest(max_request) {
}

HttpConnection::ptr HttpConnectionPool::getConnection() {
    uint64_t now_ms = sylar::GetCurrentMS();
    std::vector<HttpConnection*> invalid_conns;
    HttpConnection* ptr = nullptr;
    bool need_create = false;
    {
        MutexType::Lock lock(m_mutex);
        while(!m_conns.empty()) {
            auto conn = *m_conns.begin();
            m_conns.pop_front();
            // isConnected() 只能反映本地状态，必须再探测对端是否半关闭
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

    // 在一轮conn的获取后，清除pool中失效的conn
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

        ptr = new HttpConnection(sock); // 池子里的连接默认长连接
    }

    // 设置共享指针结束时的回调函数
    return HttpConnection::ptr(ptr, std::bind(&HttpConnectionPool::ReleasePtr
                               , std::placeholders::_1, this));

}

void HttpConnectionPool::ReleasePtr(HttpConnection* ptr, HttpConnectionPool* pool) {
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
    pool->m_conns.push_back(ptr);
}

HttpResult::ptr HttpConnectionPool::doGet(const std::string& url
                          , uint64_t timeout_ms
                          , const std::map<std::string, std::string>& headers
                          , const std::string& body) {
    return doRequest(HttpMethod::GET, url, timeout_ms, headers, body);
}

HttpResult::ptr HttpConnectionPool::doGet(Uri::ptr uri
                                   , uint64_t timeout_ms
                                   , const std::map<std::string, std::string>& headers
                                   , const std::string& body) {
    std::stringstream ss;
    ss << uri->getPath()
       << (uri->getQuery().empty() ? "" : "?")
       << uri->getQuery()
       << (uri->getFragment().empty() ? "" : "#")
       << uri->getFragment();
    return doGet(ss.str(), timeout_ms, headers, body);
}

HttpResult::ptr HttpConnectionPool::doPost(const std::string& url
                                   , uint64_t timeout_ms
                                   , const std::map<std::string, std::string>& headers
                                   , const std::string& body) {
    return doRequest(HttpMethod::POST, url, timeout_ms, headers, body);
}

HttpResult::ptr HttpConnectionPool::doPost(Uri::ptr uri
                                   , uint64_t timeout_ms
                                   , const std::map<std::string, std::string>& headers
                                   , const std::string& body) {
    std::stringstream ss;
    ss << uri->getPath()
       << (uri->getQuery().empty() ? "" : "?")
       << uri->getQuery()
       << (uri->getFragment().empty() ? "" : "#")
       << uri->getFragment();
    return doPost(ss.str(), timeout_ms, headers, body);
}

HttpResult::ptr HttpConnectionPool::doRequest(HttpMethod method
                                    , const std::string& url
                                    , uint64_t timeout_ms
                                    , const std::map<std::string, std::string>& headers
                                    , const std::string& body) {
    HttpRequest::ptr req = std::make_shared<HttpRequest>();
    req->setPath(url);
    req->setMethod(method);
    // req->setClose(false);
    bool has_host = false;

    // request前，应根据"connection"设置req的m_close属性
    for(auto& i : headers) {
        if(strcasecmp(i.first.c_str(), "connection") == 0) {
            if(strcasecmp(i.second.c_str(), "keep-alive") == 0) {
                req->setClose(false);
            }else if(strcasecmp(i.second.c_str(), "close") == 0){
                req->setClose(true);
            }
        }
        if(!has_host && strcasecmp(i.first.c_str(), "host") == 0) {
            has_host = !i.second.empty();
        }
        //在toString时connection会根据m_close自定添加，但这里依然添加是为了让
        req->setHeader(i.first, i.second);
    }

    if(!has_host) {
        if(m_vhost.empty()) {
            req->setHeader("Host", m_host);
        } else {
            req->setHeader("Host", m_vhost);
        }
    }
    req->setBody(body);
    return doRequest(req, timeout_ms);
}

HttpResult::ptr HttpConnectionPool::doRequest(HttpMethod method
                                    , Uri::ptr uri
                                    , uint64_t timeout_ms
                                    , const std::map<std::string, std::string>& headers
                                    , const std::string& body) {
    std::stringstream ss;
    ss << uri->getPath()
       << (uri->getQuery().empty() ? "" : "?")
       << uri->getQuery()
       << (uri->getFragment().empty() ? "" : "#")
       << uri->getFragment();
    return doRequest(method, ss.str(), timeout_ms, headers, body);
}

HttpResult::ptr HttpConnectionPool::doRequest(HttpRequest::ptr req
                                        , uint64_t timeout_ms) {
    auto conn = getConnection();
    if(!conn) {
        return std::make_shared<HttpResult>((int)HttpResult::Error::POOL_GET_CONNECTION
                , nullptr, "pool host:" + m_host + " port:" + std::to_string(m_port));
    }
    auto sock = conn->getSocket();
    if(!sock) {
        return std::make_shared<HttpResult>((int)HttpResult::Error::POOL_INVALID_CONNECTION
                , nullptr, "pool host:" + m_host + " port:" + std::to_string(m_port));
    }
    sock->setRecvTimeout(timeout_ms);

    // 根据conn自身的 keep-alive 自动设置请求的该req的 m_close（用户显式指定 Connection 头时则由用户设置决定）
    if (req->getHeader("connection").empty()) {
        req->setClose(!conn->isKeepAlive());
    }

    int rt = conn->sendRequest(req);
    if(rt == 0) {
        return std::make_shared<HttpResult>((int)HttpResult::Error::SEND_CLOSE_BY_PEER
                , nullptr, "send request closed by peer: " + sock->getRemoteAddress()->toString());
    }
    if(rt < 0) {
        return std::make_shared<HttpResult>((int)HttpResult::Error::SEND_SOCKET_ERROR
                    , nullptr, "send request socket error errno=" + std::to_string(errno)
                    + " errstr=" + std::string(strerror(errno)));
    }
    auto rsp = conn->recvResponse();
    if(!rsp) {
        return std::make_shared<HttpResult>((int)HttpResult::Error::TIMEOUT
                    , nullptr, "recv response timeout: " + sock->getRemoteAddress()->toString()
                    + " timeout_ms:" + std::to_string(timeout_ms));
    }

    // 服务端声明将关闭连接（connection: close）时，本地主动 将socket close，
    // ReleasePtr 的 isConnected() 检查会判定不可复用并销毁，死连接不会回池
    if (rsp->isClose()) {
        conn->close();
    }
    return std::make_shared<HttpResult>((int)HttpResult::Error::OK, rsp, "ok");
}

}
}

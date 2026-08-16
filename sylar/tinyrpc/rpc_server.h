#ifndef RPC_SERVICE
#define RPC_SERVICE

#include <memory>
#include "sylar/http/http_server.h"
#include "sylar/iomanager.h"
namespace sylar{
namespace tinyrpc{

// RpcService基本就复用HttpServer的提供的接口功能即可
class RpcServer : public http::HttpServer{
public:
    typedef std::shared_ptr<RpcServer> ptr;
    RpcServer(bool keepalive = false
               ,sylar::IOManager* worker = sylar::IOManager::GetThis()
               ,sylar::IOManager* io_worker = sylar::IOManager::GetThis()
               ,sylar::IOManager* accept_worker = sylar::IOManager::GetThis())
              : http::HttpServer(keepalive,worker,io_worker,accept_worker){ }
};
}
}
#endif
#ifndef RPC_FUNC_SERVLET
#define RPC_FUNC_SERVLET

#include <google/protobuf/message.h>
#include <google/protobuf/service.h>
#include <memory>
#include "sylar/http/servlet.h"
namespace sylar{
namespace tinyrpc{
/**
 * @brief RPC 函数式 Servlet
 * @details 与客户端协议（rpc_connection.cc）对应：
 *         - HttpRequest header: service_name / method_name
 *         - HttpRequest body: 序列化后的 request 参数
 *         通过持有的 protobuf Service 反射出 method 和 request/response 类型，
 *         反序列化后调用 Service::CallMethod，把结果序列化进 HttpResponse body。
 */
class RpcFuncServlet : public http::Servlet {
public:
    typedef std::shared_ptr<RpcFuncServlet> ptr;

    /**
     * @brief 构造函数
     * @param[in] service 注册的 protobuf 服务（持有所有权，保证服务存活时间不短于 servlet）
     */
    RpcFuncServlet(std::shared_ptr<google::protobuf::Service> service);

    
    virtual int32_t handle(http::HttpRequest::ptr request
                   , http::HttpResponse::ptr response
                   , http::HttpSession::ptr session) override;
private:
    /// !具体服务对象（提供描述符与反射能力，由 shared_ptr 管理生命周期）
    std::shared_ptr<google::protobuf::Service> m_service;
};
}
}
#endif

#ifndef RPC_FUNC_SERVLET
#define RPC_FUNC_SERVLET

#include <google/protobuf/message.h>
#include <google/protobuf/service.h>
#include <memory>
#include "sylar/http/servlet.h"
namespace sylar{
namespace tinyrpc{
 /**
 * @brief 函数式Servlet
 */
class RpcFuncServlet : public http::Servlet {
public:
    /// 智能指针类型定义
    typedef std::shared_ptr<RpcFuncServlet> ptr;
    /// 函数回调类型定义
    // typedef std::function<int32_t (http::HttpRequest::ptr request
    //                , http::HttpResponse::ptr response
    //                , http::HttpSession::ptr session)> callback;

    // 这里要做的回调函数就是ServiceImp中的callMethod，所以参数类型较确定，不确定的是具体哪一个ServiceImp对象调用
    typedef std::function<void (const google::protobuf::MethodDescriptor* method,
                                   google::protobuf::RpcController* controller,
                                   google::protobuf::Message* request,
                                   google::protobuf::Message* response,
                                   google::protobuf::Closure* done)> callback;
    /**
     * @brief 构造函数
     * @param[in] cb 回调函数
     */
    RpcFuncServlet(callback cb);
    virtual int32_t handle(http::HttpRequest::ptr request
                   , http::HttpResponse::ptr response
                   , http::HttpSession::ptr session) override;
private:
    /// 回调函数
    callback m_cb;
};
}
}
#endif
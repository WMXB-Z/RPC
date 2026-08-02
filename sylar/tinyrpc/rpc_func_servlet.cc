#include "rpc_func_servlet.h"
#include "sylar/log.h"
#include <google/protobuf/service.h>

namespace sylar {
namespace tinyrpc {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

RpcFuncServlet::RpcFuncServlet(std::shared_ptr<google::protobuf::Service> service)
    : Servlet("RpcFuncServlet")
    , m_service(service) {
}

int32_t RpcFuncServlet::handle(http::HttpRequest::ptr http_req
               , http::HttpResponse::ptr http_rsp
               , http::HttpSession::ptr session) {
    // 1. 校验服务名，防止请求路由到其他 service
    std::string service_name = http_req->getHeader("service_name");
    if (service_name != m_service->GetDescriptor()->name()) {
        SYLAR_LOG_ERROR(g_logger) << "service name mismatch: " << service_name;
        return -1;
    }

    // 2. 根据 header 中的方法名找到方法描述
    std::string method_name = http_req->getHeader("method_name");
    const google::protobuf::MethodDescriptor* method =
        m_service->GetDescriptor()->FindMethodByName(method_name);
    if (!method) {
        SYLAR_LOG_ERROR(g_logger) << "method not found: " << method_name;
        return -1;
    }

    // !3. 反射创建 request/response，并从 body 反序列化请求参数
    // 即：通过 方法描述对象method，拿到请求消息原型、响应消息原型。并根据原型对象new 一个对应类型的实例对象
    std::unique_ptr<google::protobuf::Message> request(
        m_service->GetRequestPrototype(method).New());
    std::unique_ptr<google::protobuf::Message> response(
        m_service->GetResponsePrototype(method).New());
    if (!request->ParseFromString(http_req->getBody())) {
        SYLAR_LOG_ERROR(g_logger) << "request parse from string error";
        return -1;
    }

    // 4. 调用具体业务（多态进入 ServiceImpl 重写的 各类 等方法）
    m_service->CallMethod(method, nullptr, request.get(), response.get(), nullptr);

    // 5. 把 response 序列化进 http 响应 body
    std::string result;
    if (!response->SerializeToString(&result)) {
        SYLAR_LOG_ERROR(g_logger) << "response serialize to string error";
        return -1;
    }
    http_rsp->setBody(result);
    return 0;
}

}  // namespace tinyrpc
}  // namespace sylar

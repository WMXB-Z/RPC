
#include "rpc_func_servlet.h"
#include <google/protobuf/service.h>
#include "tests/tinyrpc/echo_add.poto.pb.h"
namespace sylar{
namespace tinyrpc{
    RpcFunctionServlet::RpcFunctionServlet(callback cb) : Servlet("FunctionServlet") ,m_cb(cb){}

    int32_t RpcFunctionServlet::handle(http::HttpRequest::ptr http_res
                   , http::HttpResponse::ptr http_rep
                   , http::HttpSession::ptr session) {

        // todo:从request拿出body、服务名、方法名，并反序列化为具体的message类型，
        // 获得service的描述对象，通过该对象获取需要调用的具体方法的描述对象
        const google::protobuf::ServiceDescriptor* service_desc = EchoAddService::descriptor();
        std::string method_name = http_res->getHeader("method_name");
        const google::protobuf::MethodDescriptor* method = service_desc->FindMethodByName(method_name);
        const std::string &param_str = http_res->getBody();
        
        // todo：这里有些问题，只能显示获得具体类型的request、response吗？
        auto* request = AddRequest::default_instance().New();
        auto* response = AddResponse::default_instance().New();

        if (!request->ParseFromString(param_str)) {
            std::cout << "request parse from string error in Servlet::handle" << std::endl;
            delete request;
            delete response;
            return false;
        }
        // 根据message中的处理调用m_cb（这里本质上因该是一个serverimpl的callMethod）
        // callMethod(method, nullptr, request, response, nullptr);
        int res = m_cb(method, nullptr, request, response, nullptr);

        // 将执行结果放入http_res中
        std::string reslut;
        if(!response->SerializeToString(&reslut)){
            std::cout << "response parse serialize to string error in Servlet::handle" << std::endl;
            delete request;
            delete response;
            return false;
        }
        http_rep->setBody(reslut);

        delete request;
        delete response;
        return res == 0;
    }
}
}

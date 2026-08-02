#ifndef ECHO_ADD_SERVICEIMP
#define ECHO_ADD_SERVICEIMP
#include "echo_add.poto.pb.h"
namespace sylar{
namespace tinyrpc{
class EchoAddServiceImp : public EchoAddService{

    std::string echo(){
        std::cout << "queryEcho RPC is successful";
        return "hello this is service!";
    }

    int32_t add(int32_t a, int32_t b){
        std::cout << "aueryAdd PRC is successful";
        return a + b;
    }

    
    void queryEcho(::google::protobuf::RpcController* controller,
                    const ::sylar::tinyrpc::EchoRequest* request,
                    ::sylar::tinyrpc::EchoResponse* response,
                    ::google::protobuf::Closure* done) override{

        std::string response_str = echo();
        response->set_mess(response_str);
        response->set_success(true);

        if(done){
            done->Run();
        }
    }

   void queryAdd(::google::protobuf::RpcController* controller,
                const ::sylar::tinyrpc::AddRequest* request,
                ::sylar::tinyrpc::AddResponse* response,
                ::google::protobuf::Closure* done) override{

    // todo：从request中取出需要参数，并调用实际的服务函数进行处理将
        int32_t a = request->a();
        int32_t b = request->b();
        int32_t sum = add(a, b);
        
        response->set_sum(sum);
        response->set_success(true);
        if(done){
            done->Run();
        }
    }
};
}
}

#endif
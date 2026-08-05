#ifndef ECHO_ADD_SERVICEIMP
#define ECHO_ADD_SERVICEIMP
#include <memory>
#include "echo_add.poto.pb.h"
namespace sylar{
namespace tinyrpc{
class EchoAddServiceImp : public EchoAddService{
public:
    typedef std::shared_ptr<EchoAddServiceImp> ptr ;
    std::string echo(){
        // std::cout << "queryEcho RPC is successful";
        return "hello this is service!";
    }

    int32_t add(int32_t a, int32_t b){
        // std::cout << "aueryAdd PRC is successful";
        return a + b;
    }

    // int32_t sub(int32_t a, int32_t b){
    //     std::cout << "auerySub PRC is successful";
    //     return a - b;
    // }

    
    void queryEcho(::google::protobuf::RpcController* controller,
                    const ::sylar::tinyrpc::EchoRequest* request,
                    ::sylar::tinyrpc::EchoResponse* response,
                    ::google::protobuf::Closure* done) override{

        std::string response_str = echo();
        sylar::tinyrpc::ResultCode* code = response->mutable_result();
        code->set_errcode(0);
        code->set_errmsg("");
        response->set_mess(response_str);

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
        sylar::tinyrpc::ResultCode* code = response->mutable_result();
        code->set_errcode(0);
        code->set_errmsg("");
        response->set_sum(sum);
        if(done){
            done->Run();
        }
    }

    // void querySub(::google::protobuf::RpcController* controller,
    //             const ::sylar::tinyrpc::SubRequest* request,
    //             ::sylar::tinyrpc::SubResponse* response,
    //             ::google::protobuf::Closure* done) override{

    //     int32_t a = request->a();
    //     int32_t b = request->b();
    //     int32_t ans = sub(a, b);
    //     sylar::tinyrpc::ResultCode* code = response->mutable_result();
    //     code->set_errcode(0);
    //     code->set_errmsg("");
    //     response->set_ans(ans);
    //     if(done){
    //         done->Run();
    //     }
    // }
};
}
}

#endif
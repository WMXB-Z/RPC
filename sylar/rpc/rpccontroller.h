#ifndef RPCCONTROLLER_H
#define RPCCONTROLLER_H

#include <google/protobuf/service.h>
#include <string>

namespace sylar {
namespace rpc {
//protobuf RPC调用的“控制通道”，不承载业务数据，而负责描述 RPC 调用状态（失败、取消、错误信息、超时等）。
class MprpcController : public google::protobuf::RpcController {
public:
    MprpcController();
    void Reset();
    bool Failed() const;
    std::string ErrorText() const;
    void SetFailed(const std::string& reason);

    void StartCancel();
    bool IsCanceled() const;
    void NotifyOnCancel(google::protobuf::Closure* callback);

   private:
    // rpc方法执行过程中的状态
    bool m_failed;

    // 方法执行过程中的错误信息
    std::string m_errText;
};
}  // namespace rpc

}  // namespace sylar

#endif
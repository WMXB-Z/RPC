#ifndef RPCCONTROLLER_H
#define RPCCONTROLLER_H

#include <google/protobuf/service.h>
#include <memory>
#include <string>

namespace sylar 
{
namespace tinyrpc
{
//protobuf RPC调用的“控制通道”，不承载业务数据，而负责描述 RPC 调用状态（失败、取消、错误信息、超时等）。
class TinyRpcController : public google::protobuf::RpcController {
public:
    typedef std::shared_ptr<TinyRpcController> ptr;
    
    TinyRpcController() = default;

    ~TinyRpcController() = default;

    /**
     * @brief 重置RPC controller的状态
     */
    void Reset() override {
        m_is_failed = false;
        m_is_cancled = false;
        m_error_info = "";
    }

    /**
     * @brief 返回RPC服务调用成功与否
     */
    bool Failed() const override{ return m_is_failed;}

    /**
     * @brief 返回本次RPC服务的错误信息
    */
    std::string ErrorText() const override { return m_error_info; }

    /**
     * @brief 设置本次RPC服务的错误信息
    */
    void SetFailed(const std::string& reason) override {
        m_is_failed = true;
        m_error_info = reason;
    }

    void StartCancel() override { }
    
    /**
     * @brief 返回本次RPC是否取消
    */
    bool IsCanceled() const override {return m_is_cancled;}

    void NotifyOnCancel(google::protobuf::Closure* callback) override { }

    /**
     * @brief 设置本次RPC服务调用的服务号，用于标识服务连接
    */
    // std::string getRpcId(){ return m_rpc_id;}
    // void setRpcId(std::string rpc_id){m_rpc_id = rpc_id;}

private:
    //本次Rpc连接的标识
    // std::string m_rpc_id; 
    // rpc调用服务执行成功否
    bool m_is_failed = false;
    // 当前RPC服务的状态
    bool m_is_cancled = false;
    // 出错之后的错误码
    // int m_error_code = 0;
    // 方法执行过程中的错误信息
    std::string m_error_info;
};
}  // namespace rpc

}  // namespace sylar

#endif
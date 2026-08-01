#include "rpccontroller.h"

namespace sylar
{
namespace rpc
{
MprpcController::MprpcController(){ 
    m_failed = false;
    m_errText = "";
}
void MprpcController::Reset(){  //重置RPC状态
    m_failed = false;
    m_errText = "";
}
bool MprpcController::Failed() const{  /// 如何判断RPC是否失败
    return m_failed;
}
std::string MprpcController::ErrorText() const{ // 获取错误信息
    return m_errText;
}
void MprpcController::SetFailed(const std::string & reason){
    m_failed = true;
    m_errText = reason;
}

void MprpcController::StartCancel(){}
bool MprpcController::IsCanceled() const{// 判断是否取消
    return false;
}
void MprpcController::NotifyOnCancel(google::protobuf::Closure * callback){}
} // namespace rpc

} // namespace sylar

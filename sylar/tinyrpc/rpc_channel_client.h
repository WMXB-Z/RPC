#ifndef RPCCHANNEL_H
#define RPCCHANNEL_H

#include <google/protobuf/service.h>
#include <google/protobuf/message.h>
#include <google/protobuf/descriptor.h>
#include <memory>
#include "rpc_connection.h"

namespace sylar
{
namespace tinyrpc
{
/**
 * @brief RPC客户端使用UserServiceRpc_Stub调用远程方法，但底层是通过 RpcChannel 调用 CallMethod
 * @note 每个 RpcChannelClient 持有自己独立的连接池（RpcConnectionPool），
 *       因此创建多个客户端即可获得多条独立 TCP 连接（对应 perf 测试的 -p 参数）
*/
class RpcChannelClient : public google::protobuf::RpcChannel{
public:
    /**
     * @brief 构造函数：创建本客户端独立的连接池
     * @note 地址来自配置 rpc.server_ip / rpc.server_port（需在创建前已加载配置）
     */
    RpcChannelClient();

    /**
    * @brief 所有使用 stub
              代理类调用的rpc方法都走到这里，进行序列化和网络发送，并接收RPC服务方的response
    * @param method 远程方法
    * @param controller 控制类，用于设置超时时间等
    * @param request 请求参数
    * @param response 返回结果
    * @param done 在实际服务执行后，进一步触发的回调函数
    */
    virtual void CallMethod(const google::protobuf::MethodDescriptor *method,
                            google::protobuf::RpcController *controller,
                            const google::protobuf::Message *request,
                            google::protobuf::Message *response,
                            google::protobuf::Closure *done) override;

private:
    /// 本客户端独立的连接池
    RpcConnectionPool::ptr m_pool;
};
} 

} // namespace sylar


#endif
#include "rpc_connection.h"
#include "sylar/tinyrpc/rpc_header.pb.h"

/**

+----------------+
| send_all_size  | 4字节
+----------------+
| header_size    | 4字节
+----------------+
| RpcHeader二进制 |
|                |
| service_name   |
| method_name    |
| args_size      |
+----------------+
| request参数二进制 |
+----------------+ 
 */
namespace sylar {
namespace tinyrpc {

bool RpcConnection::encode(const google::protobuf::MethodDescriptor *method, 
                           google::protobuf::RpcController *controller, 
                           const google::protobuf::Message *request,
                           std::string &rpc_send_str) {

    // todo:这里要完成的是 RPC协议帧的封装 以及 编码（序列化）
    // // 获取服务名以及需要调用的方法
    // const google::protobuf::ServiceDescriptor *service_info = method->service();
    // std::string service_name = service_info->name();
    // std::string method_name = method->name();

    // // 将request进行序列化，转为特定编码格式（这里交由TCP层去实现）
    // std::string param_str;
    // if (!request->SerializeToString(&param_str)) {
    //     controller->SetFailed("Serialize reauest error!");// 设置RpcController相关的状态信息
    //     return false;
    // }
    // // 设置 rpcheader中的内容
    // RpcHeader rpcheader;
    // uint32_t param_size = param_str.size();
    // rpcheader.set_service_name(service_name);
    // rpcheader.set_method_name(method_name);
    // rpcheader.set_args_size(param_size);

    // // 序列化 rpcheader部分
    // std::string rpcheader_str;
    // if (!rpcheader.SerializeToString(&rpcheader_str)) {
    //     controller->SetFailed("Serialize request rpcheader error !!!");
    //     return false;
    // }

    // uint32_t send_all_size = SEND_RPC_HEADERSIZE + rpcheader_str.size() + param_str.size();
    // uint32_t rpcheader_size = rpcheader_str.size();

    // // 组装 rpc 请求帧
    // // send_all_size，固定4B
    // rpc_send_str += std::string((char *)&send_all_size, 4);
    // // rpcheader_size，固定4B
    // rpc_send_str += std::string((char *)&rpcheader_size, 4);
    // // rpcheader部分的二进制串
    // rpc_send_str += rpcheader_str;
    // // 参数部分的二进制串
    // rpc_send_str += param_str;
    
    return true;
}

bool RpcConnection::decode(google::protobuf::RpcController *controller,
                           google::protobuf::Message *response,
                           std::string& rpc_recv_str){
    // todo: 对RPC协议帧进行 反序列化 和拆解的过程
    // revc_buf：response部分的字符串首地址，recv_size这部分字符串的长度
    // 反序列化 response
    // if (!response->ParseFromArray(recv_buf, recv_size)){
    //     controller->SetFailed("parse response error !!!");
    //     return false;
    // }
    return true;
}

    /**
     * @brief 将序列化之后的RPC请求，通过stream进行TCP传输
     */
    bool sendRequest(){
        // todo: 调用encode进行封装 并 调用socket的send进行TCP传输
    }

    /**
     * @brief 接收对端的回复，并反序列化为RPC响应报文
     * @return true 
     * @return false 
     */
    bool recvResponse(){
        // todo:调用socket的recv接收TCP传输，并利用decode进行拆包
    }

}  // namespace tinyrpc
}  // namespace sylar
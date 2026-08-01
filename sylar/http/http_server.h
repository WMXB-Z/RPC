/**
 * @file http_server.h
 * @brief HTTP服务器封装
 */

#ifndef __SYLAR_HTTP_HTTP_SERVER_H__
#define __SYLAR_HTTP_HTTP_SERVER_H__

#include "../tcp_server.h"
#include "http_session.h"
#include "servlet.h"

namespace sylar {
namespace http {

/**
 * @brief HTTP服务器类
 */
class HttpServer : public TcpServer {
public:
    /// 智能指针类型
    typedef std::shared_ptr<HttpServer> ptr;

    /**
     * @brief 构造函数（注意，虽然这里分级设置了调度器，但本系统实际上并未做的这么复杂，只使用了io_worker 和accept_worker，且默认情况下二者是同一个）
     * @param[in] keepalive 是否长连接
     * @param[in] worker 工作调度器（负责调度业务层的任务，比如访问数据库、访问文件等。）
     * @param[in] io_worker io操作调度器（负责调度连接后的io任务）
     * @param[in] accept_worker 接收连接调度器（负责调度socket的连接任务）
     * 一种典型服务器架构：连接接入 → 网络IO → 业务处理 三层分离。把网络事件处理和业务处理隔离，避免一个慢请求拖垮整个网络线程。
     */ 
    HttpServer(bool keepalive = false
               ,sylar::IOManager* worker = sylar::IOManager::GetThis()
               ,sylar::IOManager* io_worker = sylar::IOManager::GetThis()
               ,sylar::IOManager* accept_worker = sylar::IOManager::GetThis());

    /**
     * @brief 获取ServletDispatch
     */
    ServletDispatch::ptr getServletDispatch() const { return m_dispatch;}

    /**
     * @brief 设置ServletDispatch
     */
    void setServletDispatch(ServletDispatch::ptr v) { m_dispatch = v;}

    virtual void setName(const std::string& v) override;
protected:
    virtual void handleClient(Socket::ptr client) override;
private:
    /// 是否支持长连接
    bool m_isKeepalive;
    /// Servlet分发器
    ServletDispatch::ptr m_dispatch;
};

}
}

#endif

/**
 * @file fd_manager.h
 * @brief 文件句柄管理类
 */
#ifndef __FD_MANAGER_H__
#define __FD_MANAGER_H__

#include <memory>
#include <vector>
#include "mutex.h"
#include "singleton.h"

namespace sylar {

/**
 * @brief 文件句柄上下文类
 * @details 管理文件句柄类型(是否socket)是否阻塞,是否关闭,读/写超时时间
 */
class FdCtx : public std::enable_shared_from_this<FdCtx> {
public:
    typedef std::shared_ptr<FdCtx> ptr;
    /**
     * @brief 通过文件句柄构造FdCtx
     */
    FdCtx(int fd);
    /**
     * @brief 析构函数
     */
    ~FdCtx();

    /**
     * @brief 是否初始化完成
     */
    bool isInit() const { return m_isInit;}

    /**
     * @brief 是否socket
     */
    bool isSocket() const { return m_isSocket;}

    /**
     * @brief 是否已关闭
     */
    bool isClose() const { return m_isClosed;}

    /**
     * @brief 记录用户是否主动设置非阻塞
     *（为什么要记录？因为sylar中要配合协程，就需要socket是非阻塞的，而实际的用户意愿 != 系统真实状态。故这里记录下用户的意愿）
     * @param[in] v 是否阻塞
     */
    void setUserNonblock(bool v) { m_userNonblock = v;}

    /**
     * @brief 获取用户是否主动设置的非阻塞
     */
    bool getUserNonblock() const { return m_userNonblock;}

    /**
     * @brief 记录系统是否设置非阻塞
     * @param[in] v 是否阻塞
     */
    void setSysNonblock(bool v) { m_sysNonblock = v;}

    /**
     * @brief 获取系统是否设置为非阻塞（为了协程运行，系统必须强制非阻塞。如果为false，则说明系统不使用协程，强制为阻塞！）
     */
    bool getSysNonblock() const { return m_sysNonblock;}

    /**
     * @brief 设置超时时间
     * @param[in] type 类型SO_RCVTIMEO(读超时), SO_SNDTIMEO(写超时)
     * @param[in] v 时间毫秒
     */
    void setTimeout(int type, uint64_t v);

    /**
     * @brief 获取超时时间
     * @param[in] type 类型SO_RCVTIMEO(读超时), SO_SNDTIMEO(写超时)
     * @return 超时时间毫秒
     */
    uint64_t getTimeout(int type);
private:
    /**
     * @brief 初始化
     */
    bool init();
private:
    /// 是否初始化
    bool m_isInit: 1;
    /// 是否socket
    bool m_isSocket: 1;
    /// 是否hook非阻塞
    bool m_sysNonblock: 1;
    /// 是否用户主动设置非阻塞
    bool m_userNonblock: 1;
    /// 是否关闭
    bool m_isClosed: 1;
    /// 文件句柄
    int m_fd;
    /// 读超时时间毫秒
    uint64_t m_recvTimeout;
    /// 写超时时间毫秒
    uint64_t m_sendTimeout;
};

/**
 * @brief 文件句柄管理类
 */
class FdManager {
public:
    typedef RWMutex RWMutexType;
    /**
     * @brief 无参构造函数
     */
    FdManager();

    /**
     * @brief 获取/创建文件句柄类FdCtx
     * @param[in] fd 文件句柄
     * @param[in] auto_create 是否自动创建
     * @return 返回对应文件句柄类FdCtx::ptr
     */
    FdCtx::ptr get(int fd, bool auto_create = false);

    /**
     * @brief 删除文件句柄类（实际上是重置fd，并不是真的删除）
     * @param[in] fd 文件句柄
     */
    void del(int fd);
private:
    /// 读写锁
    RWMutexType m_mutex;
    /// 文件句柄集合
    std::vector<FdCtx::ptr> m_datas;
};

/// 文件句柄单例
typedef Singleton<FdManager> FdMgr;

}

#endif

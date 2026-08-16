/**
 * @file timer.h
 * @brief 定时器封装
 */
#ifndef __SYLAR_TIMER_H__
#define __SYLAR_TIMER_H__

#include <memory>
#include <vector>
#include <set>
#include <functional>
#include "mutex.h"

namespace sylar {

class TimerManager;
/**
 * @brief 定时器
 */
class Timer : public std::enable_shared_from_this<Timer> {
friend class TimerManager;
public:
    /// 定时器的智能指针类型
    typedef std::shared_ptr<Timer> ptr;

    /**
     * @brief 取消定时器
     */
    bool cancel();

    /**
     * @brief 刷新设置定时器的执行时间（用于定时器的周期性刷新）
     */
    bool refresh();

    /**
     * @brief 重置定时器时间
     * @param[in] ms 定时器执行间隔时间(毫秒)
     * @param[in] from_now 是否从当前时间开始计算
     */
    bool reset(uint64_t ms, bool from_now);
private:
    /**
     * @brief 构造函数
     * @param[in] ms 定时器执行间隔时间
     * @param[in] cb 回调函数
     * @param[in] recurring 是否循环
     * @param[in] manager 定时器管理器
     */
    Timer(uint64_t ms, std::function<void()> cb,
          bool recurring, TimerManager* manager);

    /**
     * @brief 构造函数
     * @param[in] next 执行的时间戳(毫秒)
     */
    Timer(uint64_t next);
private:
    /// 是否循环定时器
    bool m_recurring = false;
    /// 执行周期（周期间隔）
    uint64_t m_ms = 0;
    /// 精确的执行时间（绝对时间点）
    uint64_t m_next = 0;
    /// 回调函数
    std::function<void()> m_cb;
    /// 定时器管理器
    TimerManager* m_manager = nullptr;
private:
    /**
     * @brief 定时器比较仿函数，用于最小堆的构建
     */
    struct Comparator {
        /**
         * @brief 比较定时器的智能指针的大小(比较依据主要是时间)
         * @param[in] lhs 定时器智能指针
         * @param[in] rhs 定时器智能指针
         */
        bool operator()(const Timer::ptr& lhs, const Timer::ptr& rhs) const;
    };
};

/**
 * @brief 定时器管理器
 */
class TimerManager {
friend class Timer;
public:
    /// 读写锁类型
    typedef RWMutex RWMutexType;

    /**
     * @brief 构造函数
     */
    TimerManager();

    /**
     * @brief 析构函数
     */
    virtual ~TimerManager();

    /**
     * @brief 添加定时器
     * @param[in] ms 定时器执行间隔时间
     * @param[in] cb 定时器回调函数
     * @param[in] recurring 是否循环定时器
     */
    Timer::ptr addTimer(uint64_t ms, std::function<void()> cb
                        ,bool recurring = false);

    /**
     * @brief 添加条件定时器
     * @param[in] ms 定时器执行间隔时间
     * @param[in] cb 定时器回调函数
     * @param[in] weak_cond 条件
     * @param[in] recurring 是否循环
     */
    Timer::ptr addConditionTimer(uint64_t ms, std::function<void()> cb
                        ,std::weak_ptr<void> weak_cond
                        ,bool recurring = false);

    /**
     * @brief 到最近一个定时器执行的时间间隔(毫秒)
     */
    uint64_t getNextTimer();

    /**
     * @brief 获取需要执行的定时器的回调函数列表，本质上就是区间查找已经到点的定时器区间
     * @param[out] cbs 回调函数数组
     */
    void listExpiredCb(std::vector<std::function<void()> >& cbs);

    /**
     * @brief 是否有定时器
     */
    bool hasTimer();
protected:

    /**
     * @brief 当有新的定时器插入到定时器的首部,执行该函数（由IOManage重载）
     */
    virtual void onTimerInsertedAtFront() = 0;

    /**
     * @brief 将定时器添加到管理器中
     */
    void addTimer(Timer::ptr val, RWMutexType::WriteLock& lock);
private:
    /**
     * @brief 检测服务器时间是否被调后了（用上次记录的时间点m_previouseTime 与目前时间now_ms做对比）
     * 系统中使用单调时间，所以不会出现时间回退。
     */
    bool detectClockRollover(uint64_t now_ms);
private:
    /// Mutex
    RWMutexType m_mutex;
    /// 定时器集合（规定了排序方式）
    std::set<Timer::ptr, Timer::Comparator> m_timers;
    /// 表示“已经重设最近触发时间”。是一个去重标志，它保证：同一轮 epoll_wait 阻塞期间，无论有多少个新定时器依次插到队首，只需要唤醒一次。
    bool m_tickled = false;
    /// 上次记录的时间点
    uint64_t m_previouseTime = 0;
};

}

#endif

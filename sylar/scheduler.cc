/**
 * @file scheduler.cc
 * @brief 协程调度器实现
 */
#include "scheduler.h"
#include "hook.h"
#include "log.h"
#include "macro.h"
#include "util.h"

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

/// 当前线程的调度器，同一个调度器下的所有线程共享同一个实例
/// 注意，Scheduler::GetThis在没有调度器时返回的是nullptr，并不会自动创建
static thread_local Scheduler *t_scheduler = nullptr;
/// 当前线程的调度协程，每个线程都独有一份
static thread_local Fiber *t_scheduler_fiber = nullptr;

Scheduler::Scheduler(size_t threads, bool use_caller, const std::string &name) {
    SYLAR_ASSERT(threads > 0);
    m_useCaller = use_caller;
    m_name      = name;

    if (use_caller) {
        --threads;//数量-1，是因为其中一个是caller线程，它也会参与协程的调度
        sylar::Fiber::GetThis();
        SYLAR_ASSERT(GetThis() == nullptr);
        t_scheduler = this;

        // 创建调度协程
        m_rootFiber.reset(new Fiber(std::bind(&Scheduler::run, this), 0, false));

        sylar::Thread::SetName(m_name);
        t_scheduler_fiber = m_rootFiber.get();
        m_rootThread      = sylar::GetThreadId();
        m_threadIds.push_back(m_rootThread);
    } else {
        m_rootThread = -1;
    }
    m_threadCount = threads;
}

Scheduler *Scheduler::GetThis() { 
    return t_scheduler; 
}

Fiber *Scheduler::GetMainFiber() { 
    return t_scheduler_fiber;
}

void Scheduler::setThis() {
    t_scheduler = this;
}

Scheduler::~Scheduler() {
    SYLAR_LOG_DEBUG(g_logger) << "Scheduler::~Scheduler()";
    SYLAR_ASSERT(m_stopping);
    if (GetThis() == this) {
        t_scheduler = nullptr;
    }
}

void Scheduler::start() {
    SYLAR_LOG_DEBUG(g_logger) << "start";
    MutexType::Lock lock(m_mutex);
    if (m_stopping) {
        SYLAR_LOG_ERROR(g_logger) << "Scheduler is stopped";
        return;
    }
    SYLAR_ASSERT(m_threads.empty());
    m_threads.resize(m_threadCount);
    for (size_t i = 0; i < m_threadCount; i++) {    //果只使用caller线程进行调度，则m_threadCount=0，那这个方法相当于啥也不做
        m_threads[i].reset(new Thread(std::bind(&Scheduler::run, this),
                                      m_name + "_" + std::to_string(i)));//依次创建调度线程并启动任务调度协程
        m_threadIds.push_back(m_threads[i]->getId());
    }
}

bool Scheduler::stopping() {
    MutexType::Lock lock(m_mutex);
    // 停止表示m_stopping为true，待调度的任务队列为空，目前正在工作的线程数量=0
    return m_stopping && m_tasks.empty() && m_activeThreadCount == 0;
}

void Scheduler::tickle() { 
    SYLAR_LOG_DEBUG(g_logger) << "ticlke"; 
}

void Scheduler::idle() {
    SYLAR_LOG_DEBUG(g_logger) << "idle";
    while (!stopping()) {
        sylar::Fiber::GetThis()->yield();
    }
}

void Scheduler::stop() {
    SYLAR_LOG_DEBUG(g_logger) << "stop";
    if (stopping()) {   //如果满足直接停止的条件，则直接结束即可。
        return;
    }
    m_stopping = true;

    /// 如果use caller，那只能由caller线程发起stop
    if (m_useCaller) {
        SYLAR_ASSERT(GetThis() == this);
    } else {
        SYLAR_ASSERT(GetThis() != this);
    }

    for (size_t i = 0; i < m_threadCount; i++) {
        tickle();
    }

    if (m_rootFiber) {
        tickle();
    }

    /// 在use caller情况下，stop前会调用本caller线程的调度器协程,
    // 并等其结束返回后，本caller线程的主协程（schedule管理协程）才能结束
    if (m_rootFiber) {
        m_rootFiber->resume();  //先切换到调度协程执行
        SYLAR_LOG_DEBUG(g_logger) << "m_rootFiber end";
    }

    // 在unuse caller的情况下，调度协程在其他的若干协程中运行，故先等这些线程都先结束，caller线程的任务添加协程才能结束
    std::vector<Thread::ptr> thrs;
    {
        MutexType::Lock lock(m_mutex);
        thrs.swap(m_threads);
    }
    for (auto &i : thrs) {
        i->join();
    }
}

void Scheduler::run() {
    SYLAR_LOG_DEBUG(g_logger) << "run";
    set_hook_enable(true);  //开启 Sylar 的 hook 机制
    setThis();  //设置当前线程的调度器
    if (sylar::GetThreadId() != m_rootThread) {//如果当前线程不是caller线程
        t_scheduler_fiber = sylar::Fiber::GetThis().get();  //保存当前线程的主协程（也就是调度线程的调度协程，以后要通过它与任务执行协程做切换）
    }

    // 将Scheduler::idle函数--->协程
    Fiber::ptr idle_fiber(new Fiber(std::bind(&Scheduler::idle, this)));
    Fiber::ptr cb_fiber;

    ScheduleTask task;
    while (true) {
        task.reset();
        bool tickle_me = false; // 是否tickle其他线程进行任务调度
        {
            MutexType::Lock lock(m_mutex);
            auto it = m_tasks.begin();
            // 遍历所有调度任务：1、找本线程能做的任务。2、其它线程能做的任务，则tickle_me设置为true
            while (it != m_tasks.end()) {
                if (it->thread != -1 && it->thread != sylar::GetThreadId()) {
                    // 指定了调度线程，但不是在当前线程上调度，标记一下需要通知其他线程进行调度，然后跳过这个任务，继续下一个
                    ++it;
                    tickle_me = true;
                    continue;
                }

                // 找到一个未指定线程，或是指定了当前线程的任务。并且含有"任务执行协程"
                SYLAR_ASSERT(it->fiber || it->cb);

                // [BUG FIX]: hook IO相关的系统调用时，在检测到IO未就绪的情况下，会先添加对应的读写事件，再yield当前协程，等IO就绪后再resume当前协程
                // 多线程高并发情境下，有可能发生刚添加事件就被触发的情况，例如一个协程A由于上述现象被立即放回任务队列中，但当前线程对协程A还未来得及yield，
                // 这样一来，当前线程还在做协程A，任务队列中也存在协程A，就有可能就会出现两个线程同时执行同一个协程的问题！
                // 这里的做法是：如果分配到的任务协程当前状态是Fiber::RUNNING，这就意味着另一个线程还没来的即yield，那当前线程就不做而是跳过它
                // 总而言之就是：协程还没有真正挂起（yield），但是 IO 事件已经触发，导致调度器错误判断这个协程状态。
                if(it->fiber && it->fiber->getState() == Fiber::RUNNING) {
                    ++it;
                    continue;
                }
                
                // 当前调度线程找到一个任务，准备开始调度，将其从任务队列中剔除，活动线程数加1
                task = *it;
                m_tasks.erase(it++);
                ++m_activeThreadCount;
                break;
            }
            // 当前线程拿完一个任务后，发现任务队列还有剩余，那么tickle一下其他线程
            tickle_me |= (it != m_tasks.end());
        }

        if (tickle_me) {    //tickle其他阻塞在epoll_wait的线程
            tickle();
        }

        if (task.fiber) {
            // resume协程，resume返回时，协程要么执行完了，要么半路yield了，总之这个任务就算完成了，活跃线程数减一
            task.fiber->resume();
            --m_activeThreadCount;
            task.reset();
        } else if (task.cb) {
            if (cb_fiber) {//把普通函数-->协程
                cb_fiber->reset(task.cb);
            } else {
                cb_fiber.reset(new Fiber(task.cb));
            }
            task.reset();
            cb_fiber->resume();
            --m_activeThreadCount;
            cb_fiber.reset();
        } else {
            // !进到这个分支情况一定是任务队列空了，调度idle协程即可
            if (idle_fiber->getState() == Fiber::TERM) {
                // 如果调度器没有调度任务，那么idle协程会不停地resume/yield，不会结束，如果idle协程结束了，那一定是调度器停止了
                SYLAR_LOG_DEBUG(g_logger) << "idle fiber term";
                break;
            }
            ++m_idleThreadCount;
            // 切换到idle协程
            // scheduler中的idle其实什么也不做，而是直接会yield，返回到"任务添加协程"
            // 而IOManage中idle重写后，会发生while(true){sleep阻塞}，直至：
            // 1、mian线程通过m_stopping告知任务确实已经结束，工作线程可以退出了
            // 2、任务队列中被添加了新任务，对方通过tickle()，给pip写入一个字节来通知循环可以退出了
            // 3、epoll event就绪了（IO事件或定时器事件）
            idle_fiber->resume();
            --m_idleThreadCount;
        }
    }
    SYLAR_LOG_DEBUG(g_logger) << "Scheduler::run() exit";
}

} // end namespace sylar
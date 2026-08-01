/**
 * @file thread.cc
 * @brief 线程封装实现
 */
#include "thread.h"
#include "log.h"
#include "util.h"

namespace sylar {

static thread_local Thread *t_thread          = nullptr; //当前 pthread 对应哪个 Sylar Thread 对象
static thread_local std::string t_thread_name = "UNKNOW"; //当前 pthread 的 Sylar Thread 对象名字

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

Thread *Thread::GetThis() {
    return t_thread;
}

const std::string &Thread::GetName() {
    return t_thread_name;
}

void Thread::SetName(const std::string &name) {
    if (name.empty()) {
        return;
    }
    if (t_thread) {
        t_thread->m_name = name;
    }
    t_thread_name = name;
}

Thread::Thread(std::function<void()> cb, const std::string &name)
    : m_cb(cb)
    , m_name(name) {
    if (name.empty()) {
        m_name = "UNKNOW";
    }
    // 新建线程的ID、线程属性、线程入口函数、传给入口函数的参数。创建即运行
    int rt = pthread_create(&m_thread, nullptr, &Thread::run, this);
    if (rt) {
        SYLAR_LOG_ERROR(g_logger) << "pthread_create thread fail, rt=" << rt
                                  << " name=" << name;
        throw std::logic_error("pthread_create error");
    }
    m_semaphore.wait();
}

Thread::~Thread() {
    if (m_thread) {
        pthread_detach(m_thread);   //把m_thread对应的pthread线程设置为 detached（分离线程
    }
}

void Thread::join() {
    if (m_thread) {
        int rt = pthread_join(m_thread, nullptr);//等待m_thread表示的pthread线程运行结束
        if (rt) {
            SYLAR_LOG_ERROR(g_logger) << "pthread_join thread fail, rt=" << rt
                                      << " name=" << m_name;
            throw std::logic_error("pthread_join error");
        }
        m_thread = 0;
    }
}

void *Thread::run(void *arg) {
    // 把sylar中的Thread对象与pthread线程绑定
    Thread *thread = (Thread *)arg;
    t_thread       = thread;   
    t_thread_name  = thread->m_name;
    thread->m_id   = sylar::GetThreadId();  //获取 Linux 内核线程的 ID
    // pthread_self():当前正在执行代码的 pthread 线程ID
    pthread_setname_np(pthread_self(), thread->m_name.substr(0, 15).c_str());   //给调用这段代码pthread线程设置名字

    std::function<void()> cb;
    cb.swap(thread->m_cb);

    thread->m_semaphore.notify();   //同步信号量通知（这是为了保证在线程初始化完成前，主线程中的构造函数无法继续执行，避免竞争状态）

    cb();//进入线程回调函数
    return 0;
}

} // namespace sylar

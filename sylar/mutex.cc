/**
 * @file mutex.cc
 * @brief 信号量实现
 */

#include "mutex.h"
#include <stdexcept>

namespace sylar {

Semaphore::Semaphore(uint32_t count) {
    if(sem_init(&m_semaphore, 0, count)) {
        throw std::logic_error("sem_init error");
    }
}

Semaphore::~Semaphore() {
    sem_destroy(&m_semaphore);
}

void Semaphore::wait() {//信号量同步中的P操作
    if(sem_wait(&m_semaphore)) {
        throw std::logic_error("sem_wait error");
    }
}

void Semaphore::notify() {//信号量同步中的V操作
    if(sem_post(&m_semaphore)) {
        throw std::logic_error("sem_post error");
    }
}

} // namespace sylar

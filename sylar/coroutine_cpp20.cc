// Copyright 2023-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

// Copyright 2026 Cruise LLC.

#include "coroutine_cpp20.h"

#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <algorithm>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

namespace co20 {

Scheduler::Scheduler() {
  epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);

  interrupt_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);

  if (epoll_fd_ != -1 && interrupt_fd_ != -1) {
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = interrupt_fd_;
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, interrupt_fd_, &ev);
  }
}

Scheduler::~Scheduler() {
  for (int fd : timerfds_) {
    close(fd);
  }
  timerfds_.clear();

  if (interrupt_fd_ != -1) {
    close(interrupt_fd_);
  }
  if (epoll_fd_ != -1) {
    close(epoll_fd_);
  }
}

void Scheduler::TriggerInterrupt() {
  if (interrupt_fd_ != -1) {
    uint64_t val = 1;
    (void)write(interrupt_fd_, &val, sizeof(val));
  }
}

void Scheduler::ScheduleCoroutine(Coroutine* coroutine) {
  if (!coroutine) return;
  auto s = coroutine->GetState();
  if (s == Coroutine::State::kYielded || s == Coroutine::State::kReady) {
    coroutine->SetState(Coroutine::State::kReady);
    ready_queue_.push_back(coroutine);
    TriggerInterrupt();
  }
}

int Scheduler::PollFd(int fd, uint32_t event_mask) {
  struct pollfd pfd;
  pfd.fd = fd;
  pfd.events = event_mask;
  pfd.revents = 0;

  int ret = poll(&pfd, 1, 0);
  if (ret <= 0) return -1;

  if (event_mask & POLLIN) {
    if (pfd.revents & (POLLIN | POLLERR)) return fd;
  } else {
    if ((pfd.revents & event_mask) || (pfd.revents & POLLERR)) 
        return fd;
  }
  return -1;
}

// 该函数在协程结束时被调用：从所有等待数据结构中移除该协程，并清理该协程相关的一些列文件描述符（如 eventfd、timerfd）。
// 
void Scheduler::CleanupCoroutine(Coroutine* coroutine) {
  auto fd_it = coroutine_fds_.find(coroutine);
  if (fd_it != coroutine_fds_.end()) {
    // 通过“corotuine-->fd”映射表，找到该corotuie正在等待的fd（如eventfd）
    int fd = fd_it->second;
    auto waiting_it = waiting_fds_.find(fd);
    if (waiting_it != waiting_fds_.end()) {
      auto& list = waiting_it->second;
      list.erase(std::remove(list.begin(), list.end(), coroutine), list.end());
      if (list.empty()) {
        // 清除 “fd-->corotuine“映射表中的对应元素
        waiting_fds_.erase(waiting_it);
        if (epoll_fd_ != -1) {
          //注销在epoll监视器中的fd
          epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
        }
      }
    }

    // 如果这个fd有定时器，还要将定时器数组中的元素清除
    if (timerfds_.count(fd) > 0) {
      close(fd);
      timerfds_.erase(fd);
    }

    coroutine_fds_.erase(fd_it);
  }

  // 同样要将该corotuine中的“唤醒铃”fd从各个映射表、epoll监视器中清除
  int ifd = coroutine->GetInterruptFd();
  if (ifd != -1) {
    auto ifd_it = waiting_fds_.find(ifd);
    if (ifd_it != waiting_fds_.end()) {
      auto& ifd_list = ifd_it->second;
      ifd_list.erase(std::remove(ifd_list.begin(), ifd_list.end(), coroutine), ifd_list.end());
      if (ifd_list.empty()) {
        waiting_fds_.erase(ifd_it);
        if (epoll_fd_ != -1) {
          epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, ifd, nullptr);
        }
      }
    }
  }

  // 清理该Coroutine 关联的“超时 timerfd”
  CleanupTimeoutFd(coroutine);
}

void Scheduler::WaitForFd(Coroutine* coroutine, int fd, uint32_t event_mask, uint64_t timeout_ns) {
  if (!coroutine) return;

  // 如果相关的fd事件已经就绪, 则立刻加入到“待调度”协程队列中
  if (PollFd(fd, event_mask) == fd) {
    coroutine->SetWaitResult(fd);
    coroutine->SetState(Coroutine::State::kReady);
    ready_queue_.push_back(coroutine);
    TriggerInterrupt();
    return;
  }

  coroutine->SetState(Coroutine::State::kWaiting);

  // 若这个协程已经在等某个fd了，则删除旧的fd的信息（相同fd则更新）
  auto existing_it = coroutine_fds_.find(coroutine);
  if (existing_it != coroutine_fds_.end()) {
    int old_fd = existing_it->second;
    if (old_fd != fd) {
      auto old_fd_it = waiting_fds_.find(old_fd);
      if (old_fd_it != waiting_fds_.end()) {
        auto& old_list = old_fd_it->second;
        old_list.erase(std::remove(old_list.begin(), old_list.end(), coroutine),
                       old_list.end());
        if (old_list.empty()) {
          waiting_fds_.erase(old_fd_it);
          if (epoll_fd_ != -1) {
            epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, old_fd, nullptr);
          }
        }
      }
      existing_it->second = fd;
    }
    // else: same FD, fall through to add to waiting list
  } else {
    coroutine_fds_[coroutine] = fd;
  }

  //为该fd添加相应的协程等待映射关系 
  [[maybe_unused]] bool fd_already_tracked = waiting_fds_.count(fd) > 0;
  auto& wait_list = waiting_fds_[fd];
  if (std::find(wait_list.begin(), wait_list.end(), coroutine) == wait_list.end()) {
    wait_list.push_back(coroutine);
  }

  if (epoll_fd_ != -1) {
    struct epoll_event ev;
    ev.data.fd = fd;
    ev.events = 0;
    if (event_mask & POLLIN) ev.events |= EPOLLIN | EPOLLRDHUP;
    if (event_mask & POLLOUT) ev.events |= EPOLLOUT;

    if (fd_already_tracked) {
      epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev);
    } else {
      int ret = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev);
      if (ret == -1 && errno == EEXIST) {
        epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev);
      } else if (ret == -1 && errno == EBADF) {
        wait_list.erase(std::remove(wait_list.begin(), wait_list.end(), coroutine), wait_list.end());
        if (wait_list.empty()) waiting_fds_.erase(fd);
        coroutine_fds_.erase(coroutine);
        coroutine->SetWaitResult(-1);
        coroutine->SetState(Coroutine::State::kReady);
        ready_queue_.push_back(coroutine);
        TriggerInterrupt();
        return;
      }
    }
  }

  // 如果存在interrupt fd，则同样添加该fd对应协程映射关系
  int ifd = coroutine->GetInterruptFd();
  if (ifd != -1) {
    [[maybe_unused]] bool ifd_already_tracked = waiting_fds_.count(ifd) > 0;
    auto& ifd_list = waiting_fds_[ifd];
    if (std::find(ifd_list.begin(), ifd_list.end(), coroutine) == ifd_list.end()) {
      ifd_list.push_back(coroutine);
    }
    if (epoll_fd_ != -1) {
      struct epoll_event ev;
      ev.data.fd = ifd;
      ev.events = EPOLLIN;
      if (ifd_already_tracked) {
        epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, ifd, &ev);
      } else {
        int ret = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, ifd, &ev);
        if (ret == -1 && errno == EEXIST) {
          epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, ifd, &ev);
        }
      }
    }
  }

  // 设置超时时间
  if (timeout_ns > 0) {
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (tfd != -1) {
      struct itimerspec its = {};
      its.it_value.tv_sec = timeout_ns / 1000000000ULL;
      its.it_value.tv_nsec = timeout_ns % 1000000000ULL;
      timerfd_settime(tfd, 0, &its, nullptr);

      timerfds_.insert(tfd);
      coroutine->SetTimeoutFd(tfd);

      auto& timer_wait_list = waiting_fds_[tfd];
      timer_wait_list.push_back(coroutine);

      if (epoll_fd_ != -1) {
        struct epoll_event tev;
        tev.data.fd = tfd;
        tev.events = EPOLLIN;
        int ret = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, tfd, &tev);
        if (ret == -1 && errno == EEXIST) {
          epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, tfd, &tev);
        }
      }
    }
  }
}

void Scheduler::SleepFor(Coroutine* coroutine, uint64_t nanoseconds) {
  if (!coroutine || nanoseconds == 0) {//不设置定时器
    ScheduleCoroutine(coroutine);
    return;
  }

  int timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
  if (timer_fd == -1) {//timerfd获取失败
    ScheduleCoroutine(coroutine);
    return;
  }

  timerfds_.insert(timer_fd);

  struct itimerspec its = {};
  its.it_value.tv_sec = nanoseconds / 1000000000ULL;
  its.it_value.tv_nsec = nanoseconds % 1000000000ULL;
  timerfd_settime(timer_fd, 0, &its, nullptr);

  WaitForFd(coroutine, timer_fd, POLLIN, 0);
}

void Scheduler::ResumeCoroutine(Coroutine* coroutine, int value) {
  if (!coroutine || coroutine->handle_.done()) 
    return;

  // 清除“coro-->fd”映射表中信息
  auto it = coroutine_fds_.find(coroutine);
  if (it != coroutine_fds_.end()) {
    int fd = it->second;

    auto fd_it = waiting_fds_.find(fd);
    if (fd_it != waiting_fds_.end()) {
      auto& list = fd_it->second;
      list.erase(std::remove(list.begin(), list.end(), coroutine), list.end());
    }

    // Clean up scheduler-owned timer FDs.
    bool is_timer = fd != interrupt_fd_ && timerfds_.count(fd) > 0;
    if (is_timer) {
      uint64_t val;
      (void)read(fd, &val, sizeof(val));
      close(fd);
      if (epoll_fd_ != -1) {
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
      }
      timerfds_.erase(fd);
    }

    if (fd_it != waiting_fds_.end() && fd_it->second.empty()) {
      waiting_fds_.erase(fd_it);
      if (!is_timer && epoll_fd_ != -1) {
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
      }
    }
    coroutine_fds_.erase(it);
  }

  // 同样，要在“coro-->fd”映射表中清除interrupt fd信息
  int ifd = coroutine->GetInterruptFd();
  if (ifd != -1) {
    auto ifd_it = waiting_fds_.find(ifd);
    if (ifd_it != waiting_fds_.end()) {
      auto& ifd_list = ifd_it->second;
      ifd_list.erase(std::remove(ifd_list.begin(), ifd_list.end(), coroutine),
                     ifd_list.end());
      if (ifd_list.empty()) {
        waiting_fds_.erase(ifd_it);
        if (epoll_fd_ != -1) {
          epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, ifd, nullptr);
        }
      }
    }
  }

  // 清除超时计时器
  CleanupTimeoutFd(coroutine);

  coroutine->SetWaitResult(value);
  coroutine->SetState(Coroutine::State::kReady);
  ready_queue_.push_back(coroutine);
//   通过shcedule的"通知铃"唤醒调度协程
  TriggerInterrupt();
}

void Scheduler::CleanupTimeoutFd(Coroutine* coroutine) {
  int tfd = coroutine->GetTimeoutFd();
  if (tfd == -1) return;

  auto tfd_it = waiting_fds_.find(tfd);
  if (tfd_it != waiting_fds_.end()) {
    auto& list = tfd_it->second;
    list.erase(std::remove(list.begin(), list.end(), coroutine), list.end());
    if (list.empty()) {
      waiting_fds_.erase(tfd_it);
      if (epoll_fd_ != -1) {
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, tfd, nullptr);
      }
    }
  }

  if (timerfds_.count(tfd) > 0) {
    // 消费掉 timerfd 的到期事件
    // uint64_t val;
    // (void)read(tfd, &val, sizeof(val));
    close(tfd);
    timerfds_.erase(tfd);
  }

  
  coroutine->SetTimeoutFd(-1);
}

void Scheduler::DispatchEpollEvents(struct epoll_event* events, int count) {
  for (int i = 0; i < count; i++) {
    int fd = events[i].data.fd;
    if (fd == interrupt_fd_) {
      // 若该就绪事件是由充当Schedule“唤醒铃”的fd导致的，则需要把内容消耗掉，以便下次使用
      uint64_t val;
      (void)read(interrupt_fd_, &val, sizeof(val));
      continue;
    }
    auto it = waiting_fds_.find(fd);
    if (it != waiting_fds_.end()) {
      std::vector<Coroutine*> to_resume = it->second;
      for (Coroutine* c : to_resume) {
        ResumeCoroutine(c, fd);
      }
    }
  }
}

void Scheduler::ProcessReadyCoroutines() {
  while (!ready_queue_.empty()) {
    Coroutine* coroutine = ready_queue_.front();
    ready_queue_.pop_front();

    if (!coroutine) continue;
    if (coroutine->handle_.done()) {
      coroutine->SetState(Coroutine::State::kDead);
      continue;
    }
    if (coroutine->GetState() != Coroutine::State::kReady) continue;

    // 执行所取出的协程
    coroutine->Resume(coroutine->GetWaitResult());
    
    // 处理epoll监视器中就绪的io事件
    if (epoll_fd_ != -1 && !waiting_fds_.empty()) {
      struct epoll_event events[64];
      int n = epoll_wait(epoll_fd_, events, 64, 0);
      if (n > 0) 
        DispatchEpollEvents(events, n);
    }
    
    // 当前取出的协程已经做完，则修改协程状态，并清理
    if (coroutine->handle_.done() || coroutine->GetState() == Coroutine::State::kRunning) {
      coroutine->SetState(Coroutine::State::kDead);
      CleanupCoroutine(coroutine);
    }
  }
}

void Scheduler::ProcessEvents() {
  if (waiting_fds_.empty()) return;

  if (epoll_fd_ != -1) {
    struct epoll_event events[64];
    // 非阻塞方式先尝试获取一次
    int n = epoll_wait(epoll_fd_, events, 64, 0);
    if (n <= 0) {
      if (waiting_fds_.empty()) 
        return;
      // 阻塞方式再获取一次
      n = epoll_wait(epoll_fd_, events, 64, -1);
      if (n <= 0) 
        return;
    }

    DispatchEpollEvents(events, n);
  }
}

void Scheduler::Run() {
  running_ = true;

  while (running_) {
    // 从“待调度”协程中取出协程并执行，并处理epoll中的就绪事件
    ProcessReadyCoroutines();

    if (waiting_fds_.empty() && ready_queue_.empty()) break;
    if (!ready_queue_.empty()) continue;
    if (waiting_fds_.empty()) break;
    // 尝试使用epoll_wait获取就绪的eventfd（感觉有些多余）
    ProcessEvents();
  }

  running_ = false;
}

thread_local Coroutine* self = nullptr; //指向“当前协程是谁”的指针
thread_local Scheduler* scheduler = nullptr;//指向“当前调度器是谁”的指针

} // namespace co20



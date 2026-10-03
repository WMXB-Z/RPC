// Copyright 2023-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

// Copyright 2026 Cruise LLC.

#pragma once

#include <chrono>
#include <coroutine>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <memory>
#include <string>
#include <sys/poll.h>
#include <type_traits>
#include <unistd.h>
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include <vector>

#include <sys/epoll.h>

namespace co20 {

class Coroutine;
class Scheduler;

struct AbortException {};

struct BaseAwaitable;
struct YieldAwaitable;
struct WaitAwaitable;
struct SleepAwaitable;
template <typename T> struct ValueTask;

// Represents a single C++20 stackless coroutine managed by a Scheduler.
class Coroutine {
public:
  enum class State {
    kNew,
    kReady,
    kRunning,
    kYielded,
    kWaiting,
    kDead
  };

  friend class Scheduler;
  friend struct BaseAwaitable;
  friend struct YieldAwaitable;
  friend struct WaitAwaitable;
  friend struct SleepAwaitable;
  friend struct Task;
  template <typename T> friend struct ValueTask;

  template<typename Func>
  Coroutine(Scheduler& scheduler, Func&& func, const std::string& name = "", int interrupt_fd = -1);

  ~Coroutine() {
    if (interrupt_fd_ != -1) {
      close(interrupt_fd_);
    }
    if (handle_) {
      handle_.destroy();
    }
  }

  Coroutine(const Coroutine&) = delete;
  Coroutine& operator=(const Coroutine&) = delete;
  Coroutine(Coroutine&&) = delete;
  Coroutine& operator=(Coroutine&&) = delete;

  YieldAwaitable Yield();
  WaitAwaitable Wait(int fd, uint32_t event_mask = POLLIN, uint64_t timeout_ns = 0);
  SleepAwaitable Sleep(uint64_t nanoseconds);

  template <typename Rep, typename Period>
  SleepAwaitable Sleep(std::chrono::duration<Rep, Period> duration);

  void Abort();

  bool IsAborted() const { return abort_pending_; }
  const std::string& Name() const { return name_; }
  State GetState() const { return state_; }
  Scheduler& GetScheduler() const { return scheduler_; }
  int GetInterruptFd() const { return interrupt_fd_; }

private:
  void SetHandle(std::coroutine_handle<> handle) { handle_ = handle; }
  void SetState(State state) { state_ = state; }
  void SetWaitResult(int result) { wait_result_ = result; }
  int GetWaitResult() const { return wait_result_; }
  void SetTimeoutFd(int fd) { timeout_fd_ = fd; }
  int GetTimeoutFd() const { return timeout_fd_; }

  inline void Resume(int value = 0);

private:
  std::coroutine_handle<> handle_;  //协程句柄
  Scheduler& scheduler_;    //调度器
  State state_ = State::kNew;   //协程状态
  std::string name_;    //协程名
  bool abort_pending_ = false;  //协程等待取消
  int id_;  //协程id
  int wait_result_ = -1;   //用于保存协程这次等待的fd(相当于本协程的“唤醒铃”)
  int interrupt_fd_ = -1;   //用来接收“唤醒/中断”本协程的fd，构造本协程时由外部传入
  int timeout_fd_ = -1; //定时器的fd
  std::shared_ptr<void> callable_storage_;  //协程对应的函数
};

// 协程集合的管理器, 协调并调度协程的运行
class Scheduler {
public:
  //初始化Scheduler：1、创建epoll监视器；2、初始化一个“唤醒铃”interrupt_fd_
  Scheduler();
  ~Scheduler();
  // 进行协程调度
  void Run();
  void Stop() { running_ = false; TriggerInterrupt(); }
  
  //向 Scheduler 中创建并注册一个新的协程，并把它放入“待运行队列”，等待 Scheduler 调度
  template<typename Func>
  Coroutine* Spawn(Func&& func, const std::string& name = "",int interrupt_fd = -1) {
    auto coroutine = std::make_unique<Coroutine>(*this, std::forward<Func>(func), name, interrupt_fd);
    Coroutine* ptr = coroutine.get();
    coroutines_.push_back(std::move(coroutine));
    ptr->SetState(Coroutine::State::kReady);
    ready_queue_.push_back(ptr);
    TriggerInterrupt();
    return ptr;
  }

  // 往待调度队列中放入协程任务
  void ScheduleCoroutine(Coroutine* coroutine);
  // 把一个 Coroutine 从“正在运行”变成“等待某个 fd”，并把这个 fd（eventfd、interrupt_fd、timerfd等）注册到 epoll；
  // 等事件发生后，Scheduler 再通过 ResumeCoroutine() 把它放回 Ready 队列。
  void WaitForFd(Coroutine* coroutine, int fd, uint32_t event_mask, uint64_t timeout_ns);
  // 负责把这个Coroutine注册到epoll中，让它等待fd事件；如果设置了超时时间，还会额外创建timerfd
  void SleepFor(Coroutine* coroutine, uint64_t nanoseconds);
  // 把一个正在等待 I/O、定时器或其他事件的协程“唤醒”，清理它之前的等待状态，然后把它重新放入就绪队列，等待调度执行
  void ResumeCoroutine(Coroutine* coroutine, int value);
  // 不通过epoll_wait立即检查一下某个 fd 现在是否已经具备指定的 I/O 条件（不阻塞）
  int PollFd(int fd, uint32_t event_mask);
  int AllocateId() { return next_id_++; }

private:
  // 对"等待调度"的协程进行调度和执行
  // 不断从 ready_queue_ 中取出“可以运行”的协程，恢复执行；
  // 如果协程执行结束，就清理它；如果协程执行过程中再次进入等待状态，就交给 epoll。
  void ProcessReadyCoroutines();
  // 查询epoll中的就绪事件，并对相应的coroutine做处理
  void ProcessEvents();
  //清理一个corortuine相关的资源
  void CleanupCoroutine(Coroutine* coroutine);
  //清理一个corotuine相关的定时器fd
  void CleanupTimeoutFd(Coroutine* coroutine);
  //通过interrupt_fd_强制唤醒调度协程（Schedule::run()）
  void TriggerInterrupt();
  // 找到已经eventfd已就绪的corotinue，将其重新放回“待调度”队列中等待调度
  void DispatchEpollEvents(struct epoll_event* events, int count);

private:
  bool running_ = false; //调度器是“运行”状态
  std::vector<std::unique_ptr<Coroutine>> coroutines_;  //保存Scheduler创建的所有 Coroutine，并负责它们的生命周期
  std::deque<Coroutine*> ready_queue_;  //保存当前“已经可以运行”的 Coroutine
  absl::flat_hash_map<int, std::vector<Coroutine*>> waiting_fds_;   //某个fd当前有哪些 Coroutine 正在等待它
  absl::flat_hash_map<Coroutine*, int> coroutine_fds_;  //某个 Coroutine 当前正在等待哪个 fd
  absl::flat_hash_set<int> timerfds_;   //记录 Scheduler 创建的 timerfd

  int epoll_fd_ = -1; //epoll实例的fd

  int interrupt_fd_ = -1;   //用于唤醒epoll_wait的fd（Scheduler 的“唤醒铃”）
  int next_id_ = 1; //协程id计数器
};

// --- Awaitable types ---
struct BaseAwaitable {
  Coroutine* coroutine_ = nullptr;
  Scheduler* scheduler_ = nullptr;

  BaseAwaitable(Coroutine* coroutine, Scheduler* scheduler)
    : coroutine_(coroutine), scheduler_(scheduler) {}

  bool await_ready() const noexcept { return false; }

  void await_suspend(std::coroutine_handle<> handle) const {
    if (coroutine_) {
      coroutine_->SetHandle(handle);
    }
  }
};

// 主动暂停当前协程-->将当前协程放回待调度的队列中
struct YieldAwaitable : BaseAwaitable {
  using BaseAwaitable::BaseAwaitable;

  bool await_ready() const noexcept { return false; }

  void await_resume() const {
    if (coroutine_ && coroutine_->IsAborted()) {
      throw AbortException();
    }
  }

  void await_suspend(std::coroutine_handle<> handle) const {
    BaseAwaitable::await_suspend(handle);
    if (coroutine_ && scheduler_) {
      coroutine_->SetState(Coroutine::State::kYielded);
      scheduler_->ScheduleCoroutine(coroutine_);
    }
  }
};

// 因IO等待导致当前协程被暂停--->注册epoll事件--->时间就绪后被唤醒
struct WaitAwaitable : BaseAwaitable {
  int fd_;
  uint32_t event_mask_;
  uint64_t timeout_ns_;
  mutable int result_ = -1;

  WaitAwaitable(Coroutine* coroutine, Scheduler* scheduler, int fd,
                 uint32_t event_mask, uint64_t timeout_ns)
    : BaseAwaitable(coroutine, scheduler), fd_(fd),
      event_mask_(event_mask), timeout_ns_(timeout_ns) {}

  bool await_ready() const noexcept {
    if (coroutine_ && scheduler_) {
      int result = scheduler_->PollFd(fd_, event_mask_);
      if (result == fd_) {
        result_ = fd_;
        return true;
      }
    }
    return false;
  }

  void await_suspend(std::coroutine_handle<> handle) const {
    BaseAwaitable::await_suspend(handle);
    if (coroutine_ && scheduler_) {
      scheduler_->WaitForFd(coroutine_, fd_, event_mask_, timeout_ns_);
    }
  }

  int await_resume() const {
    if (coroutine_ && coroutine_->IsAborted()) {
      throw AbortException();
    }
    if (result_ != -1) {
      return result_;
    }
    if (coroutine_) {
      return coroutine_->GetWaitResult();
    }
    return -1;
  }
};

// 暂停一段时间后被唤醒
struct SleepAwaitable : BaseAwaitable {
  uint64_t timeout_ns_;

  SleepAwaitable(Coroutine* coroutine, Scheduler* scheduler, uint64_t timeout_ns)
    : BaseAwaitable(coroutine, scheduler), timeout_ns_(timeout_ns) {}

  bool await_ready() const noexcept {
    return timeout_ns_ == 0;
  }

  void await_suspend(std::coroutine_handle<> handle) const {
    BaseAwaitable::await_suspend(handle);
    if (coroutine_ && scheduler_) {
      scheduler_->SleepFor(coroutine_, timeout_ns_);
    }
  }

  void await_resume() const {
    if (coroutine_ && coroutine_->IsAborted()) {
      throw AbortException();
    }
  }
};

// --- Coroutine inline implementations (need full Scheduler definition) ---
template<typename Func>
inline Coroutine::Coroutine(Scheduler& scheduler, Func&& func, const std::string& name, int interrupt_fd)
  : handle_(),
    scheduler_(scheduler),
    state_(State::kNew),
    name_(),
    abort_pending_(false),
    id_(scheduler.AllocateId()),
    wait_result_(-1),
    interrupt_fd_(interrupt_fd == -1 ? -1 : dup(interrupt_fd)) {
  name_ = name.empty() ? "co-" + std::to_string(id_) : name;

  // 存储该可调用对象，以确保其捕获内容（例如 lambda 捕获）在协程的整个生命周期内保持有效。
  using FuncType = std::decay_t<Func>;
  auto stored = std::make_shared<FuncType>(std::forward<Func>(func));
  callable_storage_ = stored;

  // 进行协程函数的调用
  auto task = [&]() {
    // 编译器检查：该FuncType是否支持传入Coroutines&参数
    if constexpr (std::is_invocable_v<FuncType, Coroutine&>) {
      return (*stored)(*this);
    } else {
      return (*stored)();
    }
  }();

  // 编译期检查：task 有没有 handle_ 这个成员
  if constexpr (requires { task.handle_; }) {
    handle_ = std::coroutine_handle<>(task.handle_);
    // 编译期检查：task.handle_.promise()有没有 handle_ 这个成员
    if constexpr (requires { task.handle_.promise().coroutine; }) {
      task.handle_.promise().coroutine = this;
    }
    task.handle_ = {};
  }
}

inline void Coroutine::Abort() {
  abort_pending_ = true;
  scheduler_.ResumeCoroutine(this, -1);
}

inline YieldAwaitable Coroutine::Yield() {
  return YieldAwaitable(this, &scheduler_);
}

inline WaitAwaitable Coroutine::Wait(int fd, uint32_t event_mask, uint64_t timeout_ns) {
  return WaitAwaitable(this, &scheduler_, fd, event_mask, timeout_ns);
}

inline SleepAwaitable Coroutine::Sleep(uint64_t nanoseconds) {
  return SleepAwaitable(this, &scheduler_, nanoseconds);
}

template <typename Rep, typename Period>
inline SleepAwaitable Coroutine::Sleep(std::chrono::duration<Rep, Period> duration) {
  auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
  return Sleep(static_cast<uint64_t>(ns));
}

// 指向当前运行的协程及其调度器的线程局部指针。
// 调度器会在每次恢复协程之前自动设置这些指针，从而支持下方的自由函数。
// 每个运行调度器的线程都会拥有自己的一份副本。
// 此处（在 Task/ValueTask 之前）进行声明，以便 ValueTask::await_resume，能够引用 co20::self，从而恢复对协程句柄的追踪
extern thread_local Coroutine* self;
extern thread_local Scheduler* scheduler;

// 协程函数返回的任务类型: [](Coroutine& co) -> Task { ... }
struct Task {
  struct promise_type {
    Coroutine* coroutine = nullptr;

    std::suspend_always initial_suspend() noexcept { return {}; }

    struct FinalSuspendAwaiter {
      bool await_ready() const noexcept { return false; }
      void await_suspend(std::coroutine_handle<promise_type> handle) const noexcept {
        if (handle.promise().coroutine) {
          handle.promise().coroutine->SetState(Coroutine::State::kDead);
        }
      }
      void await_resume() const noexcept {}
    };

    auto final_suspend() noexcept { return FinalSuspendAwaiter{}; }

    void unhandled_exception() {
    }

    Task get_return_object() {
      return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
    }

    void return_void() {}
  };

  std::coroutine_handle<promise_type> handle_;

  Task(std::coroutine_handle<promise_type> h) : handle_(h) {}
  ~Task() { if (handle_) handle_.destroy(); }

  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;
  Task(Task&& other) noexcept : handle_(other.handle_) { other.handle_ = {}; }
};

// 一种生成 T 类型值的子协程。与 Task（由调度器管理）不同，ValueTask<T> 专为在 Task 或另一个 ValueTask 内部进行 co_await 而设计。
// 它利用对称转移（symmetric transfer）机制与调用者在同一上下文中内联运行，并共享相同的调度器与协程上下文。
//
// Usage:
//   ValueTask<int> ComputeAsync(Coroutine& co) {
//     co_await co.Wait(fd, POLLIN);
//     co_return 42;
//   }
//
//   // From within a Task:
//   int result = co_await ComputeAsync(co);
template <typename T>
struct ValueTask {
  struct promise_type {
    T value_{};
    std::coroutine_handle<> continuation_{};

    std::suspend_always initial_suspend() noexcept { return {}; }

    struct FinalAwaiter {
      bool await_ready() const noexcept { return false; }
      std::coroutine_handle<> await_suspend(
          std::coroutine_handle<promise_type> h) const noexcept {
        if (auto cont = h.promise().continuation_; cont)
          return cont;
        return std::noop_coroutine();
      }
      void await_resume() const noexcept {}
    };

    FinalAwaiter final_suspend() noexcept { return {}; }
    void unhandled_exception() {}

    ValueTask get_return_object() {
      return ValueTask{std::coroutine_handle<promise_type>::from_promise(*this)};
    }

    template <typename U>
    void return_value(U&& v) { value_ = std::forward<U>(v); }
  };

  std::coroutine_handle<promise_type> handle_;

  explicit ValueTask(std::coroutine_handle<promise_type> h) : handle_(h) {}
  ~ValueTask() { if (handle_) handle_.destroy(); }

  ValueTask(const ValueTask&) = delete;
  ValueTask& operator=(const ValueTask&) = delete;
  ValueTask(ValueTask&& o) noexcept : handle_(o.handle_) { o.handle_ = {}; }
  ValueTask& operator=(ValueTask&& o) noexcept {
    if (this != &o) {
      if (handle_) handle_.destroy();
      handle_ = o.handle_;
      o.handle_ = {};
    }
    return *this;
  }

  // Awaitable interface for co_await.
  bool await_ready() const noexcept { return false; }

  std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) const noexcept {
    handle_.promise().continuation_ = caller;
    return handle_;
  }

  T await_resume() {
    // Restore the Coroutine's handle to point to the caller (continuation)
    // before this ValueTask is destroyed, preventing a dangling handle.
    if (co20::self && handle_.promise().continuation_) {
      co20::self->SetHandle(handle_.promise().continuation_);
    }
    return std::move(handle_.promise().value_);
  }
};

// Void specialization of ValueTask for sub-coroutines that don't return a
// value but still need to co_await internally.
template <>
struct ValueTask<void> {
  struct promise_type {
    std::coroutine_handle<> continuation_{};

    std::suspend_always initial_suspend() noexcept { return {}; }

    struct FinalAwaiter {
      bool await_ready() const noexcept { return false; }
      std::coroutine_handle<> await_suspend(
          std::coroutine_handle<promise_type> h) const noexcept {
        if (auto cont = h.promise().continuation_; cont)
          return cont;
        return std::noop_coroutine();
      }
      void await_resume() const noexcept {}
    };

    FinalAwaiter final_suspend() noexcept { return {}; }
    void unhandled_exception() {}

    ValueTask get_return_object() {
      return ValueTask{std::coroutine_handle<promise_type>::from_promise(*this)};
    }

    void return_void() {}
  };

  std::coroutine_handle<promise_type> handle_;

  explicit ValueTask(std::coroutine_handle<promise_type> h) : handle_(h) {}
  ~ValueTask() { if (handle_) handle_.destroy(); }

  ValueTask(const ValueTask&) = delete;
  ValueTask& operator=(const ValueTask&) = delete;
  ValueTask(ValueTask&& o) noexcept : handle_(o.handle_) { o.handle_ = {}; }
  ValueTask& operator=(ValueTask&& o) noexcept {
    if (this != &o) {
      if (handle_) handle_.destroy();
      handle_ = o.handle_;
      o.handle_ = {};
    }
    return *this;
  }

  bool await_ready() const noexcept { return false; }

  std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) const noexcept {
    handle_.promise().continuation_ = caller;
    return handle_;
  }

  void await_resume() const {
    if (co20::self && handle_.promise().continuation_) {
      co20::self->SetHandle(handle_.promise().continuation_);
    }
  }
};

// --- Coroutine::Resume (needs thread_local declarations above) ---

inline void Coroutine::Resume(int value) {
  // 判断：协程句柄存在性、协程是否已经结束
  if (handle_ && !handle_.done()) {
    wait_result_ = value;
    state_ = State::kRunning;
    co20::self = this;
    co20::scheduler = &scheduler_;
    handle_.resume();
    if (handle_.done()) {
      state_ = State::kDead;
    }
  }
}

// ----用于非侵入式协程访问的自由函数 ---
// 这些函数返回可等待对象（awaitable），必须配合 co_await 使用，例如
//   co_await co20::Yield();
//   int fd = co_await co20::Wait(my_fd, POLLIN);
//   co_await co20::Sleep(std::chrono::milliseconds(100));

inline YieldAwaitable Yield() { return self->Yield(); }

inline WaitAwaitable Wait(int fd, uint32_t event_mask = POLLIN,
                          uint64_t timeout_ns = 0) {
  return self->Wait(fd, event_mask, timeout_ns);
}

inline SleepAwaitable Sleep(uint64_t nanoseconds) {
  return self->Sleep(nanoseconds);
}

template <typename Rep, typename Period>
inline SleepAwaitable Sleep(std::chrono::duration<Rep, Period> duration) {
  return self->Sleep(duration);
}

inline SleepAwaitable Nanosleep(uint64_t ns) {
  return self->Sleep(ns);
}

inline SleepAwaitable Millisleep(uint64_t ms) {
  return self->Sleep(ms * 1000000ULL);
}

} // namespace co20


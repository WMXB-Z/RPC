#ifndef RPC_CLOSURE_H
#define RPC_CLOSURE_H

#include <google/protobuf/stubs/callback.h>
#include <functional>
#include <memory>
#include <utility>

namespace sylar{
namespace tinyrpc {

class TinyPbRpcClosure : public google::protobuf::Closure {
public:
    typedef std::shared_ptr<TinyPbRpcClosure> ptr;
    explicit TinyPbRpcClosure(std::function<void()> cb) : m_cb(std::move(cb)) {}

    ~TinyPbRpcClosure() override = default;

    void Run() override {
        if (m_cb) {
            m_cb();
        }
    }

private:
    std::function<void()> m_cb{nullptr};
};

}  // namespace tinyrpc
}
#endif
/**
 * @file test_http_sticky.cc
 * @brief HTTP 粘包测试
 * @details 一次写入多条完整的 HTTP 消息（请求/响应），验证：
 *          1. HttpSession::recvRequest 能逐条切分出请求（服务端方向）
 *          2. HttpConnection::recvResponse 能逐条切分出响应（客户端方向）
 *          测试不依赖网络和调度器，通过 socketpair + 一次性 write 模拟粘包。
 */
#include "sylar/sylar.h" 

#include <sys/socket.h>
#include <unistd.h>
#include <cstring>

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

/**
 * @brief 构造一个带可选 body 的 HTTP 请求
 */
static std::string makeRequest(const std::string &path, const std::string &body = "") {
    std::stringstream ss;
    ss << "POST " << path << " HTTP/1.1\r\n"
       << "Host: 127.0.0.1\r\n"
       << "Connection: keep-alive\r\n";
    if (!body.empty()) {
        ss << "Content-Length: " << body.size() << "\r\n";
    }
    ss << "\r\n" << body;
    return ss.str();
}

/**
 * @brief 构造一个带 body 的 HTTP 响应
 */
static std::string makeResponse(const std::string &body) {
    sylar::http::HttpResponse::ptr rsp(new sylar::http::HttpResponse(0x11, false));
    rsp->setHeader("Server", "sticky-test");
    rsp->setBody(body);
    std::stringstream ss;
    ss << *rsp;
    return ss.str();
}

/**
 * @brief 把 protected 的 Socket::init(int) 提升为 public，用于包装已有 fd
 */
class TestSocket : public sylar::Socket {
public:
    TestSocket(int fd) : sylar::Socket(AF_UNIX, SOCK_STREAM) {
        // socketpair 创建的 fd 未经过 hook，先注册到 FdManager，Socket::init 才能识别
        sylar::FdMgr::GetInstance()->get(fd, true);
        SYLAR_ASSERT(init(fd));
    }
};

int main(int argc, char **argv) {
    sylar::EnvMgr::GetInstance()->init(argc, argv);
    g_logger->setLevel(sylar::LogLevel::ERROR);

    int failed = 0;

    // ========== 1. 服务端方向：HttpSession::recvRequest 粘包 ==========
    {
        int fds[2];
        SYLAR_ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
        sylar::Socket::ptr sock(new TestSocket(fds[0]));
        sylar::http::HttpSession::ptr session(new sylar::http::HttpSession(sock));

        // 模拟客户端一次性写入 3 条请求（其中第 2 条带 body），构成粘包
        std::string payload = makeRequest("/sticky/a")
                            + makeRequest("/sticky/b", "hello")
                            + makeRequest("/sticky/c");
        ssize_t wn = ::write(fds[1], payload.data(), payload.size());
        SYLAR_ASSERT(wn == (ssize_t)payload.size());

        // 服务端连续收取 3 条请求
        std::vector<std::string> paths;
        std::vector<std::string> bodies;
        for (int i = 0; i < 3; ++i) {
            sylar::http::HttpRequest::ptr req = session->recvRequest();
            if (!req) {
                SYLAR_LOG_ERROR(g_logger) << "recvRequest #" << i << " fail";
                ++failed;
                break;
            }
            paths.push_back(req->getPath());
            bodies.push_back(req->getBody());
        }

        if (paths.size() != 3
            || paths[0] != "/sticky/a"
            || paths[1] != "/sticky/b"
            || paths[2] != "/sticky/c") {
            SYLAR_LOG_ERROR(g_logger) << "recvRequest paths mismatch";
            ++failed;
        }
        if (bodies.size() == 3 && bodies[1] != "hello") {
            SYLAR_LOG_ERROR(g_logger) << "recvRequest body mismatch";
            ++failed;
        }

        ::close(fds[1]);
    }

    // ========== 2. 客户端方向：HttpConnection::recvResponse 粘包 ==========
    {
        int fds[2];
        SYLAR_ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
        sylar::Socket::ptr sock(new TestSocket(fds[0]));
        sylar::http::HttpConnection::ptr conn(new sylar::http::HttpConnection(sock));

        // 模拟服务端一次性写入 3 条响应，构成粘包
        std::string payload = makeResponse("resp-1")
                            + makeResponse("resp-2")
                            + makeResponse("resp-3");
        ssize_t wn = ::write(fds[1], payload.data(), payload.size());
        SYLAR_ASSERT(wn == (ssize_t)payload.size());

        // 客户端连续收取 3 条响应
        std::vector<std::string> bodies;
        for (int i = 0; i < 3; ++i) {
            sylar::http::HttpResponse::ptr rsp = conn->recvResponse();
            if (!rsp) {
                SYLAR_LOG_ERROR(g_logger) << "recvResponse #" << i << " fail";
                ++failed;
                break;
            }
            bodies.push_back(rsp->getBody());
        }

        if (bodies.size() != 3
            || bodies[0] != "resp-1"
            || bodies[1] != "resp-2"
            || bodies[2] != "resp-3") {
            SYLAR_LOG_ERROR(g_logger) << "recvResponse bodies mismatch";
            ++failed;
        }

        ::close(fds[1]);
    }

    if (failed == 0) {
        std::cout << "test_http_sticky PASS" << std::endl;
        return 0;
    }
    std::cout << "test_http_sticky FAIL, failed=" << failed << std::endl;
    return 1;
}

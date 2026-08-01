#include "http_session.h"
#include "http_parser.h"

namespace sylar {
namespace http {

HttpSession::HttpSession(Socket::ptr sock, bool owner)
    : SocketStream(sock, owner) {
    m_buff_size = HttpRequestParser::GetHttpRequestBufferSize();
    m_buffer.reset(new char[m_buff_size], [](char *ptr) {
        delete[] ptr;
    });
    m_offset = 0;
}

HttpRequest::ptr HttpSession::recvRequest() {
    HttpRequestParser::ptr parser(new HttpRequestParser);
    do {
        // 先解析缓冲区中已有的数据（上一次粘包剩余的字节）
        size_t nparse = parser->execute(m_buffer.get(), m_offset);
        if (parser->hasError()) {
            close();
            return nullptr;
        }
        m_offset -= (int)nparse;
        if (parser->isFinished()) {
            break;
        }
        if (m_offset == (int)m_buff_size) {
            close();
            return nullptr;
        }
        // 数据不足一条完整消息，继续从 socket 读取
        int len = read(m_buffer.get() + m_offset, m_buff_size - m_offset);
        if (len <= 0) {
            close();
            return nullptr;
        }
        m_offset += len;
    } while (true);

    parser->getData()->init();
    return parser->getData();
}

int HttpSession::sendResponse(HttpResponse::ptr rsp) {
    std::stringstream ss;
    ss << *rsp;
    std::string data = ss.str();
    return writeFixSize(data.c_str(), data.size());
}

} // namespace http
} // namespace sylar

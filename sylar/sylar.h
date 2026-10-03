/**
 * @section 日志模块 日志模块
 * 支持流式日志风格写日志和格式化风格写日志，支持日志格式自定义，日志级别，多日志分离等等功能 流式日志使用：SYLAR_LOG_INFO(g_logger) << "this is a log"; 格式化日志使用：SYLAR_LOG_FMT_INFO(g_logger, "%s", "this is a log"); 支持时间,线程id,线程名称,日志级别,日志名称,文件名,行号等内容的自由配置
 * 
 */

#ifndef __SYLAR_SYLAR_H__
#define __SYLAR_SYLAR_H__

#include "log.h"
#include "util.h"
#include "singleton.h"
#include "mutex.h"
#include "noncopyable.h"
#include "macro.h"
#include "env.h"
#include "config.h"
#include "thread.h"
#include "fiber.h"
#include "scheduler.h"
#include "iomanager.h"
#include "fd_manager.h"
#include "hook.h"
#include "endian.h"
#include "address.h"
#include "socket.h"
#include "bytearray.h"
#include "tcp_server.h"
#include "uri.h"
#include "http/http.h"
#include "http/http_parser.h"
#include "http/http_session.h"
#include "http/servlet.h"
#include "http/http_server.h"
#include "http/http_connection.h"
#include "daemon.h"
#endif
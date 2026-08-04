#include "sylar/sylar.h"

#include <unistd.h>

sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT(); // 默认INFO级别
// 总共有五个等级：DEBUG->INFO->WARN->ERROR->FATAL，Log等级及以上的才会打印
int main(int argc, char *argv[]) {
    sylar::EnvMgr::GetInstance()->init(argc, argv);
    sylar::Config::LoadFromConfDir(sylar::EnvMgr::GetInstance()->getConfigPath());

    SYLAR_LOG_FATAL(g_logger) << "fatal msg";
    SYLAR_LOG_ERROR(g_logger) << "err msg";
    SYLAR_LOG_INFO(g_logger) << "info msg";
    SYLAR_LOG_DEBUG(g_logger) << "debug msg";

    SYLAR_LOG_FMT_FATAL(g_logger, "fatal %s:%d", __FILE__, __LINE__);
    SYLAR_LOG_FMT_ERROR(g_logger, "err %s:%d", __FILE__, __LINE__);
    SYLAR_LOG_FMT_INFO(g_logger, "info %s:%d", __FILE__, __LINE__);
    SYLAR_LOG_FMT_DEBUG(g_logger, "debug %s:%d", __FILE__, __LINE__);
   
    sleep(1);
    sylar::SetThreadName("brand_new_thread");   //设置当前线程名称。

    g_logger->setLevel(sylar::LogLevel::WARN);  //改变Log等级为WARN
    SYLAR_LOG_FATAL(g_logger) << "fatal msg";
    SYLAR_LOG_ERROR(g_logger) << "err msg";
    SYLAR_LOG_INFO(g_logger) << "info msg"; // 不打印
    SYLAR_LOG_DEBUG(g_logger) << "debug msg"; // 不打印


    // 指定输出到文件，可将log的输出至指定文件中
    sylar::FileLogAppender::ptr fileAppender(new sylar::FileLogAppender("./log.txt"));
    g_logger->addAppender(fileAppender);
    SYLAR_LOG_FATAL(g_logger) << "fatal msg";
    SYLAR_LOG_ERROR(g_logger) << "err msg";
    SYLAR_LOG_INFO(g_logger) << "info msg"; // 不打印
    SYLAR_LOG_DEBUG(g_logger) << "debug msg"; // 不打印

    sylar::Logger::ptr test_logger = SYLAR_LOG_NAME("test_logger");//通过名称获取指定 Logger（不存在则新建一个）
    sylar::StdoutLogAppender::ptr appender(new sylar::StdoutLogAppender);//指定输出至控制台
    // 设置日志输出格式
    sylar::LogFormatter::ptr formatter(new sylar::LogFormatter("%d:%rms%T%p%T%c%T%f:%l %m%n")); // 时间：启动毫秒数 级别 日志名称 文件名：行号 消息 换行
    appender->setFormatter(formatter);  //改变日志输出格式
    test_logger->addAppender(appender);
    test_logger->setLevel(sylar::LogLevel::WARN);

    SYLAR_LOG_ERROR(test_logger) << "err msg";
    SYLAR_LOG_INFO(test_logger) << "info msg"; // 不打印

    // 输出全部日志器的配置
    g_logger->setLevel(sylar::LogLevel::INFO);
    SYLAR_LOG_INFO(g_logger) << "logger config:" << sylar::LoggerMgr::GetInstance()->toYamlString();

    return 0;
}
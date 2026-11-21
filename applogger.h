#ifndef APPLOGGER_H
#define APPLOGGER_H

#include <QString>

/**
 * 应用日志。
 *
 * - 按天写入 `<程序目录>/logs/PlateletAggregation_yyyy-MM-dd.log`；
 * - 程序目录不可写时（使用默认目录安装）回退到
 *   `AppDataLocation/logs`；
 * - 启动和跨天时自动删除超过保留天数的旧日志（默认保留 15 天，含当天）；
 * - install() 接管 qDebug / qInfo / qWarning / qCritical / qFatal，
 *   同时保留 Qt 原有输出。
 *
 * 线程安全：可在任意线程调用 qDebug 等宏。
 */
namespace AppLog {

constexpr int kDefaultRetentionDays = 15;

/** 默认日志目录：<程序目录>/logs */
QString defaultLogDirectory();

/**
 * 安装日志处理器。
 * @param directory     日志目录；为空时使用 defaultLogDirectory()
 * @param retentionDays 保留天数（含当天）；<= 0 表示不清理旧日志
 * @return 是否成功打开日志文件并安装处理器
 */
bool install(const QString &directory = QString(),
             int retentionDays = kDefaultRetentionDays);

/** 恢复 Qt 默认日志处理器并关闭当前日志文件 */
void uninstall();

/** 当前实际使用的日志目录（未安装时为空） */
QString logDirectory();

/** 当前正在写入的日志文件路径（未安装时为空） */
QString currentLogFilePath();

} // namespace AppLog

#endif // APPLOGGER_H

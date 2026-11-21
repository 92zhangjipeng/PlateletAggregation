#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "applogger.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QStringList>
#include <QtGlobal>

namespace {

const char kLogPrefix[] = "PlateletAggregation_";
const char kLogSuffix[] = ".log";

QMutex g_mutex;
QFile g_file;
QString g_directory;
QDate g_fileDate;
int g_retentionDays = AppLog::kDefaultRetentionDays;
QtMessageHandler g_previousHandler = nullptr;
bool g_installed = false;

QString logFileName(const QDate &date)
{
    return QString::fromLatin1(kLogPrefix)
            + date.toString(QStringLiteral("yyyy-MM-dd"))
            + QString::fromLatin1(kLogSuffix);
}

const char *levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:    return "DEBUG";
    case QtInfoMsg:     return "INFO";
    case QtWarningMsg:  return "WARN";
    case QtCriticalMsg: return "ERROR";
    case QtFatalMsg:    return "FATAL";
    }
    return "LOG";
}

/** 删除保留期之外的日志；调用方须持有 g_mutex */
void purgeOldLogsLocked(const QDate &today)
{
    if (g_retentionDays <= 0 || g_directory.isEmpty()) {
        return;
    }

    // 保留今天 + 前 (retentionDays-1) 天，共 retentionDays 个日历日。
    const QDate oldest = today.addDays(-(g_retentionDays - 1));
    QDir dir(g_directory);
    const QString pattern = QString::fromLatin1(kLogPrefix)
            + QLatin1Char('*') + QString::fromLatin1(kLogSuffix);
    const QStringList names = dir.entryList(
                QStringList() << pattern, QDir::Files);
    const int dateOffset = int(sizeof(kLogPrefix) - 1);
    for (const QString &name : names) {
        const QDate fileDate = QDate::fromString(
                    name.mid(dateOffset, 10),
                    QStringLiteral("yyyy-MM-dd"));
        if (fileDate.isValid() && fileDate < oldest) {
            dir.remove(name);
        }
    }
}

/** 打开指定日期日志；调用方须持有 g_mutex */
bool openLogFileLocked(const QString &directory, const QDate &date)
{
    if (directory.isEmpty() || !QDir().mkpath(directory)) {
        return false;
    }

    if (g_file.isOpen()) {
        g_file.flush();
        g_file.close();
    }

    g_directory = directory;
    g_fileDate = date;
    g_file.setFileName(QDir(directory).filePath(logFileName(date)));
    if (!g_file.open(QIODevice::WriteOnly | QIODevice::Append
                     | QIODevice::Text)) {
        return false;
    }

    purgeOldLogsLocked(date);
    return true;
}

void messageHandler(QtMsgType type, const QMessageLogContext &context,
                    const QString &message)
{
    QtMessageHandler previous = nullptr;
    {
        QMutexLocker locker(&g_mutex);
        previous = g_previousHandler;

        const QDateTime now = QDateTime::currentDateTime();
        const QDate today = now.date();
        if (!g_file.isOpen() || g_fileDate != today) {
            openLogFileLocked(g_directory, today);
        }

        if (g_file.isOpen()) {
            QString line = now.toString(
                        QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
            line += QStringLiteral(" [%1] ")
                    .arg(QString::fromLatin1(levelName(type)));
            line += message;
            if (context.file && *context.file) {
                line += QStringLiteral(" (%1:%2)")
                        .arg(QString::fromUtf8(context.file))
                        .arg(context.line);
            }
            line += QLatin1Char('\n');
            g_file.write(line.toUtf8());
            g_file.flush();
        }
    }

    if (previous && previous != messageHandler) {
        previous(type, context, message);
    }
}

} // namespace

QString AppLog::defaultLogDirectory()
{
    return QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("logs"));
}

bool AppLog::install(const QString &directory, int retentionDays)
{
    QMutexLocker locker(&g_mutex);
    if (g_installed) {
        return true;
    }

    g_retentionDays = retentionDays;
    const QDate today = QDate::currentDate();
    const QString requested = directory.isEmpty()
            ? defaultLogDirectory() : directory;

    bool opened = openLogFileLocked(requested, today);
    if (!opened && directory.isEmpty()) {
        // 程序目录不可写时回退到 AppDataLocation/logs。
        const QString fallbackBase = QStandardPaths::writableLocation(
                    QStandardPaths::AppDataLocation);
        if (!fallbackBase.isEmpty()) {
            opened = openLogFileLocked(
                        QDir(fallbackBase).filePath(QStringLiteral("logs")),
                        today);
        }
    }
    if (!opened) {
        g_directory.clear();
        g_fileDate = QDate();
        return false;
    }

    g_previousHandler = qInstallMessageHandler(messageHandler);
    g_installed = true;
    return true;
}

void AppLog::uninstall()
{
    QtMessageHandler previous = nullptr;
    {
        QMutexLocker locker(&g_mutex);
        if (!g_installed) {
            return;
        }
        previous = g_previousHandler;
        g_previousHandler = nullptr;
        g_installed = false;
    }

    qInstallMessageHandler(previous);

    QMutexLocker locker(&g_mutex);
    if (g_file.isOpen()) {
        g_file.flush();
        g_file.close();
    }
    g_directory.clear();
    g_fileDate = QDate();
}

QString AppLog::logDirectory()
{
    QMutexLocker locker(&g_mutex);
    return g_directory;
}

QString AppLog::currentLogFilePath()
{
    QMutexLocker locker(&g_mutex);
    return g_file.isOpen() ? g_file.fileName() : QString();
}

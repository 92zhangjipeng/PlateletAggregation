#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "mainwindow.h"
#include "applogger.h"
#include "AppIcon.h"
#include "MachineModel.h"
#include "ModuleAdPoller.h"
#include "mainboardstatus.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QFont>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("PlateletAggregation"));
    app.setOrganizationName(QStringLiteral("PlateletAggregation"));
    app.setWindowIcon(appui::appIcon());

    qRegisterMetaType<MachineModel>("MachineModel");
    qRegisterMetaType<ModuleAdPoller::ModuleAdData>("ModuleAdPoller::ModuleAdData");
    qRegisterMetaType<MainBoardStatus>("MainBoardStatus");

    QFont appFont(QStringLiteral("Microsoft YaHei"));
    appFont.setPointSize(10);
    app.setFont(appFont);

    if (AppLog::install()) {
        qInfo() << "========== PlateletAggregation 启动 ==========";
        qInfo() << "程序目录:" << QCoreApplication::applicationDirPath();
        qInfo() << "日志文件:" << AppLog::currentLogFilePath();
    } else {
        qWarning() << "日志系统初始化失败，日志不会写入文件";
    }

    MainWindow window;
    window.show();

    const int exitCode = app.exec();
    qInfo() << "========== PlateletAggregation 退出，code ="
            << exitCode << "==========";
    AppLog::uninstall();
    return exitCode;
}

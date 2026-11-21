#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "mainwindow.h"
#include "AppIcon.h"
#include "MachineModel.h"
#include "ModuleAdPoller.h"
#include "mainboardstatus.h"

#include <QApplication>
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

    MainWindow window;
    window.show();

    return app.exec();
}

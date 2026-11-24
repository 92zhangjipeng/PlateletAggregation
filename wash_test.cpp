#include <QCoreApplication>
#include <QSettings>
#include <QString>
#include <cstdio>
#include "instrumentparameters.h"

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("PlateletAggregation"));
    app.setApplicationName(QStringLiteral("PlateletAggregation"));
    QSettings s;
    const QString rawSample = s.value("pumps/sampleWashUl").toString();
    const QString rawReagent = s.value("pumps/reagentWashUl").toString();
    std::printf("raw sample '%s' raw reagent '%s'\n",
                rawSample.toUtf8().constData(),
                rawReagent.toUtf8().constData());
    std::printf("getter sample %d getter reagent %d\n",
                sampleWashVolumeUl(), reagentWashVolumeUl());
    std::fflush(stdout);
    return 0;
}

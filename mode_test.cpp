#include <QCoreApplication>
#include <QSettings>
#include <QString>
#include <cstdio>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("PlateletAggregation"));
    app.setApplicationName(QStringLiteral("PlateletAggregation"));
    QSettings s;
    std::printf("testMode='%s'\n", s.value("instrument/testMode").toString().toUtf8().constData());
    std::printf("testQueue? keys:\n");
    for (const QString &k : s.allKeys()) std::printf("  %s\n", k.toUtf8().constData());
    return 0;
}

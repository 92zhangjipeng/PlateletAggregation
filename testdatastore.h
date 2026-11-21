#ifndef TESTDATASTORE_H
#define TESTDATASTORE_H

#include <QDateTime>
#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

/**
 * 一个试剂（或 PPP）的测试数据。
 *
 * prpPoints[0] 对应 PRP1（第 1 秒），prpPoints[299] 对应 PRP300；
 * PRP0 单独放在 prp0 中。aggregationPoints 可选，与 prpPoints 一一对应。
 */
struct TestCurveData
{
    QString reagent;                  // PPP / AA / ADP / COL / EPI / RIS
    int prp0 = -1;
    QVector<int> prpPoints;           // PRP1 ~ PRP300，最多 300 个点
    QVector<double> aggregationPoints; // 可选，集聚百分比
    int moduleNumber = 0;
    int localChannel = 0;
    int globalChannel = 0;
    QString status = QStringLiteral("完成");
};

struct SampleTestData
{
    QString sampleNumber;
    QString patientName;
    QString patientId;
    QString gender;
    int age = -1;
    QString sampleType;               // 全血 / 血浆
    QString machineModel;
    QString operatorName;
    QString remark;
    QDateTime testTime;

    // 1 个 PPP + 最多 5 个试剂，共最多 6 条
    QVector<TestCurveData> tests;
};

struct SampleSummary
{
    qint64 id = 0;
    QString sampleNumber;
    QString patientName;
    QString sampleType;
    QString machineModel;
    QDateTime testTime;
    int testCount = 0;
};

/**
 * 测试数据 SQLite 存储。
 *
 * 表结构：
 *   samples      每个样本一行（样本号、患者信息、样本类型、机型、时间等）
 *   sample_tests 每个样本的 PPP/试剂测试各一行，保存 PRP0
 *   test_points  每个测试的 PRP1~PRP300 各一行（second, prp_value）
 *
 * 这样避免一张表出现 6 × 301 个列，便于查询、导出和后续增加试剂。
 */
class TestDataStore : public QObject
{
    Q_OBJECT

public:
    explicit TestDataStore(const QString &databasePath = QString(),
                           QObject *parent = nullptr);
    ~TestDataStore() override;

    bool open(QString *error = nullptr);
    void close();
    bool isOpen() const;
    QString databasePath() const;

    /** 保存一个样本及其 PPP/最多 5 个试剂数据；成功返回样本 id，失败返回 -1 */
    qint64 saveSample(const SampleTestData &sample, QString *error = nullptr);
    bool loadSample(qint64 sampleId, SampleTestData *sample,
                    QString *error = nullptr) const;
    QVector<SampleSummary> recentSamples(int limit = 200,
                                         QString *error = nullptr) const;

    static QString defaultDatabasePath();

private:
    bool ensureSchema(QString *error);
    static bool validateSample(const SampleTestData &sample, QString *error);

    QString m_connectionName;
    QString m_databasePath;
    QSqlDatabase m_db;
    bool m_open = false;
};

#endif // TESTDATASTORE_H

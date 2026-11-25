#ifndef TESTDATASTORE_H
#define TESTDATASTORE_H

#include <QDate>
#include <QDateTime>
#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

/**
 * 一个试剂的测试数据。
 *
 * prpPoints[0] 对应 PRP1（第 1 秒），prpPoints[299] 对应 PRP300；
 * PRP0 单独放在 prp0 中。aggregationPoints 可选，与 prpPoints 一一对应。
 * PPP 不是试剂，而是样本级的基准值，放在 SampleTestData::pppValue。
 */
struct TestCurveData
{
    QString reagent;                  // AA / ADP / COL / EPI / RIS
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

    // PPP 基准 AD：样本级单独值（与 PRP0 地位相同，用于计算集聚率），
    // 不占用 sample_tests 的试剂行。
    int pppValue = -1;

    // 最多 5 个试剂测试（AA/ADP/COL/EPI/RIS）
    QVector<TestCurveData> tests;
};

struct SampleSummary
{
    qint64 id = 0;
    QString sampleNumber;
    QString patientName;
    int age = -1;                     // 年龄，-1 表示未录入
    QString sampleType;
    QString machineModel;
    QDateTime testTime;
    int pppValue = -1;                // 样本级 PPP 基准 AD
    int testCount = 0;                // 试剂测试条数（不含 PPP）

    // 信息录入各类别（结果表格下拉可改）
    QString department;      // 科别
    QString gender;          // 性别
    QString ward;            // 病区
    QString bed;             // 床号
    QString sendingDoctor;   // 送检医生
    QString testingDoctor;   // 检验医生
    QString reviewingDoctor; // 审核医生
    QString diagnosis;       // 临床诊断
    QString remark;          // 备注

    // 测试状态：待测 / 测试中 / 测试结束 / 异常结果
    QString testStatus;
};

/**
 * 测试数据 SQLite 存储。
 *
 * 表结构：
 *   samples      每个样本一行（样本号、患者信息、样本类型、机型、时间、
 *                ppp_value 样本级 PPP 基准 AD）
 *   sample_tests 每个样本的每个试剂测试一行，保存 PRP0（不含 PPP）
 *   test_points  每个试剂测试的 PRP1~PRP300 各一行（second, prp_value）
 *
 * PPP 与 PRP0 一样只是计算集聚率的基准值，不是试剂，因此不作为
 * sample_tests 的一行保存。这样避免一张表出现 5 × 301 个列，便于查询、
 * 导出和后续增加试剂。
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

    /** 保存一个样本、其 PPP 基准值和最多 5 个试剂数据；成功返回样本 id */
    qint64 saveSample(const SampleTestData &sample, QString *error = nullptr);

    /**
     * 测试流程增量保存：确保样本行存在（按样本号），返回 sample_id。
     * 已存在时直接返回已有 id，不重复插入样本。
     */
    qint64 ensureSample(const SampleTestData &sample, QString *error = nullptr);

    /** 追加一条试剂测试及其 PRP1~PRP300；sample_id 由 ensureSample 得到 */
    bool appendTest(qint64 sampleId, const TestCurveData &test,
                    QString *error = nullptr);

    /** 更新样本级 PPP 基准值（samples.ppp_value），不新增试剂行 */
    bool setSamplePppValue(qint64 sampleId, int pppValue,
                           QString *error = nullptr);

    bool loadSample(qint64 sampleId, SampleTestData *sample,
                    QString *error = nullptr) const;
    QVector<SampleSummary> recentSamples(int limit = 200,
                                         QString *error = nullptr) const;

    /**
     * 等价 recentSamples 但仅限某一天（结果信息表只显示当天样本）。
     * dayKey 形如 "yyyy-MM-dd"；取该天 00:00:00 ~ 23:59:59。
     */
    QVector<SampleSummary> samplesOnDay(const QString &dayKey,
                                        int limit = 200,
                                        QString *error = nullptr) const;

    /**
     * 定条件查询样本（查询页用）。
     * keyword 匹配样本号或患者姓名（模糊）；fromIso/toIso 为 ISO 起始/截止时间
     * （含边界，任一为空表示不限这一端）。空库/无匹配返回空列表。
     */
    QVector<SampleSummary> querySamples(const QString &keyword,
                                        const QString &fromIso,
                                        const QString &toIso,
                                        int limit = 500,
                                        QString *error = nullptr) const;

    /** 更新样本信息录入各类别子项（结果表格下拉修改） */
    bool updateSampleEntryValue(qint64 sampleId, const QString &entryKey,
                                const QString &value, QString *error = nullptr);

    /**
     * 结果表格用：按样本号确保存在一条“待测”占位记录（无曲线数据）。
     * 已存在同号样本时不动；否则插入一条 test_status=待测 的样本行。
     */
    qint64 ensurePendingSample(const QString &sampleNumber,
                               const QString &sampleType,
                               QString *error = nullptr);

    /** 更新样本测试状态：待测 / 测试中 / 测试结束 / 异常结果 */
    bool updateSampleTestStatus(const QString &sampleNumber,
                                const QString &status,
                                QString *error = nullptr);
    /** 按样本 id 更新状态：同一样本号跨天有多条记录时避免更新错行 */
    bool updateSampleTestStatusById(qint64 sampleId, const QString &status,
                                    QString *error = nullptr);

    /**
     * 当天（date）的下一个样本号：T + yyyyMMdd + 4 位序号。
     * 取 samples 表中该日期前缀的最大序号 + 1；不写数据库。
     * 数据库未打开、查询失败或当天序号已用完时返回空字符串。
     */
    QString nextSampleNumber(const QDate &date, QString *error = nullptr) const;

    /** 当天 samples 表中是否已存在该样本号（用于新增/修改样本号查重）；
     *  不同天允许重复使用同一样本号，靠样本 id/test_time 区分 */
    bool sampleNumberExists(const QString &sampleNumber) const;

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

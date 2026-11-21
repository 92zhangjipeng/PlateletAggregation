#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "testdatastore.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QStringList>
#include <QVariant>

namespace {

QString normalizedReagent(const QString &reagent)
{
    return reagent.trimmed().toUpper();
}

bool isAllowedReagent(const QString &reagent)
{
    return reagent == QLatin1String("PPP")
            || reagent == QLatin1String("AA")
            || reagent == QLatin1String("ADP")
            || reagent == QLatin1String("COL")
            || reagent == QLatin1String("EPI")
            || reagent == QLatin1String("RIS");
}

QString currentIsoTime()
{
    return QDateTime::currentDateTime().toString(Qt::ISODate);
}

QString isoTime(const QDateTime &dateTime)
{
    return (dateTime.isValid() ? dateTime : QDateTime::currentDateTime())
            .toString(Qt::ISODate);
}

} // namespace

TestDataStore::TestDataStore(const QString &databasePath, QObject *parent)
    : QObject(parent)
    , m_databasePath(databasePath.isEmpty()
                     ? defaultDatabasePath() : databasePath)
{
    m_connectionName = QStringLiteral("platelet_testdata_%1")
            .arg(reinterpret_cast<quintptr>(this), 0, 16);
}

TestDataStore::~TestDataStore()
{
    close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connectionName);
}

QString TestDataStore::defaultDatabasePath()
{
    QString directory = QStandardPaths::writableLocation(
                QStandardPaths::AppDataLocation);
    if (directory.isEmpty()) {
        directory = QDir(QCoreApplication::applicationDirPath())
                .filePath(QStringLiteral("data"));
    }
    return QDir(directory).filePath(
                QStringLiteral("platelet_test_data.db"));
}

bool TestDataStore::open(QString *error)
{
    if (m_open) {
        return true;
    }
    if (error) {
        error->clear();
    }

    const QFileInfo dbInfo(m_databasePath);
    if (!QDir().mkpath(dbInfo.absolutePath())) {
        if (error) {
            *error = tr("无法创建数据库目录：%1").arg(dbInfo.absolutePath());
        }
        return false;
    }

    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        if (error) {
            *error = tr("当前 Qt 未加载 QSQLITE 数据库驱动。");
        }
        return false;
    }

    if (!m_db.isValid()) {
        m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                         m_connectionName);
    }
    m_db.setDatabaseName(m_databasePath);

    if (!m_db.open()) {
        if (error) {
            *error = tr("打开测试数据库失败：%1").arg(m_db.lastError().text());
        }
        return false;
    }

    QSqlQuery pragma(m_db);
    pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    pragma.exec(QStringLiteral("PRAGMA busy_timeout = 5000"));

    if (!ensureSchema(error)) {
        m_db.close();
        return false;
    }

    m_open = true;
    return true;
}

void TestDataStore::close()
{
    if (m_db.isValid()) {
        m_db.close();
    }
    m_open = false;
}

bool TestDataStore::isOpen() const
{
    return m_open && m_db.isValid() && m_db.isOpen();
}

QString TestDataStore::databasePath() const
{
    return m_databasePath;
}

QString TestDataStore::nextSampleNumber(const QDate &date, QString *error) const
{
    if (error) {
        error->clear();
    }
    if (!isOpen()) {
        if (error) {
            *error = tr("测试数据库未打开");
        }
        return QString();
    }
    if (!date.isValid()) {
        if (error) {
            *error = tr("日期无效");
        }
        return QString();
    }

    const QString prefix = QStringLiteral("T%1")
            .arg(date.toString(QStringLiteral("yyyyMMdd")));
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT sample_no FROM samples WHERE sample_no LIKE :prefix"));
    query.bindValue(QStringLiteral(":prefix"), prefix + QLatin1Char('%'));
    if (!query.exec()) {
        if (error) {
            *error = tr("查询当天最大样本号失败：%1")
                    .arg(query.lastError().text());
        }
        return QString();
    }

    int maxSequence = 0;
    while (query.next()) {
        const QString number = query.value(0).toString().trimmed();
        if (number.size() != 13 || !number.startsWith(prefix)) {
            continue;
        }
        bool ok = false;
        const int sequence = number.mid(9).toInt(&ok);
        if (ok && sequence > maxSequence) {
            maxSequence = sequence;
        }
    }

    if (maxSequence >= 9999) {
        if (error) {
            *error = tr("当天样本号已用完（最大 9999）。");
        }
        return QString();
    }
    return prefix + QStringLiteral("%1")
            .arg(maxSequence + 1, 4, 10, QLatin1Char('0'));
}

bool TestDataStore::sampleNumberExists(const QString &sampleNumber) const
{
    const QString trimmed = sampleNumber.trimmed();
    if (trimmed.isEmpty() || !isOpen()) {
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT 1 FROM samples WHERE sample_no = :no LIMIT 1"));
    query.bindValue(QStringLiteral(":no"), trimmed);
    if (!query.exec()) {
        qWarning() << "sampleNumberExists query failed:"
                   << query.lastError().text();
        return false;
    }
    return query.next();
}

bool TestDataStore::ensureSchema(QString *error)
{
    const QStringList statements = {
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS samples ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  sample_no TEXT NOT NULL,"
            "  patient_name TEXT,"
            "  patient_id TEXT,"
            "  gender TEXT,"
            "  age INTEGER,"
            "  sample_type TEXT,"
            "  machine_model TEXT,"
            "  operator_name TEXT,"
            "  test_time TEXT NOT NULL,"
            "  created_at TEXT NOT NULL,"
            "  remark TEXT"
            ")"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_samples_no"
            " ON samples(sample_no)"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_samples_time"
            " ON samples(test_time)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS sample_tests ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  sample_id INTEGER NOT NULL,"
            "  test_index INTEGER NOT NULL,"
            "  reagent TEXT NOT NULL CHECK(reagent IN"
            "    ('PPP','AA','ADP','COL','EPI','RIS')),"
            "  prp0 INTEGER,"
            "  points_count INTEGER NOT NULL DEFAULT 0,"
            "  reaction_seconds INTEGER NOT NULL DEFAULT 300,"
            "  module_no INTEGER,"
            "  local_channel INTEGER,"
            "  global_channel INTEGER,"
            "  status TEXT,"
            "  created_at TEXT NOT NULL,"
            "  UNIQUE(sample_id, reagent),"
            "  FOREIGN KEY(sample_id) REFERENCES samples(id) ON DELETE CASCADE"
            ")"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_sample_tests_sample"
            " ON sample_tests(sample_id, test_index)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS test_points ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  test_id INTEGER NOT NULL,"
            "  second INTEGER NOT NULL CHECK(second BETWEEN 1 AND 300),"
            "  prp_value INTEGER NOT NULL,"
            "  aggregation_percent REAL,"
            "  FOREIGN KEY(test_id) REFERENCES sample_tests(id)"
            "    ON DELETE CASCADE,"
            "  UNIQUE(test_id, second)"
            ")"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_test_points_test"
            " ON test_points(test_id, second)"),
        QStringLiteral(
            "CREATE VIEW IF NOT EXISTS v_sample_test_summary AS"
            " SELECT s.id AS sample_id, s.sample_no, s.patient_name,"
            "        s.sample_type, s.machine_model, s.test_time,"
            "        t.id AS test_id, t.reagent, t.prp0, t.points_count"
            " FROM samples s"
            " LEFT JOIN sample_tests t ON t.sample_id = s.id")
    };

    for (const QString &sql : statements) {
        QSqlQuery query(m_db);
        if (!query.exec(sql)) {
            if (error) {
                *error = tr("创建测试数据表失败：%1\n%2")
                        .arg(query.lastError().text(), sql);
            }
            return false;
        }
    }
    return true;
}

bool TestDataStore::validateSample(const SampleTestData &sample,
                                   QString *error)
{
    if (error) {
        error->clear();
    }
    if (sample.sampleNumber.trimmed().isEmpty()) {
        if (error) {
            *error = tr("样本号不能为空。");
        }
        return false;
    }
    if (sample.tests.isEmpty()) {
        if (error) {
            *error = tr("样本至少需要一条 PPP 或试剂测试数据。");
        }
        return false;
    }
    if (sample.tests.size() > 6) {
        if (error) {
            *error = tr("一个样本最多保存 1 个 PPP + 5 个试剂测试。");
        }
        return false;
    }

    int pppCount = 0;
    int reagentCount = 0;
    QStringList seenReagents;
    for (const TestCurveData &test : sample.tests) {
        const QString reagent = normalizedReagent(test.reagent);
        if (!isAllowedReagent(reagent)) {
            if (error) {
                *error = tr("不支持的试剂类型：%1").arg(test.reagent);
            }
            return false;
        }
        if (seenReagents.contains(reagent, Qt::CaseInsensitive)) {
            if (error) {
                *error = tr("同一试剂不能重复保存：%1").arg(reagent);
            }
            return false;
        }
        seenReagents.append(reagent);

        if (reagent == QLatin1String("PPP")) {
            ++pppCount;
        } else {
            ++reagentCount;
        }
        if (test.prpPoints.size() > 300) {
            if (error) {
                *error = tr("%1 的 PRP 点数超过 300。").arg(reagent);
            }
            return false;
        }
        if (!test.aggregationPoints.isEmpty()
                && test.aggregationPoints.size() != test.prpPoints.size()) {
            if (error) {
                *error = tr("%1 的集聚曲线点数与 PRP 点数不一致。").arg(reagent);
            }
            return false;
        }
    }

    if (pppCount > 1 || reagentCount > 5) {
        if (error) {
            *error = tr("一个样本只允许 1 个 PPP 和最多 5 个试剂测试。");
        }
        return false;
    }
    return true;
}

qint64 TestDataStore::saveSample(const SampleTestData &sample, QString *error)
{
    if (error) {
        error->clear();
    }
    if (!validateSample(sample, error)) {
        return -1;
    }
    if (!isOpen() && !open(error)) {
        return -1;
    }
    if (!m_db.transaction()) {
        if (error) {
            *error = tr("开启数据库事务失败：%1").arg(m_db.lastError().text());
        }
        return -1;
    }

    auto rollback = [this](QString *errorMessage, const QString &message) {
        m_db.rollback();
        if (errorMessage) {
            *errorMessage = message;
        }
        return static_cast<qint64>(-1);
    };

    QSqlQuery sampleQuery(m_db);
    sampleQuery.prepare(QStringLiteral(
        "INSERT INTO samples("
        "  sample_no, patient_name, patient_id, gender, age,"
        "  sample_type, machine_model, operator_name,"
        "  test_time, created_at, remark"
        ") VALUES ("
        "  :sample_no, :patient_name, :patient_id, :gender, :age,"
        "  :sample_type, :machine_model, :operator_name,"
        "  :test_time, :created_at, :remark"
        ")"));
    sampleQuery.bindValue(":sample_no", sample.sampleNumber.trimmed());
    sampleQuery.bindValue(":patient_name", sample.patientName);
    sampleQuery.bindValue(":patient_id", sample.patientId);
    sampleQuery.bindValue(":gender", sample.gender);
    sampleQuery.bindValue(":age", sample.age >= 0 ? QVariant(sample.age)
                                                   : QVariant());
    sampleQuery.bindValue(":sample_type", sample.sampleType);
    sampleQuery.bindValue(":machine_model", sample.machineModel);
    sampleQuery.bindValue(":operator_name", sample.operatorName);
    sampleQuery.bindValue(":test_time", isoTime(sample.testTime));
    sampleQuery.bindValue(":created_at", currentIsoTime());
    sampleQuery.bindValue(":remark", sample.remark);

    if (!sampleQuery.exec()) {
        return rollback(error, tr("写入样本信息失败：%1")
                        .arg(sampleQuery.lastError().text()));
    }
    const qint64 sampleId = sampleQuery.lastInsertId().toLongLong();

    QSqlQuery testQuery(m_db);
    testQuery.prepare(QStringLiteral(
        "INSERT INTO sample_tests("
        "  sample_id, test_index, reagent, prp0, points_count,"
        "  reaction_seconds, module_no, local_channel, global_channel,"
        "  status, created_at"
        ") VALUES ("
        "  :sample_id, :test_index, :reagent, :prp0, :points_count,"
        "  :reaction_seconds, :module_no, :local_channel, :global_channel,"
        "  :status, :created_at"
        ")"));

    QSqlQuery pointQuery(m_db);
    pointQuery.prepare(QStringLiteral(
        "INSERT INTO test_points("
        "  test_id, second, prp_value, aggregation_percent"
        ") VALUES (:test_id, :second, :prp_value, :aggregation_percent)"));

    int reagentIndex = 0;
    for (const TestCurveData &test : sample.tests) {
        const QString reagent = normalizedReagent(test.reagent);
        const int testIndex = (reagent == QLatin1String("PPP"))
                ? 0 : ++reagentIndex;

        testQuery.bindValue(":sample_id", sampleId);
        testQuery.bindValue(":test_index", testIndex);
        testQuery.bindValue(":reagent", reagent);
        testQuery.bindValue(":prp0", test.prp0 >= 0
                            ? QVariant(test.prp0) : QVariant());
        testQuery.bindValue(":points_count", test.prpPoints.size());
        testQuery.bindValue(":reaction_seconds", 300);
        testQuery.bindValue(":module_no", test.moduleNumber > 0
                            ? QVariant(test.moduleNumber) : QVariant());
        testQuery.bindValue(":local_channel", test.localChannel > 0
                            ? QVariant(test.localChannel) : QVariant());
        testQuery.bindValue(":global_channel", test.globalChannel > 0
                            ? QVariant(test.globalChannel) : QVariant());
        testQuery.bindValue(":status", test.status);
        testQuery.bindValue(":created_at", currentIsoTime());

        if (!testQuery.exec()) {
            return rollback(error, tr("写入 %1 测试记录失败：%2")
                            .arg(reagent, testQuery.lastError().text()));
        }
        const qint64 testId = testQuery.lastInsertId().toLongLong();

        for (int i = 0; i < test.prpPoints.size(); ++i) {
            pointQuery.bindValue(":test_id", testId);
            pointQuery.bindValue(":second", i + 1);
            pointQuery.bindValue(":prp_value", test.prpPoints.at(i));
            pointQuery.bindValue(
                        ":aggregation_percent",
                        i < test.aggregationPoints.size()
                        ? QVariant(test.aggregationPoints.at(i)) : QVariant());
            if (!pointQuery.exec()) {
                return rollback(error, tr("写入 %1 第 %2 个 PRP 点失败：%3")
                                .arg(reagent)
                                .arg(i + 1)
                                .arg(pointQuery.lastError().text()));
            }
        }
    }

    if (!m_db.commit()) {
        return rollback(error, tr("提交测试数据失败：%1")
                        .arg(m_db.lastError().text()));
    }
    return sampleId;
}

bool TestDataStore::loadSample(qint64 sampleId, SampleTestData *sample,
                               QString *error) const
{
    if (error) {
        error->clear();
    }
    if (!sample) {
        if (error) {
            *error = tr("目标样本对象为空。");
        }
        return false;
    }
    if (!isOpen()) {
        if (error) {
            *error = tr("测试数据库尚未打开。");
        }
        return false;
    }

    QSqlQuery sampleQuery(m_db);
    sampleQuery.prepare(QStringLiteral(
        "SELECT sample_no, patient_name, patient_id, gender, age,"
        "       sample_type, machine_model, operator_name, remark, test_time"
        " FROM samples WHERE id = :id"));
    sampleQuery.bindValue(":id", sampleId);
    if (!sampleQuery.exec() || !sampleQuery.next()) {
        if (error) {
            *error = tr("找不到样本 id=%1：%2")
                    .arg(sampleId)
                    .arg(sampleQuery.lastError().text());
        }
        return false;
    }

    SampleTestData result;
    result.sampleNumber = sampleQuery.value(0).toString();
    result.patientName = sampleQuery.value(1).toString();
    result.patientId = sampleQuery.value(2).toString();
    result.gender = sampleQuery.value(3).toString();
    result.age = sampleQuery.value(4).isNull() ? -1
                                                : sampleQuery.value(4).toInt();
    result.sampleType = sampleQuery.value(5).toString();
    result.machineModel = sampleQuery.value(6).toString();
    result.operatorName = sampleQuery.value(7).toString();
    result.remark = sampleQuery.value(8).toString();
    result.testTime = QDateTime::fromString(sampleQuery.value(9).toString(),
                                            Qt::ISODate);

    QSqlQuery testQuery(m_db);
    testQuery.prepare(QStringLiteral(
        "SELECT id, reagent, prp0, module_no, local_channel,"
        "       global_channel, status"
        " FROM sample_tests WHERE sample_id = :sample_id"
        " ORDER BY test_index, id"));
    testQuery.bindValue(":sample_id", sampleId);
    if (!testQuery.exec()) {
        if (error) {
            *error = tr("读取测试记录失败：%1").arg(testQuery.lastError().text());
        }
        return false;
    }

    QSqlQuery pointQuery(m_db);
    pointQuery.prepare(QStringLiteral(
        "SELECT second, prp_value, aggregation_percent"
        " FROM test_points WHERE test_id = :test_id ORDER BY second"));

    while (testQuery.next()) {
        TestCurveData test;
        const qint64 testId = testQuery.value(0).toLongLong();
        test.reagent = testQuery.value(1).toString();
        test.prp0 = testQuery.value(2).isNull()
                ? -1 : testQuery.value(2).toInt();
        test.moduleNumber = testQuery.value(3).isNull()
                ? 0 : testQuery.value(3).toInt();
        test.localChannel = testQuery.value(4).isNull()
                ? 0 : testQuery.value(4).toInt();
        test.globalChannel = testQuery.value(5).isNull()
                ? 0 : testQuery.value(5).toInt();
        test.status = testQuery.value(6).toString();

        pointQuery.bindValue(":test_id", testId);
        if (!pointQuery.exec()) {
            if (error) {
                *error = tr("读取 %1 的 PRP 点失败：%2")
                        .arg(test.reagent, pointQuery.lastError().text());
            }
            return false;
        }
        while (pointQuery.next()) {
            test.prpPoints.append(pointQuery.value(1).toInt());
            if (!pointQuery.value(2).isNull()) {
                test.aggregationPoints.append(pointQuery.value(2).toDouble());
            }
        }
        result.tests.append(test);
    }

    *sample = result;
    return true;
}

QVector<SampleSummary> TestDataStore::recentSamples(int limit,
                                                    QString *error) const
{
    if (error) {
        error->clear();
    }
    QVector<SampleSummary> result;
    if (!isOpen()) {
        if (error) {
            *error = tr("测试数据库尚未打开。");
        }
        return result;
    }

    const int boundedLimit = qBound(1, limit, 5000);
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT s.id, s.sample_no, s.patient_name, s.sample_type,"
        "       s.machine_model, s.test_time,"
        "       (SELECT COUNT(*) FROM sample_tests t"
        "        WHERE t.sample_id = s.id) AS test_count"
        " FROM samples s"
        " ORDER BY s.test_time DESC, s.id DESC"
        " LIMIT :limit"));
    query.bindValue(":limit", boundedLimit);
    if (!query.exec()) {
        if (error) {
            *error = tr("读取最近样本失败：%1").arg(query.lastError().text());
        }
        return result;
    }

    while (query.next()) {
        SampleSummary summary;
        summary.id = query.value(0).toLongLong();
        summary.sampleNumber = query.value(1).toString();
        summary.patientName = query.value(2).toString();
        summary.sampleType = query.value(3).toString();
        summary.machineModel = query.value(4).toString();
        summary.testTime = QDateTime::fromString(query.value(5).toString(),
                                                 Qt::ISODate);
        summary.testCount = query.value(6).toInt();
        result.append(summary);
    }
    return result;
}

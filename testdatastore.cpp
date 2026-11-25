#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "testdatastore.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSet>
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
    // PPP 不是试剂，是样本级基准值（samples.ppp_value），不能写进试剂表。
    return reagent == QLatin1String("AA")
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
    // 数据库文件放在程序目录下的 data 文件夹，便于查找和备份
    QString directory = QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("data"));
    if (!QFileInfo(directory).isDir() && !QDir().mkpath(directory)) {
        // 程序目录不可写（如安装在 Program Files）时退回用户数据目录
        directory = QStandardPaths::writableLocation(
                    QStandardPaths::AppDataLocation);
        if (directory.isEmpty()) {
            directory = QCoreApplication::applicationDirPath();
        }
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

    // 只检查当天：同一样本号在不同天允许再次使用，落库时按日期分成
    // 不同的 samples 行（不同 id/test_time），各自保存 300 点数据。
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT 1 FROM samples WHERE sample_no = :no"
        "   AND substr(test_time, 1, 10) = :today LIMIT 1"));
    query.bindValue(QStringLiteral(":no"), trimmed);
    query.bindValue(QStringLiteral(":today"),
                    QDate::currentDate().toString(Qt::ISODate));
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
            "  category TEXT,"
            "  gender_entry TEXT,"
            "  ward TEXT,"
            "  bed TEXT,"
            "  sending_doctor TEXT,"
            "  testing_doctor TEXT,"
            "  reviewing_doctor TEXT,"
            "  diagnosis TEXT,"
            "  test_status TEXT,"
            "  ppp_value INTEGER,"
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
        // 同一样本号可能在不同天重复使用：按 (样本号, 日期) 查当天记录。
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_samples_no_time"
            " ON samples(sample_no, test_time)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS sample_tests ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  sample_id INTEGER NOT NULL,"
            "  test_index INTEGER NOT NULL CHECK(test_index BETWEEN 1 AND 5),"
            "  reagent TEXT NOT NULL CHECK(reagent IN"
            "    ('AA','ADP','COL','EPI','RIS')),"
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
            " ON test_points(test_id, second)")
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

    // 旧库升级：samples 表补新列（已存在则报错被忽略）
    QSqlQuery alter(m_db);
    alter.exec(QStringLiteral(
        "ALTER TABLE samples ADD COLUMN category TEXT"));
    alter.exec(QStringLiteral(
        "ALTER TABLE samples ADD COLUMN gender_entry TEXT"));
    alter.exec(QStringLiteral(
        "ALTER TABLE samples ADD COLUMN ward TEXT"));
    alter.exec(QStringLiteral(
        "ALTER TABLE samples ADD COLUMN bed TEXT"));
    alter.exec(QStringLiteral(
        "ALTER TABLE samples ADD COLUMN sending_doctor TEXT"));
    alter.exec(QStringLiteral(
        "ALTER TABLE samples ADD COLUMN testing_doctor TEXT"));
    alter.exec(QStringLiteral(
        "ALTER TABLE samples ADD COLUMN reviewing_doctor TEXT"));
    alter.exec(QStringLiteral(
        "ALTER TABLE samples ADD COLUMN diagnosis TEXT"));
    alter.exec(QStringLiteral(
        "ALTER TABLE samples ADD COLUMN test_status TEXT"));
    alter.exec(QStringLiteral(
        "ALTER TABLE samples ADD COLUMN ppp_value INTEGER"));

    // 旧版本把 PPP 当成一条 sample_tests 记录保存（test_index=0）。
    // PPP 只是样本级基准值：迁到 samples.ppp_value 后删除旧试剂行。
    QSqlQuery migrate(m_db);
    if (!migrate.exec(QStringLiteral(
            "UPDATE samples SET ppp_value = ("
            "  SELECT t.prp0 FROM sample_tests t"
            "  WHERE t.sample_id = samples.id AND t.reagent = 'PPP'"
            "  ORDER BY t.id LIMIT 1)"
            " WHERE ppp_value IS NULL AND EXISTS ("
            "  SELECT 1 FROM sample_tests t"
            "  WHERE t.sample_id = samples.id AND t.reagent = 'PPP')"))) {
        if (error) {
            *error = tr("迁移旧 PPP 数据失败：%1")
                    .arg(migrate.lastError().text());
        }
        return false;
    }
    if (!migrate.exec(QStringLiteral(
            "DELETE FROM sample_tests WHERE reagent = 'PPP'"))) {
        if (error) {
            *error = tr("删除旧 PPP 试剂行失败：%1")
                    .arg(migrate.lastError().text());
        }
        return false;
    }

    // 视图定义随 ppp_value 变化，旧库需要先删除再重建。
    if (!migrate.exec(QStringLiteral(
            "DROP VIEW IF EXISTS v_sample_test_summary"))) {
        if (error) {
            *error = tr("重建测试汇总视图失败：%1")
                    .arg(migrate.lastError().text());
        }
        return false;
    }
    if (!migrate.exec(QStringLiteral(
            "CREATE VIEW v_sample_test_summary AS"
            " SELECT s.id AS sample_id, s.sample_no, s.patient_name,"
            "        s.sample_type, s.machine_model, s.test_time,"
            "        s.ppp_value,"
            "        t.id AS test_id, t.reagent, t.prp0, t.points_count"
            " FROM samples s"
            " LEFT JOIN sample_tests t ON t.sample_id = s.id"
            "   AND t.reagent <> 'PPP'"))) {
        if (error) {
            *error = tr("创建测试汇总视图失败：%1")
                    .arg(migrate.lastError().text());
        }
        return false;
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
    if (sample.tests.isEmpty() && sample.pppValue < 0) {
        if (error) {
            *error = tr("样本至少需要 PPP 基准值或一条试剂测试数据。");
        }
        return false;
    }
    if (sample.tests.size() > 5) {
        if (error) {
            *error = tr("一个样本最多保存 5 个试剂测试。");
        }
        return false;
    }
    if (sample.pppValue > 4095) {
        if (error) {
            *error = tr("PPP 基准值超出 AD 有效范围（0~4095）：%1")
                    .arg(sample.pppValue);
        }
        return false;
    }

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

        if (test.prpPoints.size() > 300) {
            if (error) {
                *error = tr("%1 的 PRP 点数超过 300。").arg(reagent);
            }
            return false;
        }
        // 集聚率可以少于 PRP 点（基准无效时没有集聚数据），
        // 但绝不能多于 PRP 点；缺失部分按 NULL 保存。
        if (test.aggregationPoints.size() > test.prpPoints.size()) {
            if (error) {
                *error = tr("%1 的集聚曲线点数多于 PRP 点数。").arg(reagent);
            }
            return false;
        }
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
        "  ppp_value, test_time, created_at, remark"
        ") VALUES ("
        "  :sample_no, :patient_name, :patient_id, :gender, :age,"
        "  :sample_type, :machine_model, :operator_name,"
        "  :ppp_value, :test_time, :created_at, :remark"
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
    sampleQuery.bindValue(":ppp_value", sample.pppValue >= 0
                          ? QVariant(sample.pppValue) : QVariant());
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
        const int testIndex = ++reagentIndex;

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

qint64 TestDataStore::ensureSample(const SampleTestData &sample,
                                   QString *error)
{
    if (error) {
        error->clear();
    }
    const QString sampleNumber = sample.sampleNumber.trimmed();
    if (sampleNumber.isEmpty()) {
        if (error) {
            *error = tr("样本号不能为空。");
        }
        return -1;
    }
    if (!isOpen() && !open(error)) {
        return -1;
    }

    // 样本号在不同天可能重复使用：同一天才视为同一条样本记录，
    // 跨天必须新建 samples 行，用 id + test_time 区分各自的 300 点数据。
    const QDate testDate = (sample.testTime.isValid()
                            ? sample.testTime : QDateTime::currentDateTime())
            .date();
    QSqlQuery findQuery(m_db);
    findQuery.prepare(QStringLiteral(
        "SELECT id FROM samples WHERE sample_no = :sample_no"
        "   AND substr(test_time, 1, 10) = :test_date"
        " ORDER BY id DESC LIMIT 1"));
    findQuery.bindValue(":sample_no", sampleNumber);
    findQuery.bindValue(":test_date", testDate.toString(Qt::ISODate));
    if (!findQuery.exec()) {
        if (error) {
            *error = tr("查询样本失败：%1").arg(findQuery.lastError().text());
        }
        return -1;
    }
    if (findQuery.next()) {
        return findQuery.value(0).toLongLong();
    }

    QSqlQuery insertQuery(m_db);
    insertQuery.prepare(QStringLiteral(
        "INSERT INTO samples("
        "  sample_no, patient_name, patient_id, gender, age,"
        "  sample_type, machine_model, operator_name,"
        "  ppp_value, test_time, created_at, remark"
        ") VALUES ("
        "  :sample_no, :patient_name, :patient_id, :gender, :age,"
        "  :sample_type, :machine_model, :operator_name,"
        "  :ppp_value, :test_time, :created_at, :remark"
        ")"));
    insertQuery.bindValue(":sample_no", sampleNumber);
    insertQuery.bindValue(":patient_name", sample.patientName);
    insertQuery.bindValue(":patient_id", sample.patientId);
    insertQuery.bindValue(":gender", sample.gender);
    insertQuery.bindValue(":age", sample.age >= 0 ? QVariant(sample.age)
                                                   : QVariant());
    insertQuery.bindValue(":sample_type", sample.sampleType);
    insertQuery.bindValue(":machine_model", sample.machineModel);
    insertQuery.bindValue(":operator_name", sample.operatorName);
    insertQuery.bindValue(":ppp_value", sample.pppValue >= 0
                          ? QVariant(sample.pppValue) : QVariant());
    insertQuery.bindValue(":test_time", isoTime(sample.testTime));
    insertQuery.bindValue(":created_at", currentIsoTime());
    insertQuery.bindValue(":remark", sample.remark);
    if (!insertQuery.exec()) {
        if (error) {
            *error = tr("写入样本信息失败：%1")
                    .arg(insertQuery.lastError().text());
        }
        return -1;
    }
    return insertQuery.lastInsertId().toLongLong();
}

bool TestDataStore::appendTest(qint64 sampleId, const TestCurveData &test,
                               QString *error)
{
    if (error) {
        error->clear();
    }
    if (sampleId <= 0) {
        if (error) {
            *error = tr("样本 id 无效。");
        }
        return false;
    }
    const QString reagent = normalizedReagent(test.reagent);
    if (!isAllowedReagent(reagent)) {
        if (error) {
            *error = tr("不支持的试剂类型：%1").arg(test.reagent);
        }
        return false;
    }
    if (test.prpPoints.size() > 300) {
        if (error) {
            *error = tr("%1 的 PRP 点数超过 300。").arg(reagent);
        }
        return false;
    }
    // 集聚率可以少于 PRP 点（基准无效时没有集聚数据），
    // 但绝不能多于 PRP 点；缺失部分按 NULL 保存。
    if (test.aggregationPoints.size() > test.prpPoints.size()) {
        if (error) {
            *error = tr("%1 的集聚曲线点数多于 PRP 点数。").arg(reagent);
        }
        return false;
    }
    if (!isOpen() && !open(error)) {
        return false;
    }
    if (!m_db.transaction()) {
        if (error) {
            *error = tr("开启数据库事务失败：%1").arg(m_db.lastError().text());
        }
        return false;
    }

    auto rollback = [this](QString *errorMessage, const QString &message) {
        m_db.rollback();
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    };

    QSqlQuery existingQuery(m_db);
    existingQuery.prepare(QStringLiteral(
        "SELECT reagent FROM sample_tests"
        " WHERE sample_id = :sample_id AND reagent <> 'PPP'"));
    existingQuery.bindValue(":sample_id", sampleId);
    if (!existingQuery.exec()) {
        return rollback(error, tr("读取已有测试记录失败：%1")
                        .arg(existingQuery.lastError().text()));
    }
    int reagentCount = 0;
    while (existingQuery.next()) {
        const QString existing = existingQuery.value(0).toString();
        if (existing == reagent) {
            // 该试剂已存在：更新状态（“测试中”占位 → 终态“完成/异常”），
            // 并覆盖/补齐曲线（pointQuery 用 UNIQUE(test_id, second) 覆盖）。
            QSqlQuery updateStatus(m_db);
            updateStatus.prepare(QStringLiteral(
                "UPDATE sample_tests SET status = :status,"
                "  prp0 = :prp0, points_count = :points_count,"
                "  module_no = :module_no, local_channel = :local_channel,"
                "  global_channel = :global_channel, reaction_seconds = 300"
                " WHERE sample_id = :sample_id AND reagent = :reagent"));
            updateStatus.bindValue(":status", test.status);
            updateStatus.bindValue(":prp0", test.prp0 >= 0
                                   ? QVariant(test.prp0) : QVariant());
            updateStatus.bindValue(":points_count", test.prpPoints.size());
            updateStatus.bindValue(":module_no", test.moduleNumber > 0
                                   ? QVariant(test.moduleNumber) : QVariant());
            updateStatus.bindValue(":local_channel", test.localChannel > 0
                                   ? QVariant(test.localChannel) : QVariant());
            updateStatus.bindValue(":global_channel", test.globalChannel > 0
                                   ? QVariant(test.globalChannel) : QVariant());
            updateStatus.bindValue(":sample_id", sampleId);
            updateStatus.bindValue(":reagent", reagent);
            if (!updateStatus.exec()) {
                return rollback(error, tr("更新 %1 测试记录失败：%2")
                                .arg(reagent, updateStatus.lastError().text()));
            }
            // 覆盖旧曲线点（先清空该试剂已有 test_points 再重写），
            // 使“测试中”占位行的空曲线被完整曲线替换。
            QSqlQuery flush(m_db);
            flush.prepare(QStringLiteral(
                "DELETE FROM test_points WHERE test_id IN ("
                "  SELECT id FROM sample_tests"
                "   WHERE sample_id = :sample_id AND reagent = :reagent)"));
            flush.bindValue(":sample_id", sampleId);
            flush.bindValue(":reagent", reagent);
            if (!flush.exec()) {
                return rollback(error, tr("清空 %1 旧曲线失败：%2")
                                .arg(reagent, flush.lastError().text()));
            }

            QSqlQuery pointQuery(m_db);
            pointQuery.prepare(QStringLiteral(
                "INSERT INTO test_points("
                "  test_id, second, prp_value, aggregation_percent"
                ") SELECT id, :second, :prp_value, :aggregation_percent"
                "  FROM sample_tests"
                " WHERE sample_id = :sample_id AND reagent = :reagent"));
            for (int i = 0; i < test.prpPoints.size(); ++i) {
                pointQuery.bindValue(":second", i + 1);
                pointQuery.bindValue(":prp_value", test.prpPoints.at(i));
                pointQuery.bindValue(":aggregation_percent",
                                     i < test.aggregationPoints.size()
                                     ? QVariant(test.aggregationPoints.at(i))
                                     : QVariant());
                pointQuery.bindValue(":sample_id", sampleId);
                pointQuery.bindValue(":reagent", reagent);
                if (!pointQuery.exec()) {
                    return rollback(error, tr("写入 %1 第 %2 个 PRP 点失败：%3")
                                    .arg(reagent)
                                    .arg(i + 1)
                                    .arg(pointQuery.lastError().text()));
                }
            }
            if (!m_db.commit()) {
                return rollback(error, tr("提交 %1 测试数据失败：%2")
                                .arg(reagent, m_db.lastError().text()));
            }
            return true;
        }
        ++reagentCount;
    }
    if (reagentCount >= 5) {
        return rollback(error, tr("一个样本最多 5 个试剂测试。"));
    }

    const int testIndex = reagentCount + 1;

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
    testQuery.bindValue(":sample_id", sampleId);
    testQuery.bindValue(":test_index", testIndex);
    testQuery.bindValue(":reagent", reagent);
    testQuery.bindValue(":prp0", test.prp0 >= 0 ? QVariant(test.prp0)
                                                 : QVariant());
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

    QSqlQuery pointQuery(m_db);
    pointQuery.prepare(QStringLiteral(
        "INSERT INTO test_points("
        "  test_id, second, prp_value, aggregation_percent"
        ") VALUES (:test_id, :second, :prp_value, :aggregation_percent)"));
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

    if (!m_db.commit()) {
        return rollback(error, tr("提交 %1 测试数据失败：%2")
                        .arg(reagent, m_db.lastError().text()));
    }
    return true;
}

bool TestDataStore::setSamplePppValue(qint64 sampleId, int pppValue,
                                      QString *error)
{
    if (error) {
        error->clear();
    }
    if (sampleId <= 0) {
        if (error) {
            *error = tr("样本 id 无效。");
        }
        return false;
    }
    if (pppValue > 4095) {
        if (error) {
            *error = tr("PPP 基准值超出 AD 有效范围（0~4095）：%1")
                    .arg(pppValue);
        }
        return false;
    }
    if (!isOpen() && !open(error)) {
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "UPDATE samples SET ppp_value = :ppp_value WHERE id = :id"));
    // -1 表示本次没有读到有效 PPP，写入 NULL，后续读到再更新。
    query.bindValue(":ppp_value", pppValue >= 0
                    ? QVariant(pppValue) : QVariant());
    query.bindValue(":id", sampleId);
    if (!query.exec()) {
        if (error) {
            *error = tr("写入 PPP 基准值失败：%1")
                    .arg(query.lastError().text());
        }
        return false;
    }
    if (query.numRowsAffected() == 0) {
        if (error) {
            *error = tr("找不到样本 id=%1，PPP 基准值未保存。").arg(sampleId);
        }
        return false;
    }
    return true;
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
        "       sample_type, machine_model, operator_name, remark, test_time,"
        "       ppp_value, gender_entry"
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
    result.pppValue = sampleQuery.value(10).isNull()
            ? -1 : sampleQuery.value(10).toInt();
    // 性别统一：优先权威列 gender，界面录入/结果表写的是 gender_entry，
    // 界面录入时 gender 常为空，回退到 gender_entry 保证详情与结果表一致。
    if (result.gender.isEmpty()) {
        result.gender = sampleQuery.value(11).toString();
    }

    QSqlQuery testQuery(m_db);
    testQuery.prepare(QStringLiteral(
        "SELECT id, reagent, prp0, module_no, local_channel,"
        "       global_channel, status"
        " FROM sample_tests WHERE sample_id = :sample_id"
        "   AND reagent <> 'PPP'"
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

QVector<SampleSummary> TestDataStore::querySamples(const QString &keyword,
                                                   const QString &fromIso,
                                                   const QString &toIso,
                                                   int limit,
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

    QStringList conditions;
    QVariantList binds;
    if (!keyword.isEmpty()) {
        conditions << QStringLiteral("(s.sample_no LIKE ? OR s.patient_name LIKE ?)");
        const QString pattern = QStringLiteral("%%%1%%").arg(keyword);
        binds << pattern << pattern;
    }
    if (!fromIso.isEmpty()) {
        conditions << QStringLiteral("s.test_time >= ?");
        binds << fromIso;
    }
    if (!toIso.isEmpty()) {
        conditions << QStringLiteral("s.test_time <= ?");
        binds << toIso;
    }
    QString where;
    if (!conditions.isEmpty()) {
        where = QStringLiteral(" WHERE ") + conditions.join(QStringLiteral(" AND "));
    }

    const int boundedLimit = qBound(1, limit, 5000);
    const QString sql =
            QStringLiteral("SELECT s.id, s.sample_no, s.patient_name, s.age,"
                           " s.sample_type, s.machine_model, s.test_time,"
                           " s.ppp_value, s.category, s.gender_entry, s.ward,"
                           " s.bed, s.sending_doctor, s.testing_doctor,"
                           " s.reviewing_doctor, s.diagnosis, s.test_status,"
                           " s.remark,"
                           " (SELECT COUNT(*) FROM sample_tests t"
                           "  WHERE t.sample_id = s.id AND t.reagent <> 'PPP')"
                           " AS test_count"
                           " FROM samples s")
            + where
            + QStringLiteral(" ORDER BY s.test_time DESC, s.id DESC"
                             " LIMIT ?");
    QSqlQuery query(m_db);
    query.prepare(sql);
    for (const QVariant &bind : binds) {
        query.addBindValue(bind);
    }
    query.addBindValue(boundedLimit);
    if (!query.exec()) {
        if (error) {
            *error = tr("查询样本失败：%1").arg(query.lastError().text());
        }
        return result;
    }

    while (query.next()) {
        SampleSummary summary;
        summary.id = query.value(0).toLongLong();
        summary.sampleNumber = query.value(1).toString();
        summary.patientName = query.value(2).toString();
        summary.age = query.value(3).isNull()
                ? -1 : query.value(3).toInt();
        summary.sampleType = query.value(4).toString();
        summary.machineModel = query.value(5).toString();
        summary.testTime = QDateTime::fromString(query.value(6).toString(),
                                                 Qt::ISODate);
        summary.pppValue = query.value(7).isNull()
                ? -1 : query.value(7).toInt();
        summary.department = query.value(8).toString();
        summary.gender = query.value(9).toString();
        summary.ward = query.value(10).toString();
        summary.bed = query.value(11).toString();
        summary.sendingDoctor = query.value(12).toString();
        summary.testingDoctor = query.value(13).toString();
        summary.reviewingDoctor = query.value(14).toString();
        summary.diagnosis = query.value(15).toString();
        summary.testStatus = query.value(16).toString();
        summary.remark = query.value(17).toString();
        summary.testCount = query.value(18).toInt();
        result.append(summary);
    }
    return result;
}

QVector<SampleSummary> TestDataStore::samplesOnDay(const QString &dayKey,
                                                   int limit,
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
    const QDate day = QDate::fromString(dayKey, QStringLiteral("yyyy-MM-dd"));
    if (!day.isValid()) {
        if (error) {
            *error = tr("无效日期：%1").arg(dayKey);
        }
        return result;
    }
    const QString fromIso = QDateTime(day, QTime(0, 0, 0)).toString(Qt::ISODate);
    const QString toIso = QDateTime(day, QTime(23, 59, 59)).toString(Qt::ISODate);

    const int boundedLimit = qBound(1, limit, 5000);
    const QString sql =
            QStringLiteral("SELECT s.id, s.sample_no, s.patient_name, s.age,"
                           " s.sample_type, s.machine_model, s.test_time,"
                           " s.ppp_value, s.category, s.gender_entry, s.ward,"
                           " s.bed, s.sending_doctor, s.testing_doctor,"
                           " s.reviewing_doctor, s.diagnosis, s.test_status,"
                           " s.remark,"
                           " (SELECT COUNT(*) FROM sample_tests t"
                           "  WHERE t.sample_id = s.id AND t.reagent <> 'PPP')"
                           " AS test_count"
                           " FROM samples s"
                           " WHERE s.test_time >= ? AND s.test_time <= ?"
                           " ORDER BY s.test_time DESC, s.id DESC"
                           " LIMIT ?");
    QSqlQuery query(m_db);
    query.prepare(sql);
    query.addBindValue(fromIso);
    query.addBindValue(toIso);
    query.addBindValue(boundedLimit);
    if (!query.exec()) {
        if (error) {
            *error = tr("读取当天样本失败：%1").arg(query.lastError().text());
        }
        return result;
    }

    while (query.next()) {
        SampleSummary summary;
        summary.id = query.value(0).toLongLong();
        summary.sampleNumber = query.value(1).toString();
        summary.patientName = query.value(2).toString();
        summary.age = query.value(3).isNull()
                ? -1 : query.value(3).toInt();
        summary.sampleType = query.value(4).toString();
        summary.machineModel = query.value(5).toString();
        summary.testTime = QDateTime::fromString(query.value(6).toString(),
                                                 Qt::ISODate);
        summary.pppValue = query.value(7).isNull()
                ? -1 : query.value(7).toInt();
        summary.department = query.value(8).toString();
        summary.gender = query.value(9).toString();
        summary.ward = query.value(10).toString();
        summary.bed = query.value(11).toString();
        summary.sendingDoctor = query.value(12).toString();
        summary.testingDoctor = query.value(13).toString();
        summary.reviewingDoctor = query.value(14).toString();
        summary.diagnosis = query.value(15).toString();
        summary.testStatus = query.value(16).toString();
        summary.remark = query.value(17).toString();
        summary.testCount = query.value(18).toInt();
        result.append(summary);
    }
    return result;
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
    const QString sql =
            QStringLiteral("SELECT s.id, s.sample_no, s.patient_name, s.age,"
                           " s.sample_type, s.machine_model, s.test_time,"
                           " s.ppp_value, s.category, s.gender_entry, s.ward,"
                           " s.bed, s.sending_doctor, s.testing_doctor,"
                           " s.reviewing_doctor, s.diagnosis, s.test_status,"
                           " s.remark,"
                           " (SELECT COUNT(*) FROM sample_tests t"
                           "  WHERE t.sample_id = s.id AND t.reagent <> 'PPP')"
                           " AS test_count"
                           " FROM samples s"
                           " ORDER BY s.test_time DESC, s.id DESC"
                           " LIMIT :limit");
    QSqlQuery query(m_db);
    query.prepare(sql);
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
        summary.age = query.value(3).isNull()
                ? -1 : query.value(3).toInt();
        summary.sampleType = query.value(4).toString();
        summary.machineModel = query.value(5).toString();
        summary.testTime = QDateTime::fromString(query.value(6).toString(),
                                                 Qt::ISODate);
        summary.pppValue = query.value(7).isNull()
                ? -1 : query.value(7).toInt();
        summary.department = query.value(8).toString();
        summary.gender = query.value(9).toString();
        summary.ward = query.value(10).toString();
        summary.bed = query.value(11).toString();
        summary.sendingDoctor = query.value(12).toString();
        summary.testingDoctor = query.value(13).toString();
        summary.reviewingDoctor = query.value(14).toString();
        summary.diagnosis = query.value(15).toString();
        summary.testStatus = query.value(16).toString();
        summary.remark = query.value(17).toString();
        summary.testCount = query.value(18).toInt();
        result.append(summary);
    }
    return result;
}

bool TestDataStore::updateSampleEntryValue(qint64 sampleId,
                                           const QString &entryKey,
                                           const QString &value,
                                           QString *error)
{
    // entryKey 只允许来自信息录入类别的已知键，防注入列名；
    // patient_name / age 为结果信息表直接编辑的姓名、年龄列。
    static const QSet<QString> allowed = {
        QStringLiteral("patient_name"),
        QStringLiteral("age"),
        QStringLiteral("category"), QStringLiteral("gender_entry"),
        QStringLiteral("ward"), QStringLiteral("bed"),
        QStringLiteral("sending_doctor"), QStringLiteral("testing_doctor"),
        QStringLiteral("reviewing_doctor"), QStringLiteral("diagnosis"),
        QStringLiteral("remark")
    };
    if (!allowed.contains(entryKey)) {
        if (error) {
            *error = tr("不支持的信息类别：%1").arg(entryKey);
        }
        return false;
    }
    if (!isOpen()) {
        if (error) {
            *error = tr("测试数据库尚未打开。");
        }
        return false;
    }
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "UPDATE samples SET %1 = :value WHERE id = :id")
            .arg(entryKey));
    if (entryKey == QLatin1String("age")) {
        // 年龄是 INTEGER：空值写 NULL，合法数字按整数写入。
        const QString trimmed = value.trimmed();
        bool ok = false;
        const int age = trimmed.toInt(&ok);
        query.bindValue(QStringLiteral(":value"),
                        ok && !trimmed.isEmpty()
                        ? QVariant(age) : QVariant());
    } else {
        query.bindValue(QStringLiteral(":value"), value.trimmed());
    }
    query.bindValue(QStringLiteral(":id"), sampleId);
    if (!query.exec()) {
        if (error) {
            *error = tr("更新信息失败：%1").arg(query.lastError().text());
        }
        return false;
    }
    return true;
}

qint64 TestDataStore::ensurePendingSample(const QString &sampleNumber,
                                          const QString &sampleType,
                                          QString *error)
{
    if (error) {
        error->clear();
    }
    const QString trimmed = sampleNumber.trimmed();
    if (trimmed.isEmpty()) {
        if (error) {
            *error = tr("样本号不能为空。");
        }
        return -1;
    }
    if (!isOpen() && !open(error)) {
        return -1;
    }

    // 只复用当天的样本行；同一样本号跨天再次测试时新建一行。
    QSqlQuery findQuery(m_db);
    findQuery.prepare(QStringLiteral(
        "SELECT id FROM samples WHERE sample_no = :sample_no"
        "   AND substr(test_time, 1, 10) = :test_date"
        " ORDER BY id DESC LIMIT 1"));
    findQuery.bindValue(QStringLiteral(":sample_no"), trimmed);
    findQuery.bindValue(QStringLiteral(":test_date"),
                        QDate::currentDate().toString(Qt::ISODate));
    if (!findQuery.exec()) {
        if (error) {
            *error = tr("查询样本失败：%1").arg(findQuery.lastError().text());
        }
        return -1;
    }
    if (findQuery.next()) {
        return findQuery.value(0).toLongLong();
    }

    QSqlQuery insertQuery(m_db);
    insertQuery.prepare(QStringLiteral(
        "INSERT INTO samples("
        "  sample_no, sample_type, test_time, created_at, test_status"
        ") VALUES ("
        "  :sample_no, :sample_type, :test_time, :created_at, :test_status"
        ")"));
    insertQuery.bindValue(QStringLiteral(":sample_no"), trimmed);
    insertQuery.bindValue(QStringLiteral(":sample_type"), sampleType);
    insertQuery.bindValue(QStringLiteral(":test_time"), currentIsoTime());
    insertQuery.bindValue(QStringLiteral(":created_at"), currentIsoTime());
    insertQuery.bindValue(QStringLiteral(":test_status"),
                          tr("待测"));
    if (!insertQuery.exec()) {
        if (error) {
            *error = tr("写入待测样本失败：%1")
                    .arg(insertQuery.lastError().text());
        }
        return -1;
    }
    return insertQuery.lastInsertId().toLongLong();
}

bool TestDataStore::updateSampleTestStatus(const QString &sampleNumber,
                                           const QString &status,
                                           QString *error)
{
    if (error) {
        error->clear();
    }
    if (!isOpen()) {
        if (error) {
            *error = tr("测试数据库尚未打开。");
        }
        return false;
    }
    // 同一样本号可能跨天存在多条记录：只更新最近一次测试的样本行，
    // 不能把历史日期的记录一起改掉。
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "UPDATE samples SET test_status = :status"
        " WHERE id = (SELECT id FROM samples"
        "             WHERE sample_no = :sample_no"
        "             ORDER BY id DESC LIMIT 1)"));
    query.bindValue(QStringLiteral(":status"), status);
    query.bindValue(QStringLiteral(":sample_no"), sampleNumber.trimmed());
    if (!query.exec()) {
        if (error) {
            *error = tr("更新测试状态失败：%1")
                    .arg(query.lastError().text());
        }
        return false;
    }
    return true;
}

bool TestDataStore::updateSampleTestStatusById(qint64 sampleId,
                                               const QString &status,
                                               QString *error)
{
    if (error) {
        error->clear();
    }
    if (sampleId <= 0) {
        if (error) {
            *error = tr("样本 id 无效。");
        }
        return false;
    }
    if (!isOpen()) {
        if (error) {
            *error = tr("测试数据库尚未打开。");
        }
        return false;
    }
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "UPDATE samples SET test_status = :status WHERE id = :id"));
    query.bindValue(QStringLiteral(":status"), status);
    query.bindValue(QStringLiteral(":id"), sampleId);
    if (!query.exec()) {
        if (error) {
            *error = tr("更新测试状态失败：%1")
                    .arg(query.lastError().text());
        }
        return false;
    }
    if (query.numRowsAffected() == 0) {
        if (error) {
            *error = tr("找不到样本 id=%1。").arg(sampleId);
        }
        return false;
    }
    return true;
}


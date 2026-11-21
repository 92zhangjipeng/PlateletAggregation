#ifndef COORDINATEFILEIO_H
#define COORDINATEFILEIO_H

#include "instrumentcoordinates.h"

#include <QString>
#include <QVector>

/**
 * 坐标文件的中间表示（与 JSON 格式一一对应）。
 *
 * 文件里每个目标位置最多包含 3 组工具坐标，下标与
 * InstrumentCoordinates::ZAxis 一致：0=抓手、1=试剂针、2=样本针。
 * xyValid / zValid 分别表示 X/Y 与 Z 是否已设置，导入时按勾选状态生效。
 */
struct CoordinateDocument
{
    struct ToolData
    {
        bool xyValid = false;
        bool zValid = false;
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
    };

    struct Entry
    {
        InstrumentCoordinates::TargetKind kind =
                InstrumentCoordinates::TargetKind::Origin;
        int index = 0;
        ToolData tools[3];
    };

    QString machineModel;   // 文件标注的机型，例如 KS1200
    int version = 0;        // 文件格式版本
    QVector<Entry> entries;
};

/**
 * 坐标 JSON 文件的读写。
 *
 * 文件格式（version 1）：
 * @code
 * {
 *   "format": "PlateletAggregationCoordinates",
 *   "version": 1,
 *   "machineModel": "KS1200",
 *   "unit": "mm",
 *   "exportedAt": "2026-11-21T18:30:00",
 *   "targets": [
 *     {
 *       "kind": "channel",
 *       "index": 0,
 *       "name": "CH1",
 *       "tools": {
 *         "gripper": {
 *           "xyValid": true, "x": 100.0, "y": 200.0,
 *           "zValid": true,  "z": 20.0
 *         }
 *       }
 *     }
 *   ]
 * }
 * @endcode
 *
 * kind 取值：origin / cleaningHole / discardCup / channel / tube /
 * reagent / sampleWell；tools 键：gripper / reagentNeedle / sampleNeedle。
 * 单位统一为毫米。导出只包含当前机型下已设置过坐标的目标。
 */
class CoordinateFileIo
{
public:
    struct ApplyResult
    {
        int applied = 0;       // 成功应用的目标数
        int skipped = 0;       // 超出当前机型范围而跳过的目标数
        int ignoredTools = 0;  // 数值越界而忽略的工具坐标组数
    };

    /** 导出当前机型的所有已设置坐标；写入失败时通过 error 返回原因 */
    static bool save(const InstrumentCoordinates &coordinates,
                     const QString &filePath,
                     QString *error = nullptr);

    /** 读取并校验坐标文件；成功时填充 document */
    static bool load(const QString &filePath,
                     CoordinateDocument *document,
                     QString *error = nullptr);

    /**
     * 用文档覆盖坐标表：先 clear()，再按当前机型范围写入。
     * 超出当前机型范围的目标会跳过并计数，不视为失败。
     */
    static ApplyResult apply(const CoordinateDocument &document,
                             InstrumentCoordinates *coordinates);

private:
    CoordinateFileIo() = delete;
};

#endif // COORDINATEFILEIO_H

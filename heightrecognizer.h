#ifndef HEIGHTRECOGNIZER_H
#define HEIGHTRECOGNIZER_H

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <QString>

/** 参照物物理高度（蓝色参照块），mm。 */
static const double kRefBlockPhysicalMm = 20.0;

struct HeightRecognitionResult {
    bool ok = false;
    double rbcHeightMm = 0.0;   // 红细胞高度（mm）
    int interfaceX = -1;        // 红细胞/血浆分界 X（横图，原图像素）
    int refLeft = -1;           // 蓝色参照块矩形
    int refTop = -1;
    int refW = 0;
    int refH = 0;
    double pxPerMm = 0.0;
    QString state;              // Normal / Hemolyzed / InvalidRegion
    double hemolysisIndex = 0.0;
};

/**
 * 全血测高的纯 OpenCV 基础识别：
 * 1) 在横放（landscape）BGR 图上找蓝色参照块，得 px/mm（参照块物理高 20mm）；
 * 2) 统计红色像素在每一列的分布，找红细胞与血浆的分界 X；
 * 3) 参照块右端（=“参照顶”）到分界的像素差 / px/mm → 红细胞高度 mm。
 *
 * 不依赖 bloodlayer（该库只有 x64 DLL，当前工程是 32 位），精度低于正式库，
 * 仅用于把识别高度回填到加样表 HeightColumn。
 */
class HeightRecognizer
{
public:
    static HeightRecognitionResult recognize(const cv::Mat &landscapeBgr);
};

#endif // HEIGHTRECOGNIZER_H
#include "heightrecognizer.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/opencv.hpp>

#include <QString>

#include <algorithm>
#include <vector>

// 红色（RBC）的两个 HSV 区间
namespace {
struct ColorRange {
    int lowH, highH, lowS, highS, lowV, highV;
};
const ColorRange kRed1{0, 8, 50, 255, 80, 220};
const ColorRange kRed2{170, 180, 50, 255, 80, 220};
// 蓝色参照块
const ColorRange kBlue{95, 130, 60, 255, 40, 255};
}

HeightRecognitionResult HeightRecognizer::recognize(const cv::Mat &landscapeBgr)
{
    HeightRecognitionResult out;
    if (landscapeBgr.empty()) {
        out.state = QStringLiteral("InvalidRegion");
        return out;
    }

    cv::Mat hsv;
    cv::cvtColor(landscapeBgr, hsv, cv::COLOR_BGR2HSV);
    cv::Mat blurred;
    cv::GaussianBlur(hsv, blurred, cv::Size(5, 5), 0);

    // 1) 蓝色参照块 → px/mm
    cv::Mat blueMask;
    cv::inRange(blurred,
                cv::Scalar(kBlue.lowH, kBlue.lowS, kBlue.lowV),
                cv::Scalar(kBlue.highH, kBlue.highS, kBlue.highV),
                blueMask);
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(blueMask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    double pxPerMm = 0.0;
    cv::Rect refRect;
    double bestArea = 0.0;
    for (const auto &c : contours) {
        const cv::Rect r = cv::boundingRect(c);
        const double area = r.area();
        if (area > bestArea) {
            bestArea = area;
            refRect = r;
        }
    }
    if (refRect.width > 0 && refRect.height > 0) {
        const int refPx = std::max(refRect.width, refRect.height);
        if (refPx > 10) {
            pxPerMm = static_cast<double>(refPx) / kRefBlockPhysicalMm;
            out.refLeft = refRect.x;
            out.refTop = refRect.y;
            out.refW = refRect.width;
            out.refH = refRect.height;
            out.pxPerMm = pxPerMm;
        }
    }

    // 2) 红色（RBC）各列密度 → 找分界 X
    cv::Mat redMask1, redMask2, redMask;
    cv::inRange(blurred,
                cv::Scalar(kRed1.lowH, kRed1.lowS, kRed1.lowV),
                cv::Scalar(kRed1.highH, kRed1.highS, kRed1.highV), redMask1);
    cv::inRange(blurred,
                cv::Scalar(kRed2.lowH, kRed2.lowS, kRed2.lowV),
                cv::Scalar(kRed2.highH, kRed2.highS, kRed2.highV), redMask2);
    cv::bitwise_or(redMask1, redMask2, redMask);

    const int width = landscapeBgr.cols;
    const int height = landscapeBgr.rows;
    // 只在管身中部采样，避开参照块与试管边缘。
    const int y0 = height / 5;
    const int y1 = height * 4 / 5;
    const int bandRows = y1 - y0;

    std::vector<int> colRed(width, 0);
    int maxRed = 1;
    for (int y = y0; y < y1; ++y) {
        const uchar *row = redMask.ptr<uchar>(y);
        for (int x = 0; x < width; ++x) {
            if (row[x]) {
                ++colRed[x];
                maxRed = std::max(maxRed, colRed[x]);
            }
        }
    }
    const double densityThreshold = 0.15 * maxRed;
    int packedLeft = -1, interfaceX = -1;
    for (int x = 0; x < width; ++x) {
        if (colRed[x] > densityThreshold) {
            if (packedLeft < 0) {
                packedLeft = x;
            }
            interfaceX = x;
        }
    }

    // 3) 红细胞高度
    if (interfaceX > packedLeft && packedLeft >= 0 && pxPerMm > 0.0) {
        const int packedPx = interfaceX - packedLeft;
        out.interfaceX = interfaceX;
        out.rbcHeightMm = static_cast<double>(packedPx) / pxPerMm;
        out.ok = true;
    } else {
        out.state = QStringLiteral("InvalidRegion");
        return out;
    }

    // 4) 溶血判定：分析分界右侧血浆区域里的红色比例
    const int startX = std::max(0, interfaceX + interfaceX / 5);
    const int plasmaW = std::max(8, width - startX);
    const int regW = std::max(8, plasmaW / 2);
    if (startX + regW <= width) {
        const cv::Rect plasma(startX, height / 5, regW, bandRows);
        const int total = plasma.width * plasma.height;
        int redPixels = 0;
        const cv::Mat band = redMask(plasma);
        for (int y = 0; y < band.rows; ++y) {
            const uchar *row = band.ptr<uchar>(y);
            for (int x = 0; x < band.cols; ++x) {
                redPixels += (row[x] ? 1 : 0);
            }
        }
        out.hemolysisIndex = total > 0
                ? static_cast<double>(redPixels) / static_cast<double>(total)
                : 0.0;
        out.state = (out.hemolysisIndex > 0.05)
                ? QStringLiteral("Hemolyzed") : QStringLiteral("Normal");
    } else {
        out.state = QStringLiteral("Normal");
    }

    return out;
}
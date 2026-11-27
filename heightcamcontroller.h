#ifndef HEIGHTCAMCONTROLLER_H
#define HEIGHTCAMCONTROLLER_H

#include <QObject>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QImage>

#include <opencv2/core.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>

#include <atomic>

/**
 * 测高相机采集层。
 *
 * 独立跑在一个 QThread 里（由 MainWindow 负责 moveToThread 并启动）。
 * 全血模式下开机即常开取流（33ms 一次 drainLiveFrame），测高上升沿由
 * 主线程调用 triggerCapture()，在相机线程里取一帧新图 → 竖图转横图 →
 * 存临时 jpg → 发 imageCaptured(path) 信号。
 *
 * 本质照搬老工程 opencvfindheigh，去掉 QsLog / cglobal / ini 依赖，
 * 日志改用 Qt 的 qInfo/qWarning。
 */
class HeightCamController : public QObject
{
    Q_OBJECT

public:
    explicit HeightCamController(QObject *parent = nullptr);
    ~HeightCamController() override;

    static const QString DEFAULT_IMAGE_SUBDIR;

    bool hasOpenedCamera() const { return m_cameraIndex >= 0; }
    QString imageDir() const { return m_imageDir; }

    /**
     * 枚举系统 DirectShow 视频输入设备，返回的列表下标与
     * OpenCV CAP_DSHOW 的设备索引一致（都按 DirectShow 枚举顺序）。
     * 设备名为空表示该索引存在但读不到 FriendlyName。
     */
    static QStringList cameraDeviceNames();

public slots:
    /** 线程启动一次：开相机 + 常开取流。 */
    void start();
    /** 打开/关闭相机常开取流（血浆模式关闭）。 */
    void setCameraPower(bool enable);
    /** 设置相机设备索引（默认 0），可在启动前调用。 */
    void setCameraIndex(int index);
    /**
     * 按设备名称选择测高相机；name 为空时退回按 fallbackIndex 打开。
     * 设置了名称但枚举不到同名设备时拒绝打开其它摄像头，
     * 避免电脑内置摄像头被误当成仪器摄像头。
     */
    void setCameraDevice(const QString &name, int fallbackIndex);
    /** 主板上测高上升沿调用：拍照→转横图→存 jpg→发 imageCaptured。 */
    void triggerCapture();
    /** 实时预览开关：全血开，血浆关。 */
    void setPreviewEnabled(bool enable);

signals:
    /** 拍照成功：返回存放的 jpg 路径。 */
    void imageCaptured(const QString &imagePath);
    /** 写真失败：errorType（配置/硬件/采集/保存）+ 原因。 */
    void captureFailed(const QString &errorType, const QString &errorMessage);
    /** 实时取流帧（约 30fps），血浆模式/松停止时不发。全血模式用于实时预览。 */
    void liveFrame(const QImage &frame);

private:
    bool createImageStorageDirectory();
    bool ensureCaptureOpen();
    /** 按设备名解析出当前 DirectShow 索引；找不到返回 -1。 */
    int resolveCameraIndex() const;
    void releaseCapture();
    void applyWindowsCameraLikeParams();
    void drainLiveFrame();
    quint64 hashFrame(const cv::Mat &frame) const;
    bool takeFreshFrame(cv::Mat &outFrame);
    void convertLiveToQImage(cv::Mat &frame);

    QString m_imageDir;
    QString m_cameraDevicePath;
    QString m_cameraDeviceName;
    int m_cameraIndex = 0;
    bool m_previewEnabled = false;
    std::atomic<bool> m_abort{false};
    std::atomic<bool> m_cameraWanted{false};
    cv::VideoCapture m_capture;
    QTimer *m_liveTimer = nullptr;
    QMutex m_frameMutex;
    cv::Mat m_liveFrame;
    quint64 m_lastSavedHash = 0;
};

#endif // HEIGHTCAMCONTROLLER_H
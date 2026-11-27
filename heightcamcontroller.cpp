#include "heightcamcontroller.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QThread>

#include <opencv2/imgproc.hpp>

#include <QDebug>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <dshow.h>
#endif

namespace {

/** 相机竖装时把竖图转成横图：参照物在上、血球在左、血浆在右。 */
cv::Mat toLandscapeFrame(const cv::Mat &frame)
{
    if (frame.empty() || frame.rows <= frame.cols) {
        return frame;
    }
    cv::Mat landscape;
    cv::transpose(frame, landscape);
    cv::flip(landscape, landscape, 1);
    return landscape;
}

} // namespace

// 老工程 opencvfindheigh 的 DEFAULT_IMAGE_SUBDIR
const QString HeightCamController::DEFAULT_IMAGE_SUBDIR = "FindHighImage";

HeightCamController::HeightCamController(QObject *parent) : QObject(parent)
    , m_liveTimer(nullptr)
    , m_lastSavedHash(0)
{
    m_cameraWanted.store(false);
    createImageStorageDirectory();
}

HeightCamController::~HeightCamController()
{
    m_abort.store(true);
    m_cameraWanted.store(false);
    if (m_liveTimer) {
        m_liveTimer->stop();
    }
    releaseCapture();
    qInfo() << "[HeightCam] 退出测高相机实例";
}

QStringList HeightCamController::cameraDeviceNames()
{
    QStringList names;
#ifdef Q_OS_WIN
    const HRESULT initResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = SUCCEEDED(initResult);
    if (FAILED(initResult) && initResult != RPC_E_CHANGED_MODE) {
        qWarning() << "[HeightCam] COM 初始化失败:" << initResult;
        return names;
    }

    ICreateDevEnum *deviceEnum = nullptr;
    const HRESULT createResult = CoCreateInstance(
                CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
                IID_ICreateDevEnum,
                reinterpret_cast<void **>(&deviceEnum));
    if (SUCCEEDED(createResult) && deviceEnum) {
        IEnumMoniker *monikerEnum = nullptr;
        if (deviceEnum->CreateClassEnumerator(
                    CLSID_VideoInputDeviceCategory, &monikerEnum, 0) == S_OK
                && monikerEnum) {
            IMoniker *moniker = nullptr;
            while (monikerEnum->Next(1, &moniker, nullptr) == S_OK
                   && moniker) {
                QString name;
                IPropertyBag *bag = nullptr;
                if (SUCCEEDED(moniker->BindToStorage(
                                  nullptr, nullptr, IID_IPropertyBag,
                                  reinterpret_cast<void **>(&bag)))
                        && bag) {
                    VARIANT value;
                    VariantInit(&value);
                    if (SUCCEEDED(bag->Read(L"FriendlyName", &value,
                                            nullptr))
                            && value.vt == VT_BSTR) {
                        name = QString::fromWCharArray(value.bstrVal);
                    }
                    VariantClear(&value);
                    bag->Release();
                }
                // 名称读不到也要占一个下标，保证与 OpenCV 的索引对齐。
                names.append(name);
                moniker->Release();
            }
            monikerEnum->Release();
        }
        deviceEnum->Release();
    }
    if (shouldUninitialize) {
        CoUninitialize();
    }
#endif
    return names;
}

int HeightCamController::resolveCameraIndex() const
{
    if (m_cameraDeviceName.isEmpty()) {
        return m_cameraIndex;
    }
    const QStringList names = cameraDeviceNames();
    for (int i = 0; i < names.size(); ++i) {
        if (names.at(i).compare(m_cameraDeviceName, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    qWarning() << "[HeightCam] 未找到配置的测高相机:"
               << m_cameraDeviceName << " 当前设备:" << names;
    return -1;
}

bool HeightCamController::createImageStorageDirectory()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    if (appDir.isEmpty()) {
        qWarning() << "[HeightCam] 无法获取应用程序路径";
        return false;
    }
    m_imageDir = QDir::cleanPath(appDir + QDir::separator()
                                 + DEFAULT_IMAGE_SUBDIR);
    QFileInfo appDirInfo(appDir);
    if (!appDirInfo.isWritable()) {
        qWarning() << "[HeightCam] 应用程序目录无写入权限:"
                   << QDir::toNativeSeparators(appDir);
        return false;
    }
    QDir dir;
    if (dir.exists(m_imageDir)) {
        return true;
    }
    if (!dir.mkpath(m_imageDir)) {
        qWarning() << "[HeightCam] 创建测高图片文件夹失败:" << m_imageDir;
        m_imageDir.clear();
        return false;
    }
    qInfo() << "[HeightCam] 测高图片文件夹:" << m_imageDir;
    return true;
}

quint64 HeightCamController::hashFrame(const cv::Mat &frame) const
{
    if (frame.empty() || !frame.data) {
        return 0;
    }
    quint64 h = (static_cast<quint64>(frame.cols) << 32)
            | static_cast<quint64>(frame.rows);
    const int channels = frame.channels();
    const int step = qMax(1, (frame.rows * frame.cols * channels) / 2500);
    for (int y = 0; y < frame.rows; y += 4) {
        const uchar *row = frame.ptr<uchar>(y);
        const int rowBytes = frame.cols * channels;
        for (int i = 0; i < rowBytes; i += step) {
            h = h * 131ull + row[i];
        }
    }
    return h;
}

void HeightCamController::convertLiveToQImage(cv::Mat &frame)
{
    if (frame.empty() || frame.channels() != 3) {
        return;
    }
    cv::Mat rgb;
    cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);
    QImage img(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step),
               QImage::Format_RGB888);
    emit liveFrame(img.copy());
}

void HeightCamController::drainLiveFrame()
{
    if (m_abort.load() || !m_cameraWanted.load()) {
        return;
    }
    if (!m_capture.isOpened()) {
        return;
    }
    cv::Mat f;
    if (!m_capture.read(f) || f.empty()) {
        return;
    }
    QMutexLocker locker(&m_frameMutex);
    m_liveFrame = f;
    // 实时预览：与拍照一致先转成横图再发 RGB，避免竖图在宽面板里缩得很小。
    if (m_previewEnabled) {
        const cv::Mat preview = toLandscapeFrame(f);
        cv::Mat rgb;
        cv::cvtColor(preview, rgb, cv::COLOR_BGR2RGB);
        QImage img(rgb.data, rgb.cols, rgb.rows,
                   static_cast<int>(rgb.step), QImage::Format_RGB888);
        emit liveFrame(img.copy());
    }
}

void HeightCamController::setCameraPower(bool enable)
{
    m_cameraWanted.store(enable);
    if (!enable) {
        if (m_liveTimer) {
            m_liveTimer->stop();
        }
        releaseCapture();
        {
            QMutexLocker locker(&m_frameMutex);
            m_liveFrame.release();
        }
        m_lastSavedHash = 0;
        qInfo() << "[HeightCam] 血浆模式，摄像头已关闭";
        return;
    }

    if (!m_liveTimer) {
        m_liveTimer = new QTimer(this);
        m_liveTimer->setInterval(33);
        connect(m_liveTimer, &QTimer::timeout,
                this, &HeightCamController::drainLiveFrame);
    }
    if (!ensureCaptureOpen()) {
        qWarning() << "[HeightCam] 全血模式打开摄像头失败";
        return;
    }
    m_liveTimer->start();
    drainLiveFrame();
    qInfo() << "[HeightCam] 全血模式，摄像头已打开并持续取流";
}

void HeightCamController::setCameraIndex(int index)
{
    m_cameraIndex = index;
    m_cameraDevicePath = QString::number(index);
}

void HeightCamController::setCameraDevice(const QString &name,
                                          int fallbackIndex)
{
    m_cameraDeviceName = name.trimmed();
    setCameraIndex(fallbackIndex);
}

void HeightCamController::setPreviewEnabled(bool enable)
{
    m_previewEnabled = enable;
    if (enable) {
        drainLiveFrame();   // 立即推一帧
    } else {
        m_liveFrame.release();
    }
}

void HeightCamController::start()
{
    qInfo() << "[HeightCam] 启动测高线程:" << QThread::currentThreadId();
    if (!m_liveTimer) {
        m_liveTimer = new QTimer(this);
        m_liveTimer->setInterval(33);
        connect(m_liveTimer, &QTimer::timeout,
                this, &HeightCamController::drainLiveFrame);
    }
    if (m_cameraWanted.load()) {
        setCameraPower(true);
    }
}

void HeightCamController::releaseCapture()
{
    if (m_capture.isOpened()) {
        m_capture.release();
        qInfo() << "[HeightCam] 摄像头资源已释放";
    }
}

bool HeightCamController::ensureCaptureOpen()
{
    if (m_capture.isOpened()) {
        return true;
    }
    if (m_cameraIndex < 0) {
        return false;
    }

    // 按名称选择时用 DirectShow：设备列表和 CAP_DSHOW 的索引都来自
    // DirectShow 枚举，能保证打开的就是选中的那台；MSMF 的枚举顺序
    // 与之不同，只作为兜底。
    const int cameraIndex = resolveCameraIndex();
    if (cameraIndex < 0) {
        qWarning() << "[HeightCam] 配置的测高相机未连接，拒绝打开其它摄像头";
        return false;
    }
    const bool byName = !m_cameraDeviceName.isEmpty();

    const int MAX_CAMERA_RETRY = 3;
    const int RETRY_DELAY_MS = 100;
    qputenv("OPENCV_VIDEOIO_MSMF_ENABLE_HW_TRANSFORMS", "0");
    const int backends[] = {
        byName ? cv::CAP_DSHOW : cv::CAP_MSMF,
        byName ? cv::CAP_MSMF : cv::CAP_DSHOW
    };
    for (int retry = 0; retry < MAX_CAMERA_RETRY; ++retry) {
        if (m_abort.load()) {
            releaseCapture();
            return false;
        }
        for (int b = 0; b < 2; ++b) {
            m_capture.open(cameraIndex, backends[b]);
            if (!m_capture.isOpened()) {
                qInfo() << "[HeightCam] 打开失败 后端=" << b
                        << " 索引=" << cameraIndex
                        << " 设备:" << m_cameraDeviceName;
                continue;
            }
            applyWindowsCameraLikeParams();
            qInfo() << "[HeightCam] 摄像头打开成功 索引:" << cameraIndex
                    << " 设备:" << m_cameraDeviceName
                    << " 后端:" << backends[b];
            return true;
        }
        qInfo() << "[HeightCam] 打开摄像头失败，重试:" << retry + 1
                << "/" << MAX_CAMERA_RETRY;
        QThread::msleep(RETRY_DELAY_MS);
    }
    return false;
}

void HeightCamController::applyWindowsCameraLikeParams()
{
    const int captureW = 640;
    const int captureH = 480;
    const int curW = qRound(m_capture.get(cv::CAP_PROP_FRAME_WIDTH));
    const int curH = qRound(m_capture.get(cv::CAP_PROP_FRAME_HEIGHT));
    if (curW != captureW || curH != captureH) {
        m_capture.set(cv::CAP_PROP_FRAME_WIDTH, captureW);
        m_capture.set(cv::CAP_PROP_FRAME_HEIGHT, captureH);
    }
    m_capture.set(cv::CAP_PROP_FPS, 30);
    m_capture.set(cv::CAP_PROP_CONVERT_RGB, 1);
    m_capture.set(cv::CAP_PROP_BUFFERSIZE, 1);

    cv::Mat kick;
    m_capture.read(kick);
    m_capture.set(cv::CAP_PROP_AUTO_EXPOSURE, 1);
    m_capture.set(cv::CAP_PROP_AUTO_EXPOSURE, 0.75);
}

bool HeightCamController::takeFreshFrame(cv::Mat &outFrame)
{
    outFrame.release();
    QElapsedTimer wait;
    wait.start();
    quint64 h = 0;
    while (wait.elapsed() < 450) {
        drainLiveFrame();
        {
            QMutexLocker locker(&m_frameMutex);
            if (!m_liveFrame.empty()) {
                outFrame = m_liveFrame.clone();
            }
        }
        h = hashFrame(outFrame);
        if (!outFrame.empty() && h != 0 && h != m_lastSavedHash) {
            m_lastSavedHash = h;
            return true;
        }
        QThread::msleep(20);
    }
    if (!outFrame.empty()) {
        m_lastSavedHash = h;
        qWarning() << "[HeightCam] 取流超时，使用当前帧 hash=" << h;
        return true;
    }
    return false;
}

void HeightCamController::triggerCapture()
{
    m_abort.store(false);

    if (!m_cameraWanted.load()) {
        emit captureFailed(QStringLiteral("模式限制"),
                                  QStringLiteral("血浆模式不支持测高"));
        return;
    }
    if (!ensureCaptureOpen()) {
        qWarning() << "[HeightCam] 摄像头最终打开失败，索引:" << m_cameraIndex;
        if (m_cameraIndex < 0) {
            emit captureFailed(QStringLiteral("配置错误"),
                              QStringLiteral("摄像头索引无效"));
        } else {
            emit captureFailed(QStringLiteral("硬件错误"),
                              QStringLiteral("无法打开摄像头设备"));
        }
        return;
    }
    if (m_liveTimer && !m_liveTimer->isActive()) {
        m_liveTimer->start();
    }

    cv::Mat frame;
    if (!takeFreshFrame(frame)) {
        qWarning() << "[HeightCam] 无法获取有效帧";
        emit captureFailed(QStringLiteral("采集错误"),
                          QStringLiteral("摄像头画面获取超时"));
        return;
    }
    if (m_abort.load()) {
        return;
    }
    if (frame.cols <= 0 || frame.rows <= 0) {
        qWarning() << "[HeightCam] 获取到无效尺寸的帧";
        emit captureFailed(QStringLiteral("采集错误"),
                          QStringLiteral("摄像头返回无效图像尺寸"));
        return;
    }

    // 竖图转横图：参照物在上、血球在左、血浆在右。
    const cv::Mat rotatedFrame = toLandscapeFrame(frame);
    if (rotatedFrame.empty() || rotatedFrame.channels() != 3) {
        qWarning() << "[HeightCam] 无效的输出图像通道:"
                   << (rotatedFrame.empty() ? 0 : rotatedFrame.channels());
        emit captureFailed(QStringLiteral("数据异常"),
                          QStringLiteral("图像格式不正确"));
        return;
    }

    if (m_imageDir.isEmpty()) {
        qWarning() << "[HeightCam] 图片目录为空";
        emit captureFailed(QStringLiteral("保存错误"),
                          QStringLiteral("图片目录未初始化"));
        return;
    }
    const QString outPath = QDir(m_imageDir).filePath(
                QStringLiteral("opencvPRP.jpg"));
    std::string outpath = outPath.toLocal8Bit().toStdString();
    if (!cv::imwrite(outpath, rotatedFrame)) {
        qWarning() << "[HeightCam] 图像保存失败" << outPath;
        if (!m_abort.load()) {
            emit captureFailed(QStringLiteral("保存错误"),
                              QStringLiteral("图像保存失败"));
        }
        return;
    }
    qInfo() << "[HeightCam] 已保存" << outPath
            << rotatedFrame.cols << "x" << rotatedFrame.rows;
    if (!m_abort.load()) {
        emit imageCaptured(outPath);
    }
}
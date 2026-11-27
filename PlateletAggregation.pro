QT += core gui widgets serialport sql

CONFIG += c++14

TEMPLATE = app
TARGET = PlateletAggregation

DEFINES += QT_DEPRECATED_WARNINGS
DEFINES += QT_MESSAGELOGCONTEXT

RC_ICONS = img/appicon.ico

RESOURCES += PlateletAggregation.qrc

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    consumablesdialog.cpp \
    settingsdialog.cpp \
    modulecontrolwidget.cpp \
    SerialPortManager.cpp \
    ModuleAdPoller.cpp \
    mainboardpoller.cpp \
    mainboardstatusicons.cpp \
    CommunicationWorker.cpp \
    CustomTitleBar.cpp \
    FramelessDialog.cpp \
    MedicalMessageBox.cpp \
    FramelessWindowSupport.cpp \
    DetectionModule.cpp \
    modulerackwidget.cpp \
    moduleoverviewwidget.cpp \
    reagentbottlepanel.cpp \
    sampletraymodel.cpp \
    tubetraydialog.cpp \
    testdatastore.cpp \
    testsamplequeue.cpp \
    testcurvewidget.cpp \
    testcurvedialog.cpp \
    testsession.cpp \
    sampledetailpanel.cpp \
    resultstablewidget.cpp \
    resultsinfowidget.cpp \
    querywidget.cpp \
    curvedisplaywidget.cpp \
    instrumentcoordinates.cpp \
    coordinatefileio.cpp \
    coordinatemaintenancewidget.cpp \
    applogger.cpp \
    motionprotocol.cpp \
    motioncontroller.cpp \
    coordinateverificationcontroller.cpp \
    perfusioncontroller.cpp \
    cleaningcontroller.cpp \
    resetcontroller.cpp \
    pumppositiontracker.cpp \
    medicalwidgets.cpp \
    testscheduler.cpp \
    testactioncontroller.cpp \
    addsamplewidget.cpp \
    heightcamcontroller.cpp \
    heightrecognizer.cpp

HEADERS += \
    mainwindow.h \
    consumablesdialog.h \
    settingsdialog.h \
    modulecontrolwidget.h \
    modulerackwidget.h \
    moduleoverviewwidget.h \
    channelrunstate.h \
    reagentbottlepanel.h \
    sampletraymodel.h \
    tubetraydialog.h \
    testdatastore.h \
    testsamplequeue.h \
    testscheduler.h \
    testactioncontroller.h \
    instrumentparameters.h \
    referenceinterval.h \
    informationentry.h \
    testcurvewidget.h \
    testcurvedialog.h \
    testsession.h \
    sampledetailpanel.h \
    resultstablewidget.h \
    resultsinfowidget.h \
    querywidget.h \
    curvedisplaywidget.h \
    instrumentcoordinates.h \
    coordinatefileio.h \
    coordinatemaintenancewidget.h \
    applogger.h \
    motionprotocol.h \
    motioncontroller.h \
    coordinateverificationcontroller.h \
    perfusioncontroller.h \
    cleaningcontroller.h \
    resetcontroller.h \
    pumppositiontracker.h \
    medicalwidgets.h \
    addsamplewidget.h \
    heightcamcontroller.h \
    heightrecognizer.h \
    SerialPortManager.h \
    ModuleAdPoller.h \
    mainboardpoller.h \
    mainboardstatus.h \
    mainboardconsumables.h \
    mainboardstatusicons.h \
    CommunicationWorker.h \
    MachineModel.h \
    appconfig.h \
    AppIcon.h \
    CustomTitleBar.h \
    FramelessDialog.h \
    MedicalMessageBox.h \
    FramelessWindowSupport.h \
    DetectionModule.h

# ===== 测高 OpenCV（32 位 MinGW，来自 C:/opencv3.4.1/OpencvBuildLib）=====
INCLUDEPATH += C:/opencv3.4.1/OpencvBuildLib/include
LIBS += -LC:/opencv3.4.1/OpencvBuildLib/lib \
    -lopencv_core453 \
    -lopencv_imgproc453 \
    -lopencv_videoio453 \
    -lopencv_highgui453 \
    -lopencv_imgcodecs453

# 测高相机按名称选择：DirectShow 枚举（ICreateDevEnum）
win32: LIBS += -lole32 -loleaut32 -luuid -lstrmiids

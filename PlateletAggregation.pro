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
    addsamplewidget.cpp

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

QT       += core gui network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    CrasterParabolicHeatmapWidget.cpp \
    MollweideHeatmapWidget.cpp \
    PeerConnection.cpp \
    QFlowLayout.cpp \
    TcpAgentServer.cpp \
    clientpeerswindow.cpp \
    commandadapter.cpp \
    commhelper.cpp \
    countratestatisticswindow.cpp \
    curveFit.cpp \
    dataprocessor.cpp \
    detsettingwindow.cpp \
    energycalibration.cpp \
    globalsettings.cpp \
    localsettingwindow.cpp \
    main.cpp \
    mainwindow.cpp \
    neutronyieldcalibration.cpp \
    neutronyieldstatisticswindow.cpp \
    otaupgradewindow.cpp \
    parsedata.cpp \
    particalwindow.cpp \
    qcomboboxdelegate.cpp \
    qhuaweiswitcherhelper.cpp \
    switchbutton.cpp \
    sysutils.cpp \
    waveformwindow.cpp

HEADERS += \
    CrasterParabolicHeatmapWidget.h \
    MollweideHeatmapWidget.h \
    PeerConnection.h \
    QFlowLayout.h \
    TcpAgentServer.h \
    clientpeerswindow.h \
    commandadapter.h \
    countratestatisticswindow.h \
    curveFit.h \
    dataprocessor.h \
    detsettingwindow.h \
    energycalibration.h \
    localsettingwindow.h \
    neutronyieldcalibration.h \
    neutronyieldstatisticswindow.h \
    otaupgradewindow.h \
    parsedata.h \
    particalwindow.h \
    qcomboboxdelegate.h \
    qhuaweiswitcherhelper.h \
    qlitethread.h \
    commhelper.h \
    globalsettings.h \
    mainwindow.h \
    switchbutton.h \
    sysutils.h \
    waveformwindow.h

FORMS += \
    clientpeerswindow.ui \
    countratestatisticswindow.ui \
    detsettingwindow.ui \
    energycalibration.ui \
    localsettingwindow.ui \
    mainwindow.ui \
    neutronyieldcalibration.ui \
    neutronyieldstatisticswindow.ui \
    otaupgradewindow.ui \
    particalwindow.ui \
    waveformwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target


DESTDIR = $$PWD/../build_Zr_ActivationPro
contains(QT_ARCH, x86_64) {
    # x64
    DESTDIR = $$DESTDIR/x64
} else {
    # x86
    DESTDIR = $$DESTDIR/x86
}

DESTDIR = $$DESTDIR/qt$$QT_VERSION/
message(DESTDIR = $$DESTDIR)

#开启工程的debug和release两种版本构建
#CONFIG += debug_and_release
#避免创建空的debug和release目录
CONFIG -= debug_and_release
CONFIG(debug, debug|release) {
    TARGET = Zr_ActivationProd
} else {
    TARGET = Zr_ActivationPro
}

#指定编译产生的文件分门别类放到对应目录
MOC_DIR     = temp/moc
RCC_DIR     = temp/rcc
UI_DIR      = temp/ui
OBJECTS_DIR = temp/obj

#把所有警告都关掉眼不见为净
CONFIG += warn_off

#开启大资源支持
CONFIG += resources_big

#############################################################################################################
exists (./.git) {
    GIT_BRANCH   = $$system(git rev-parse --abbrev-ref HEAD)
    GIT_DATE = $$system(git show --oneline --format=\"%ci\" -s HEAD)
    GIT_HASH     = $$system(git show --oneline --format=\"%H\" -s HEAD)
    GIT_VERSION = "Git: $${GIT_BRANCH}: $${GIT_DATE} $${GIT_HASH}"
} else {
    GIT_BRANCH      = None
    GIT_DATE        = None
    GIT_HASH        = None
    GIT_VERSION     = None
}

DEFINES += GIT_BRANCH=\"\\\"$$GIT_BRANCH\\\"\"
DEFINES += GIT_DATE=\"\\\"$$GIT_DATE\\\"\"
DEFINES += GIT_HASH=\"\\\"$$GIT_HASH\\\"\"
DEFINES += GIT_VERSION=\"\\\"$$GIT_VERSION\\\"\"
DEFINES += APP_VERSION="\\\"V2.1.1\\\""

windows {
    # MinGW
    *-g++* {
        QMAKE_CXXFLAGS += -Wall -Wextra -Wpedantic
        #QMAKE_CXXFLAGS += -finput-charset=UTF-8
        #QMAKE_CXXFLAGS += -fexec-charset=UTF-8
        #QMAKE_CXXFLAGS += -fwide-exec-charset=UTF-16
        #设置wchar_t类型数据的编码格式。不同主机值可能不同，编译器运行时根据主机情况会自动识别出最符合
        #主机的方案作为默认值，这个参数是不需要动的。UTF-16 UTF-16BE UTF-16LE UTF-32LE UTF-32BE
    }
    # MSVC
    *-msvc* {
        QMAKE_CXXFLAGS += /utf-8
        QMAKE_CXXFLAGS += /source-charset:utf-8
        QMAKE_CXXFLAGS += /execution-charset:utf-8
    }
}
#QMAKE_MANIFEST += $$PWD/manifest.xml

include($$PWD/../3rdParty/log4qt/Include/log4qt.pri)
include($$PWD/../3rdParty/resource/resource.pri)
include($$PWD/../3rdParty/QCustomplot/QCustomplot.pri)
#include($$PWD/../3rdParty/QFontIcon/QFontIcon.pri)
include($$PWD/../3rdParty/QGoodWindow/QGoodWindow/QGoodWindow.pri)
include($$PWD/../3rdParty/QGoodWindow/QGoodWindowHelper/QGoodWindowHelper.pri)
include($$PWD/../3rdParty/QGoodWindow/QGoodCentralWidget/QGoodCentralWidget.pri)
include($$PWD/../3rdParty/hdf5/C++/hdf5Wrapper.pri)
include($$PWD/../3rdParty/alglib-cpp/alglib.pri)
INCLUDEPATH += $$PWD/../3rdParty/eigen-5.0.0

# 添加config配置
# CONFIG += precompile_header
# 指定要使用的预编译头文件
# PRECOMPILED_HEADER += stable.h

LIBS += -lws2_32

# 启用libhv网络库
DEFINES += ENABLE_LIBHV=1
win32 {
    # libhv
    contains(DEFINES, ENABLE_LIBHV=1) {
        message("Including libhv library")
        INCLUDEPATH += $$PWD/../3rdParty/libhv/include
        LIBS += -L$$PWD/../3rdParty/libhv/lib -lhv
        LIBS += -lws2_32
    }

    contains(DEFINES, USE_QTELNET=1) {
        message("Including telnet library")
        # include($$PWD/../3rdParty/QNtpClient/QNtpClient.pri) /*时钟同步服务器*/
        include($$PWD/../3rdParty/QTelnet/QTelnet.pri)
    }
}

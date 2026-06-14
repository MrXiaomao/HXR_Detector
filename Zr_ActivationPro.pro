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
    crashhandler.cpp \
    detparamlogger.cpp \
    useractionlogger.cpp \
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
    crashhandler.h \
    detparamlogger.h \
    useractionlogger.h \
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
    DEFINES += QT_MESSAGELOGCONTEXT #定义之后Release版qWarning()输出的消息才能够捕获到
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

# git 日期含空格时不能直接进 -D，否则 MSVC/clangd 会拆成多个宏（问题项里 Expected ')'）
GIT_DATE ~= s/ /_/g
GIT_DATE ~= s/:/-/g
GIT_DATE ~= s/+//g
GIT_VERSION ~= s/ /_/g
GIT_VERSION ~= s/:/-/g
GIT_VERSION ~= s/+//g

DEFINES += GIT_BRANCH=\"\\\"$$GIT_BRANCH\\\"\"
DEFINES += GIT_DATE=\"\\\"$$GIT_DATE\\\"\"
DEFINES += GIT_HASH=\"\\\"$$GIT_HASH\\\"\"
DEFINES += GIT_VERSION=\"\\\"$$GIT_VERSION\\\"\"
DEFINES += APP_VERSION="\\\"V2.1.1\\\""

windows {
    # MinGW
    *-g++* {
        QMAKE_CXXFLAGS += -Wall -Wextra -Wpedantic
        # 大对象/大翻译单元：避免链接与编译阶段内存峰值过高
        QMAKE_CXXFLAGS += -Wa,-mbig-obj
        # Debug 下减少调试信息生成，缓解 cc1plus.exe out of memory
        QMAKE_CXXFLAGS_DEBUG += -fno-var-tracking -fno-var-tracking-assignments
        #QMAKE_CXXFLAGS += -finput-charset=UTF-8
        #QMAKE_CXXFLAGS += -fexec-charset=UTF-8
        #QMAKE_CXXFLAGS += -fwide-exec-charset=UTF-16
        #设置wchar_t类型数据的编码格式。不同主机值可能不同，编译器运行时根据主机情况会自动识别出最符合
        #主机的方案作为默认值，这个参数是不需要动的。UTF-16 UTF-16BE UTF-16LE UTF-32LE UTF-32BE
    }
    # MSVC（/utf-8 已含源/执行字符集，勿再叠加 /source-charset 会 D8016）
    *-msvc* {
        QMAKE_CXXFLAGS += /utf-8
        DEFINES += _USE_MATH_DEFINES
        # HDF5 C++ 包装编入 exe，见 3rdParty/hdf5/C++/include/H5api_adpt.h
        DEFINES += HDF5_CPP_EMBEDDED_IN_APP
    }
}
#QMAKE_MANIFEST += $$PWD/manifest.xml

include($$PWD/../3rdParty/log4qt/Include/log4qt.pri)
include($$PWD/../3rdParty/resource/resource.pri)
include($$PWD/../3rdParty/QCustomplot/QCustomplot.pri)
include($$PWD/../3rdParty/QGoodWindow/QGoodWindowHelper/QGoodWindowHelper.pri)
include($$PWD/../3rdParty/hdf5/C++/hdf5Wrapper.pri)
include($$PWD/../3rdParty/alglib-cpp/alglib.pri)
INCLUDEPATH += $$PWD/../3rdParty/eigen-5.0.0

# 预编译头：降低 MinGW 编译 mainwindow.cpp 等大文件时的内存占用
#CONFIG += precompile_header
#PRECOMPILED_HEADER = stable.h

LIBS += -lws2_32
win32: LIBS += -ldbghelp

# 启用 libhv（MinGW 用 libhv.dll.a；MSVC 需 3rdParty/libhv/lib/hv.lib，否则回退 TcpAgentServer）
DEFINES += ENABLE_LIBHV=1
win32 {
    *-msvc*:!exists($$PWD/../3rdParty/libhv/lib/hv.lib) {
        DEFINES -= ENABLE_LIBHV=1
        !build_pass:message("MSVC: 未找到 hv.lib，ENABLE_LIBHV 已关闭，使用 Qt TcpAgentServer")
    }

    contains(DEFINES, ENABLE_LIBHV=1) {
        message("Including libhv library")
        INCLUDEPATH += $$PWD/../3rdParty/libhv/include
        LIBS += -L$$PWD/../3rdParty/libhv/lib -lhv
        *-g++* {
            # MinGW 导入库名 libhv.dll.a，-lhv 可解析
        }
        *-msvc* {
            LIBS += $$PWD/../3rdParty/libhv/lib/hv.lib
        }
        LIBS += -lws2_32
    }

    contains(DEFINES, USE_QTELNET=1) {
        message("Including telnet library")
        # include($$PWD/../3rdParty/QNtpClient/QNtpClient.pri) /*时钟同步服务器*/
        include($$PWD/../3rdParty/QTelnet/QTelnet.pri)
    }
}

# MSVC Release：生成 PDB（供 VS 分析 logs/crash_*.dmp 与现场栈符号）
# Qt Creator 选 Kit「Desktop Qt x.x.x MSVC20xx 64bit」+ Release 构建即可。
win32:*-msvc* {
    # 与 MinGW 产物分目录，避免 exe/pdb 混用导致 dump 符号错乱
    DESTDIR = $$DESTDIR/msvc

    !build_pass: message("MSVC build output (exe+pdb): $$DESTDIR")

    !CONFIG(debug, debug|release) {
        QMAKE_CFLAGS_RELEASE += /Zi
        QMAKE_CXXFLAGS_RELEASE += /Zi

        # 链接生成完整 PDB，与 exe 同目录（CrashHandler / Visual Studio 加载）
        QMAKE_LFLAGS_RELEASE += /DEBUG
        QMAKE_LFLAGS_RELEASE += /DEBUG:FULL
        QMAKE_LFLAGS_RELEASE += /INCREMENTAL:NO
        QMAKE_LFLAGS_RELEASE += "/PDB:$$shell_path($$DESTDIR)\\$${TARGET}.pdb"

        !build_pass: message("MSVC Release PDB: $$shell_path($$DESTDIR)\\$${TARGET}.pdb")
    }

    # HDF5 为 DLL，复制到 exe 输出目录（须换行或 & 分隔，勿连续 += 拼成一条 copy）
    HDF5_LIBDIR = $$shell_path($$PWD/../3rdParty/hdf5/C++/lib)
    HDF5_OUTDIR = $$shell_path($$DESTDIR)
    QMAKE_POST_LINK += $$QMAKE_COPY \"$$HDF5_LIBDIR\\hdf5.dll\" \"$$HDF5_OUTDIR\"
    QMAKE_POST_LINK += $$escape_expand(\\n\\t)$$QMAKE_COPY \"$$HDF5_LIBDIR\\zlib1.dll\" \"$$HDF5_OUTDIR\"
}

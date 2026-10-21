#include "mainwindow.h"
#include "globalsettings.h"
#include "crashhandler.h"
#include "commhelper.h"
// #include "commhelper.h"
#include "countratestatisticswindow.h"
#include "spectrumh5rebuildwindow.h"
#include "useractionlogger.h"
#include "lightstyle.h"
#include "darkstyle.h"
// #include "customcolorstyle.h"

#include <QApplication>
#include <QCoreApplication>
#include <QStyleFactory>
#include <QFileInfo>
#include <QDir>
#include <QSplashScreen>
#include <QScreen>
#include <QMessageBox>
#include <QTimer>

#include <cstdlib>

#include <log4qt/log4qt.h>
#include <log4qt/logger.h>
#include <log4qt/layout.h>
#include <log4qt/patternlayout.h>
#include <log4qt/consoleappender.h>
#include <log4qt/dailyfileappender.h>
#include <log4qt/logmanager.h>
#include <log4qt/propertyconfigurator.h>
#include <log4qt/loggerrepository.h>
#include <log4qt/fileappender.h>
#include <log4qt/writerappender.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringConverter>
#endif

QMainWindow *mMainWindow = nullptr;
QMutex mutexMsg;
QtMessageHandler system_default_message_handler = NULL;// 用来保存系统默认的输出接口
void AppMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString &msg)
{
    QMutexLocker locker(&mutexMsg);
    if (type == QtWarningMsg && context.file == nullptr && context.function == nullptr)
        return;// 主要用于过滤系统的警告信息

    if (type == QtFatalMsg) {
        CrashHandler::logQtFatal(msg, context.file, context.line, context.function);
        if (system_default_message_handler)
            system_default_message_handler(type, context, msg);
        std::abort();
    }

    // 退出阶段或 qFatal 递归时勿 invokeMethod（主窗口可能已析构，会再次触发 fatal）
    const bool shuttingDown = !QCoreApplication::instance()
            || QCoreApplication::instance()->closingDown();
    if (!shuttingDown && mMainWindow && type != QtDebugMsg
        && !UserActionLogger::shouldExcludeFromUiLog(msg)) {
        QMetaObject::invokeMethod(mMainWindow, "sigWriteLog", Qt::QueuedConnection,
                                  Q_ARG(QString, msg), Q_ARG(QtMsgType, type));
    }

    //这里必须调用，否则消息被拦截，log4qt无法捕获系统日志
    if (system_default_message_handler){
        system_default_message_handler(type, context, msg);
    }
}

static QString resolveAppModuleKey(const QStringList &args)
{
    if (args.contains(QStringLiteral("-m")) && args.contains(QStringLiteral("countRateStatistics")))
        return QStringLiteral("countRateStatistics");
    if (args.contains(QStringLiteral("-m")) && args.contains(QStringLiteral("spectrumH5Rebuild")))
        return QStringLiteral("spectrumH5Rebuild");
    return QStringLiteral("main");
}

static QString exitLogMessage(const QString &moduleKey)
{
    if (moduleKey == QStringLiteral("countRateStatistics"))
        return QObject::tr("计数率统计模块退出");
    if (moduleKey == QStringLiteral("spectrumH5Rebuild"))
        return QObject::tr("能谱H5重建模块退出");
    return QObject::tr("主程序退出");
}

static void setLog4QtAppenderUtf8Encoding(Log4Qt::WriterAppender *appender)
{
    if (!appender)
        return;
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    appender->setEncoding(QTextCodec::codecForName("UTF-8"));
#else
    appender->setEncoding(QStringConverter::Encoding::Utf8);
#endif
}

static void applyUtf8EncodingToAllLog4QtAppenders()
{
    const QList<Log4Qt::AppenderSharedPtr> appenders = Log4Qt::Logger::rootLogger()->appenders();
    for (const Log4Qt::AppenderSharedPtr &appender : appenders) {
        setLog4QtAppenderUtf8Encoding(qobject_cast<Log4Qt::WriterAppender *>(appender.data()));
    }
}

#include <locale.h>
#include <QTextCodec>
#include <QTranslator>
#include <QLibraryInfo>
static QTranslator qtTranslator;
static QTranslator qtbaseTranslator;
static QTranslator appTranslator;
int main(int argc, char *argv[])
{
    QGoodWindow::setup();
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication a(argc, argv);

    CrashHandler::install(QStringLiteral("logs"));
    CrashHandler::setApplicationInfo(QStringLiteral(APP_VERSION),
                                     QStringLiteral(GIT_BRANCH),
                                     QStringLiteral(GIT_HASH),
                                     QStringLiteral(GIT_DATE));

    QApplication::setApplicationName("HXR分时能谱软件");
    QApplication::setOrganizationName("Copyright (c) 2025");
    QApplication::setOrganizationDomain("");
    QApplication::setApplicationVersion(APP_VERSION);
    QApplication::setStyle(QStyleFactory::create("fusion"));//WindowsVista fusion windows

    GlobalSettings settingsSystem;
    if(settingsSystem.value("Global/Options/enableNativeUI",false).toBool()) {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,false);
        QApplication::setAttribute(Qt::AA_DontUseNativeMenuBar,false);
        QApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings,false);
    }

    QFont font = QApplication::font();
#if defined(Q_OS_WIN) && defined(Q_CC_MSVC)
    int fontId = QFontDatabase::addApplicationFont(QApplication::applicationDirPath() + "/inziu-iosevkaCC-SC-regular.ttf");
#else
    int fontId = QFontDatabase::addApplicationFont(QStringLiteral(":/font/font/inziu-iosevkaCC-SC-regular.ttf"));
#endif
    QStringList fontFamilies = QFontDatabase::applicationFontFamilies(fontId);
    if (fontFamilies.size() > 0) {
        font.setFamily(fontFamilies[0]);//启用内置字体
    }

    int pointSize = font.pointSize();
    qreal dpi = QGuiApplication::primaryScreen()->devicePixelRatio();
    if (dpi >= 2.0)
        pointSize += 3;
    else if (dpi > 1.0)
        pointSize += 2;
    font.setPointSize(pointSize);
    font.setFixedPitch(true);
    qApp->setFont(font);
    qApp->setStyle(new DarkStyle());
    qApp->style()->setObjectName("fusion");

    settingsSystem.beginGroup("Version");
    settingsSystem.setValue("Version",GIT_VERSION);
    settingsSystem.endGroup();

    QSplashScreen splash;
    splash.setPixmap(QPixmap(":/splash.png"));
    splash.show();

    splash.showMessage(QObject::tr("启动中，请稍等..."), Qt::AlignLeft | Qt::AlignBottom, Qt::white);

    // 日志初始化前统一 locale 为 UTF-8，避免追加写入已有日志文件时使用系统 GBK 编码
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));

    // 启用新的日子记录类
    QString filename = QFileInfo(QCoreApplication::applicationFilePath()).baseName();
    QStringList args = QCoreApplication::arguments();
    const QString appModuleKey = resolveAppModuleKey(args);
    const QString appExitLogMessage = exitLogMessage(appModuleKey);
    if (args.contains("-m") && args.contains("offline")){
        filename += ".offline";
    }

    // 启用新的日子记录类
    QString sConfFilename = QString("./config/%1.log4qt.conf").arg(filename);
    if (QFileInfo::exists(sConfFilename)){
        Log4Qt::PropertyConfigurator::configure(sConfFilename);
    } else {
        Log4Qt::LogManager::setHandleQtMessages(true);
        Log4Qt::Logger *logger = Log4Qt::Logger::rootLogger();
        logger->setLevel(Log4Qt::Level::DEBUG_INT); //设置日志输出级别

        /****************PatternLayout配置日志的输出格式****************************/
        Log4Qt::PatternLayout *layout = new Log4Qt::PatternLayout();
        layout->setConversionPattern("%d{yyyy-MM-dd HH:mm:ss.zzz} [%p]: %m %n");
        layout->activateOptions();

        /***************************配置日志的输出位置***********/
        //输出到控制台
        Log4Qt::ConsoleAppender *consoleAppender = new Log4Qt::ConsoleAppender(layout, Log4Qt::ConsoleAppender::STDOUT_TARGET);
        setLog4QtAppenderUtf8Encoding(consoleAppender);
        consoleAppender->activateOptions();
        logger->addAppender(consoleAppender);

        //输出到文件(如果需要把离线处理单独保存日志文件，可以改这里)
        Log4Qt::DailyFileAppender *dailiAppender = new Log4Qt::DailyFileAppender(layout, "logs/.log", QString("%1_yyyy-MM-dd").arg(filename));
        dailiAppender->setAppendFile(true);
        dailiAppender->setImmediateFlush(true);
        setLog4QtAppenderUtf8Encoding(dailiAppender);
        dailiAppender->activateOptions();
        logger->addAppender(dailiAppender);
    }

    applyUtf8EncodingToAllLog4QtAppenders();

    // 确保logs目录存在
    QDir dir(QDir::currentPath() + "/logs");
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QObject::connect(&a, &QCoreApplication::aboutToQuit, &a, [appExitLogMessage] {
        qInfo().noquote() << appExitLogMessage;
        CommHelper::instance()->shutdown();
    }, Qt::DirectConnection);

    QString qlibpath = QLibraryInfo::location(QLibraryInfo::TranslationsPath);
    if(qtTranslator.load("qt_zh_CN.qm",qlibpath))
        qApp->installTranslator(&qtTranslator);
    if(qtbaseTranslator.load("qtbase_zh_CN.qm",qlibpath))
        qApp->installTranslator(&qtbaseTranslator);

    qRegisterMetaType<QtMsgType>("QtMsgType");
    qRegisterMetaType<FullSpectrum>("FullSpectrum");
    system_default_message_handler = qInstallMessageHandler(AppMessageHandler);
    QString darkTheme = "true";
    settingsSystem.beginGroup("Global/Startup");
    if(settingsSystem.contains("darkTheme"))
        darkTheme = settingsSystem.value("darkTheme").toString();
    settingsSystem.endGroup();

    bool isDarkTheme = true;
    if(darkTheme == "true") {
        isDarkTheme = true;
        QGoodWindow::setAppDarkTheme();
    } else {
        isDarkTheme = false;
        QGoodWindow::setAppLightTheme();
    }

    // QFontIcon::addFont(":/icons/icons/fontawesome-webfont-v6.6.0-solid-900.ttf");
    // QFontIcon::addFont(":/icons/icons/fontawesome-webfont-v6.6.0-brands-400.ttf");
    // QFontIcon::instance()->setColor(isDarkTheme?Qt::white:Qt::black);

    QGoodWindowHelper w;
    if (args.contains("-m") && args.contains("countRateStatistics")){
        QApplication::setApplicationName("HXR分时能谱软件离线数据分析-计数率统计");
        mMainWindow = new CountRateStatisticsWindow(isDarkTheme, &w);
    }
    else if (args.contains("-m") && args.contains("spectrumH5Rebuild")){
        QApplication::setApplicationName("HXR分时能谱软件离线数据分析-能谱H5重建");
        mMainWindow = new SpectrumH5RebuildWindow(isDarkTheme, &w);
    }
    else {
        QApplication::setApplicationName("HXR分时能谱软件");
        mMainWindow = new MainWindow(isDarkTheme, &w);
    }
    w.setupUiHelper(mMainWindow, isDarkTheme);

    qInfo().noquote() << QObject::tr("系统启动，模块：%1，程序目录：%2")
                             .arg(appModuleKey, QCoreApplication::applicationDirPath());
    qInfo().noquote() << QString("软件版本信息：%1, git:%2,%3").arg(GIT_BRANCH, GIT_HASH, GIT_DATE);
    CRASH_BREADCRUMB(QObject::tr("主界面已启动"));

    splash.finish(&w);

    QRect screenRect = QGuiApplication::primaryScreen()->availableGeometry();
    int x = (screenRect.width() - w.width()) / 2;
    int y = (screenRect.height() - w.height()) / 2;
    w.move(x, y);
    w.setWindowState(w.windowState() | Qt::WindowMaximized);
    w.show();

    // 主界面已显示后再启动看门狗，避免 exec() 前长时间初始化导致误报 GUI 卡死
    CrashHandler::startGuiWatchdog(8000);

    int ret = a.exec();

    //运行运行到这里，此时主窗体析构函数还没触发，所以shutdownRootLogger需要在主窗体销毁以后再做处理
    QObject::connect(&w, &QObject::destroyed, []{
        auto logger = Log4Qt::Logger::rootLogger();
        logger->removeAllAppenders();
        logger->loggerRepository()->shutdown();
    });

    return ret;
}

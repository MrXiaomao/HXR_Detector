#include "mainwindow.h"
#include "globalsettings.h"
// #include "commhelper.h"
#include "countratestatisticswindow.h"
#include "neutronyieldstatisticswindow.h"
#include "lightstyle.h"
#include "darkstyle.h"
// #include "customcolorstyle.h"

#include <QApplication>
#include <QStyleFactory>
#include <QFileInfo>
#include <QDir>
#include <QSplashScreen>
#include <QScreen>
#include <QMessageBox>
#include <QTimer>

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

QMainWindow *mMainWindow = nullptr;
QMutex mutexMsg;
QtMessageHandler system_default_message_handler = NULL;// 用来保存系统默认的输出接口
void AppMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString &msg)
{
    QMutexLocker locker(&mutexMsg);
    if (type == QtWarningMsg && context.file == nullptr && context.function == nullptr)
        return;// 主要用于过滤系统的警告信息

    if (mMainWindow && type != QtDebugMsg){
        //emit mw->sigWriteLog(msg, type);
        QMetaObject::invokeMethod(mMainWindow, "sigWriteLog", Qt::QueuedConnection, Q_ARG(QString, msg), Q_ARG(QtMsgType, type));
    }

    if (type == QtFatalMsg){
        return ;
    }

    //这里必须调用，否则消息被拦截，log4qt无法捕获系统日志
    if (system_default_message_handler){
        system_default_message_handler(type, context, msg);
    }
}

bool gMatlabInited = false;
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
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication::setAttribute(Qt::AA_DontUseNativeMenuBar);
    QApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication a(argc, argv);
    QApplication::setApplicationName("溴化镧配套软件");
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

    // 启用新的日子记录类
    QString filename = QFileInfo(QCoreApplication::applicationFilePath()).baseName();
    QStringList args = QCoreApplication::arguments();
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
        consoleAppender->activateOptions();
        consoleAppender->setEncoding(QTextCodec::codecForName("UTF-8"));
        logger->addAppender(consoleAppender);

        //输出到文件(如果需要把离线处理单独保存日志文件，可以改这里)        
        Log4Qt::DailyFileAppender *dailiAppender = new Log4Qt::DailyFileAppender(layout, "logs/.log", QString("%1_yyyy-MM-dd").arg(filename));
        dailiAppender->setAppendFile(true);
        dailiAppender->activateOptions();
        dailiAppender->setEncoding(QTextCodec::codecForName("UTF-8"));
        logger->addAppender(dailiAppender);
    }

    // 确保logs目录存在
    QDir dir(QDir::currentPath() + "/logs");
    if (!dir.exists()) {
        dir.mkpath(".");
    }

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

    QTextCodec::setCodecForLocale(QTextCodec::codecForMib(106));/* Utf8 */
    QGoodWindowHelper w;
    if (args.contains("-m") && args.contains("countRateStatistics")){
        QApplication::setApplicationName("溴化镧配套软件离线数据分析-计数率统计");
        mMainWindow = new CountRateStatisticsWindow(isDarkTheme, &w);
    }
    else if (args.contains("-m") && args.contains("neutronYieldStatistics")){
        QApplication::setApplicationName("溴化镧配套软件离线数据分析-中子产额统计");
        mMainWindow = new NeutronYieldStatisticsWindow(isDarkTheme, &w);
    }
    else
        mMainWindow = new MainWindow(isDarkTheme, &w);
    w.setupUiHelper(mMainWindow, isDarkTheme);

    qInfo().noquote() << QObject::tr("系统启动");
    qInfo().noquote() << QString("软件版本信息：%1, git:%2,%3").arg(GIT_BRANCH, GIT_HASH, GIT_DATE);

    splash.finish(&w);

    QRect screenRect = QGuiApplication::primaryScreen()->availableGeometry();
    int x = (screenRect.width() - w.width()) / 2;
    int y = (screenRect.height() - w.height()) / 2;
    w.move(x, y);
    w.setWindowState(w.windowState() | Qt::WindowMaximized);
    w.show();

    int ret = a.exec();

    //运行运行到这里，此时主窗体析构函数还没触发，所以shutdownRootLogger需要在主窗体销毁以后再做处理
    QObject::connect(&w, &QObject::destroyed, []{
        auto logger = Log4Qt::Logger::rootLogger();
        logger->removeAllAppenders();
        logger->loggerRepository()->shutdown();
    });

    return ret;
}

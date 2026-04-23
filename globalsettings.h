#ifndef GLOBALSETTINGS_H
#define GLOBALSETTINGS_H

#include <QObject>
#include <string.h>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QMutex>
#include <QReadWriteLock>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QElapsedTimer>

#include <cstring>      // 用于内存初始化（如memset）
#include <QtEndian> //qFromBigEndian需要
// 子能谱数据包信息
#pragma pack(push, 1)  // 确保字节对齐
struct SubSpectrumPacket {
    quint32 header;        // FFFF AAB1
    quint16 dataType;      // 00D2
    quint32 spectrumSeq;   // 能谱序号
    quint32 measureTime;   // 测量时间
    quint32 deathTime;     // 死时间
    quint16 spectrumSubNo; // 能谱编号
    quint32 spectrum[256]; // 能谱数据 256*32bit
    quint32 timeUTCs;      // utc整数秒
    quint32 timeUTCms;     // utc小数秒
    quint32 reserved;      // 保留位 0000 0000
    quint32 tail;          // FFFF CCD1

    // 构造函数初始化
    SubSpectrumPacket()
        : spectrumSeq(0),
        measureTime(0),
        deathTime(0),
        spectrumSubNo(0) {
        // 初始化能谱数组
        std::memset(spectrum, 0, sizeof(spectrum));
    }

    // 添加字节序转换成员函数
    // 字节序问题：x86 是小端序，网络数据通常是大端序
    void convertNetworkToHost() {        // 添加字节序转换成员函数
        // 处理字节序（Windows是小端序，网络数据通常是大端序）
        header = qFromBigEndian<quint32>(header);
        dataType = qFromBigEndian<quint16>(dataType);
        spectrumSeq = qFromBigEndian<quint32>(spectrumSeq);
        measureTime = qFromBigEndian<quint32>(measureTime);
        deathTime = qFromBigEndian<quint32>(deathTime);
        spectrumSubNo = qFromBigEndian<quint16>(spectrumSubNo);
        timeUTCs = qFromBigEndian<quint32>(timeUTCs);
        timeUTCms = qFromBigEndian<quint32>(timeUTCms);
        tail = qFromBigEndian<quint32>(tail);

        // 转换能谱数据数组
        for (int i = 0; i < 256; ++i) {
            spectrum[i] = qFromBigEndian<quint32>(spectrum[i]);
        }
    }
};
#pragma pack(pop)

// 用于描述探测器空间位置，及其测得的中子产额数值
struct Sensor {
    double lonDeg;  // 经度[-180,180]
    double latDeg;  // 纬度[-90, 90]
    double value;   // 中子产额
};

// 用于写入到HDF5文件的能谱数据
struct H5Spectrum{
    quint32 sequence;      // 能谱序号
    quint32 timeUTCs;      // utc整数秒
    quint32 timeUTCms;     // utc小数秒
    quint32 measureTime;   // 能量测量时间间隔,单位ms
    quint32 deathTime;      // 死时间,单位*10ns
    quint32 spectrum[8192]; // 8192道完整数据
};

// 用于解包时的完整能谱数据
#pragma pack(push, 1)  // 确保字节对齐
struct FullSpectrum {
    quint32 sequence;      // 能谱序号
    quint32 timeUTCs;      // utc整数秒
    quint32 timeUTCms;     // utc小数秒
    quint32 measureTime;   // 能量测量时间间隔,单位ms
    quint32 deathTime;      // 死时间,单位*10ns
    quint32 spectrum[8192]; // 8192道完整数据
    quint32 receivedMask = 0;   // 32个子包位图
    QDateTime completeTime; // 完成时间,记录一个完整能谱数据接收完的北京时间
    bool isComplete;       // 是否完整
    // QSet<quint16> receivedPackets; // 已收到的包编号
    
    // 默认构造函数
    FullSpectrum() : sequence(0), measureTime(0), deathTime(0), receivedMask(0), isComplete(false) {
        memset(spectrum, 0, sizeof(spectrum));
    }
    
    // 拷贝构造函数
    FullSpectrum(const FullSpectrum& other) 
        : sequence(other.sequence), measureTime(other.measureTime), deathTime(other.deathTime),
          receivedMask(other.receivedMask), completeTime(other.completeTime), isComplete(other.isComplete),
          timeUTCs(other.timeUTCs), timeUTCms(other.timeUTCms) {
        memcpy(spectrum, other.spectrum, sizeof(spectrum));
    }
    
    // 赋值运算符
    FullSpectrum& operator=(const FullSpectrum& other) {
        if (this != &other) {
            sequence = other.sequence;
            timeUTCs = other.timeUTCs;
            timeUTCms = other.timeUTCms;
            measureTime = other.measureTime;
            deathTime = other.deathTime;
            receivedMask = other.receivedMask;
            completeTime = other.completeTime;
            isComplete = other.isComplete;
            memcpy(spectrum, other.spectrum, sizeof(spectrum));
        }
        return *this;
    }

    // 添加字节序转换成员函数
    // 字节序问题：x86 是小端序，网络数据通常是大端序
    void convertNetworkToHost() {
        sequence = qFromBigEndian<quint32>(sequence);
        timeUTCs = qFromBigEndian<quint32>(timeUTCs );
        timeUTCms = qFromBigEndian<quint32>(timeUTCms);
        measureTime = qFromBigEndian<quint32>(measureTime);
        deathTime = qFromBigEndian<quint32>(deathTime);

        // 转换能谱数据数组
        for (int i = 0; i < 8192; ++i) {
            spectrum[i] = qFromBigEndian<quint32>(spectrum[i]);
        }
    }
};
#pragma pack(pop)

// 存放粒子信息的数据
struct ParticleData{
    QDateTime time;//粒子事件发生的时间
    quint16 energy; //粒子幅度
    quint16 typeFlag; //粒子类型标志（0：γ；1：α）
};

// 将FullSpectrum结构中的部分数据转化为H5Spectrum类型，用于存储。
H5Spectrum FullSpectoH5Spec(const FullSpectrum& src);

// 注册 H5Spectrum 为 Qt 元类型
Q_DECLARE_METATYPE(H5Spectrum)

#define SYSTEM_CONFIG_FILE "./Config/system_config.ini" //系统配置文件,保存UI界面等全局配置信息,用户不建议编辑
#define DEVICE_CONFIG_FILE "./Config/device_config.ini" //设备配置文件,保存设备相关的配置信息，如探测器参数等，用户可以编辑
class JsonSettings : public QObject{
    Q_OBJECT
public:
    JsonSettings(const QString &fileName) {
        QFileInfo mConfigurationFile;
        mConfigurationFile.setFile(fileName);
        mFileName = mConfigurationFile.absoluteFilePath();
        mOpened = this->load();

        if (mWatchThisFile){
            // 监视文件内容变化，一旦发现变化重新读取配置文件内容，保持配置信息同步
            mConfigurationFileWatch = new QFileSystemWatcher();
            QFileInfo mConfigurationFile;
            mConfigurationFile.setFile(mFileName);
            if (!mConfigurationFileWatch->files().contains(mConfigurationFile.absoluteFilePath()))
                mConfigurationFileWatch->addPath(mConfigurationFile.absoluteFilePath());

            connect(mConfigurationFileWatch, &QFileSystemWatcher::fileChanged, this, [=](const QString &fileName){
                this->load();
            });

            //mConfigurationFileWatch->addPath(mConfigurationFile.absolutePath());//只需要监视某个文件即可，这里不需要监视整个目录
            // connect(mConfigurationFileWatch, &QFileSystemWatcher::directoryChanged, [=](const QString &path){
            // });
        }
    };
    ~JsonSettings(){
        if (mConfigurationFileWatch)
        {
            delete mConfigurationFileWatch;
            mConfigurationFileWatch = nullptr;
        }

        if (!realtime)
        {
            flush();
        }
    };

    bool isOpen() {
        return this->mOpened;
    }

    QString fileName() const{
        return mFileName;
    }

    bool flush(){
        return save(mFileName);
    };

    /*
        {
            "键key": "值value",
        }
    */
    void setValue(const QString &key, const QVariant &value){
        QWriteLocker locker(&mRWLock);
        mJsonRoot[key] = value.toJsonValue();

        if (realtime)
            flush();
    };
    // 取值
    QVariant value(const QString &key, const QVariant &defaultValue = QVariant())
    {
        QReadLocker locker(&mRWLock);
        auto iterator = mJsonRoot.find(key);
        if (iterator != mJsonRoot.end()) {
            return mJsonRoot[key].toVariant();
        }
        else{
            return defaultValue;
        }
    };

    void setValue(QStringList &names, const QString &key, const QVariant &value){
        QWriteLocker locker(&mRWLock);
        setValue(mJsonRoot, names, key, value);
        if (realtime)
            flush();
    };

    QVariant value(QStringList &names, const QString &key, const QVariant &defaultValue = QVariant())
    {
        QReadLocker locker(&mRWLock);
        return value(mJsonRoot, names, key, defaultValue);
    };

    void setValueAt(QStringList &names, const quint8 &index, const QVariant &value){
        QWriteLocker locker(&mRWLock);
        setValueAt(mJsonRoot, names, index, value);

        if (realtime)
            flush();
    };

    QVariant valueAt(QStringList &names, const quint8 &index, const QVariant &defaultValue){
        QReadLocker locker(&mRWLock);
        return valueAt(mJsonRoot, names, index, defaultValue);
    };

    /*
    {
        "arrayName":[
            "键key1": "值value1", //arrayIndex===0
            "键key2": "值value2", //arrayIndex===1
        ]
    }
    */
    void appendArrayValue(const QString &arrayName, const QVariant &value)
    {
        QWriteLocker locker(&mRWLock);
        auto iterator = mJsonRoot.find(arrayName);
        if (iterator != mJsonRoot.end())
        {
            QJsonValueRef valueArrayRef = iterator.value();
            if (valueArrayRef.isArray())
            {
                QJsonArray arrayGroup = valueArrayRef.toArray();
                arrayGroup.append(value.toJsonValue());
                valueArrayRef = arrayGroup;

                if (realtime)
                    flush();
            }
        }
        else
        {
            QJsonArray arrayGroup;
            arrayGroup.append(value.toJsonValue());

            mJsonRoot.insert(arrayName, QJsonValue(arrayGroup));

            if (realtime)
                flush();
        }
    };

    /*
    {
        "arrayName":[
            "键key1": "值value1", //arrayIndex===0
            "键key2": "值value2", //arrayIndex===1
        ]
    }
    */
    void setArrayValue(const QString &arrayName, const quint8 &arrayIndex, const QVariant &value){
        QWriteLocker locker(&mRWLock);
        auto iterator = mJsonRoot.find(arrayName);
        if (iterator != mJsonRoot.end())
        {
            QJsonValueRef valueArrayRef = iterator.value();
            if (valueArrayRef.isArray())
            {
                QJsonArray arrayGroup = valueArrayRef.toArray();
                if (arrayIndex < arrayGroup.size())
                {
                    arrayGroup.replace(arrayIndex, value.toJsonValue());
                    valueArrayRef = arrayGroup;

                    if (realtime)
                        flush();
                }
            }
        }
    };

    /*
    {
        "groupName":{
            "arrayName":[
                "键key1": "值value1", //arrayIndex===0
                "键key2": "值value2", //arrayIndex===2
            ]
        }
    }
    */

    /*
    {
        "groupName":{
            "arrayName":[
                {
                    "键key1": "值value1", //arrayIndex===0
                    "键key2": "值value2", //arrayIndex===2
                }
            ]
        }
    }
    */
    void setArrayValue(const QString &groupName, const QString &arrayName, const quint8 &arrayIndex, const QString &key, const QVariant &value){
        QWriteLocker locker(&mRWLock);
        auto iterator = mJsonRoot.find(groupName);
        if (iterator != mJsonRoot.end())
        {
            QJsonValueRef valueGroupRef = iterator.value();
            if (valueGroupRef.isObject())
            {
                QJsonObject objGroup = valueGroupRef.toObject();
                auto iterator2 = objGroup.find(arrayName);
                if (iterator2 != objGroup.end())
                {
                    QJsonValueRef valueArrayRef = iterator2.value();
                    if (valueArrayRef.isArray())
                    {
                        QJsonArray arrayGroup = valueArrayRef.toArray();
                        if (arrayIndex < arrayGroup.size()){
                            QJsonValueRef valueGroupRef = arrayGroup[arrayIndex];
                            if (valueGroupRef.isObject())
                            {
                                QJsonObject objArray = valueGroupRef.toObject();
                                objArray[key] = value.toJsonValue();

                                valueGroupRef = objArray;
                            }
                            else
                            {
                                // 找到字段，但是类型不对
                                return;
                            }
                        }
                        else
                        {
                            // 数组越界
                            QJsonObject objArray;
                            objArray[key] = value.toJsonValue();

                            arrayGroup.append(objArray);
                        }

                        valueArrayRef = arrayGroup;
                    }
                    else
                    {
                        // 找到字段，但是类型不对
                        return;
                    }
                }
                else
                {
                    QJsonObject objArray;
                    objArray[key] = value.toJsonValue();

                    QJsonArray arrayGroup;
                    arrayGroup.append(objArray);

                    objGroup.insert(arrayName, QJsonValue(arrayGroup));
                }

                valueGroupRef = objGroup;
            }
            else
            {
                // 找到字段，但是类型不对
                return;
            }
        }
        else
        {
            QJsonObject objArray;
            objArray[key] = value.toJsonValue();

            QJsonArray arrayGroup;
            arrayGroup.append(objArray);

            QJsonObject objGroup;
            objGroup.insert(arrayName, QJsonValue(arrayGroup));

            mJsonRoot.insert(groupName, QJsonValue(objGroup));
        }

        if (realtime)
            flush();
    };

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////
private:
    bool load(){
        //QReadLocker locker(&mRWLock);
        mJsonRoot = QJsonObject();
        mPrefix.clear();

        QFile file(mFileName);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QByteArray jsonData = file.readAll();
            file.close();

            QJsonParseError error;
            QJsonDocument mJsonDoc = QJsonDocument::fromJson(jsonData, &error);
            if (error.error == QJsonParseError::NoError) {
                if (mJsonDoc.isObject()) {
                    mJsonRoot = mJsonDoc.object();
                    return true;
                } else {
                    qDebug() << "文件[" << mFileName << "]解析失败！";
                    return false;
                }
            } else{
                qDebug() << "文件[" << mFileName << "]解析失败！" << error.errorString().toUtf8().constData();
                return false;
            }
        } else {
            qDebug() << "文件[" << mFileName << "]打开失败！";

            //是否需要创建一个新的文件
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)){
                file.close();
                return true;
            } else {
                return false;
            }
        }
    };

    bool save(const QString &fileName = ""){
        //QWriteLocker locker(&mRWLock);
        QFile file(fileName);
        if (fileName.isEmpty())
            file.setFileName(mFileName);

        if (file.open(QIODevice::ReadWrite | QIODevice::Text)) {
            QJsonDocument jsonDoc(mJsonRoot);
            file.write(jsonDoc.toJson());
            file.close();
            return true;
        } else {
            qDebug() << "文件[" << mFileName << "]信息保存失败！";
            return false;
        }
    };

    void setValue(QJsonObject &group, QStringList &names, const QString &key, const QVariant &value){
        // 取首名称
        QString name = names.front();
        names.pop_front();

        if (names.size() > 0){
            auto iterator = group.find(name);
            if (iterator != group.end()) {
                QJsonValueRef valueGroupRef = iterator.value();
                QJsonObject childGroup = valueGroupRef.toObject();
                setValue(childGroup, names, key, value);
                valueGroupRef = childGroup;
            }
            else {
                QJsonObject childGroup;
                group.insert(name, QJsonValue(childGroup));
                setValue(childGroup, names, key, value);
            }
        } else {
            auto iterator = group.find(name);
            if (iterator != group.end()) {
                QJsonValueRef valueGroupRef = iterator.value();
                QJsonObject targetGroup = valueGroupRef.toObject();
                targetGroup[key] = value.toJsonValue();
                valueGroupRef = targetGroup;
            }
            else {
                QJsonObject targetGroup;
                targetGroup[key] = value.toJsonValue();
                group.insert(name, QJsonValue(targetGroup));
            }
        }
    };

    QVariant value(QJsonObject &group, QStringList &names, const QString &key, const QVariant &defaultValue = QVariant())
    {
        QString name = names.front();
        names.pop_front();

        if (names.size() > 0){
            auto iterator = group.find(name);
            if (iterator != group.end()) {
                QJsonValueRef valueGroupRef = iterator.value();
                QJsonObject childGroup = valueGroupRef.toObject();
                return value(childGroup, names, key, defaultValue);
            }
            else {
                return defaultValue;
            }
        } else {
            auto iterator = group.find(name);
            if (iterator != group.end()) {
                QJsonValueRef valueGroupRef = iterator.value();
                QJsonObject targetGroup = valueGroupRef.toObject();
                return targetGroup[key].toVariant();
            }
            else {
                return defaultValue;
            }
        }
    };

    void setValueAt(QJsonObject &group, QStringList &names, const quint8 &index, const QVariant &value){
        // 取首名称
        QString name = names.front();
        names.pop_front();

        if (names.size() > 0){
            auto iterator = group.find(name);
            if (iterator != group.end()) {
                QJsonValueRef valueGroupRef = iterator.value();
                QJsonObject childGroup = valueGroupRef.toObject();
                setValueAt(childGroup, names, index, value);
                valueGroupRef = childGroup;
            }
            else {
                qDebug() << "Don't find group " << name;
            }
        } else {
            auto iterator = group.find(name);
            if (iterator != group.end())
            {
                QJsonValueRef valueArrayRef = iterator.value();
                if (valueArrayRef.isArray())
                {
                    QJsonArray arrayGroup = valueArrayRef.toArray();
                    if (index < arrayGroup.size())
                    {
                        arrayGroup.replace(index, value.toJsonValue());
                        valueArrayRef = arrayGroup;
                    }
                    else{
                        qDebug() << "Index out of range. " << index;
                    }
                }
                else {
                    qDebug() << "Don't find group " << name;
                }
            }
            else {
                qDebug() << "Don't find group " << name;
            }
        }
    };

    QVariant valueAt(QJsonObject &group, QStringList &names, const quint8 &index, const QVariant &defaultValue){
        // 取首名称
        QString name = names.front();
        names.pop_front();

        if (names.size() > 0){
            auto iterator = group.find(name);
            if (iterator != group.end()) {
                QJsonValueRef valueGroupRef = iterator.value();
                QJsonObject childGroup = valueGroupRef.toObject();
                return valueAt(childGroup, names, index, defaultValue);
            }
            else {
                qDebug() << "Don't find group " << name;
            }
        } else {
            auto iterator = group.find(name);
            if (iterator != group.end())
            {
                QJsonValueRef valueArrayRef = iterator.value();
                if (valueArrayRef.isArray())
                {
                    QJsonArray targetGroup = valueArrayRef.toArray();
                    if (index < targetGroup.size())
                    {
                        return targetGroup[index].toVariant();
                    }
                    else{
                        qDebug() << "Index out of range. " << index;
                    }
                }
                else {
                    qDebug() << "Don't find group " << name;
                }
            }
            else {
                qDebug() << "Don't find group " << name;
            }
        }
    };

protected:
    QString mFileName;
    QString mPrefix;
    QJsonObject mJsonRoot;
    QReadWriteLock mRWLock;
    QMutex mAccessMutex;//访问锁
    bool mOpened = false;//文档打开成功标识
    bool realtime = true;
    bool mWatchThisFile = false;
    QFileSystemWatcher *mConfigurationFileWatch;
};

#include <QColor>
#include <QSettings>
#include <QApplication>
class GlobalSettings: public QSettings
{
    Q_OBJECT
public:
    explicit GlobalSettings(QObject *parent = nullptr);
    explicit GlobalSettings(QString fileName, QObject *parent = nullptr);
    ~GlobalSettings();

#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    void setValue(QAnyStringView key, const QVariant &value);
#else
    void setValue(const QString &key, const QVariant &value);
#endif
    void setRealtimeSave(bool realtime);
    bool isRealtimeSave() const { return realtime;}
    
    void setIntVector(const QString& key, const QVector<int>& vec);
    void setDoubleVector(const QString& key, const QVector<double>& vec);
    QVector<int> GetIntVector(const QString& key,
                                 const QVector<int>& def = {}) const;
    QVector<double> GetDoubleVector(const QString& key,
                                 const QVector<double>& def = {}) const;

    bool isDarkTheme(){return mIsDarkTheme;};
    bool enableThemeColor(){return mThemeColorEnable;};
    QColor colorTheme(){return mThemeColor;};

private:
    bool realtime = false;
    bool mIsDarkTheme = true;
    bool mThemeColorEnable = true;
    QColor mThemeColor = QColor(255,255,255);
};

#define DET_NUM 32
#define IP_LENGTH 17
#define MAC_LENGTH 18
#define DEFAULT_HDF5_FILENAME "./Config/Settings.H5"
//参数结构定义
#pragma pack(push, 1)  // 确保字节对齐
typedef struct _DetParameter{
    //探测器ID
    quint8 id;

    //经度：0-360
    double longitude;

    //纬度：0-180
    double latitude;

    //基本设置
    //增益
    /*
     0001：对应增益为0.08；
     0002：对应增益为0.16；
     0003：对应增益为0.32；
     0004：对应增益为1.26；
     0005：对应增益为2.52；
     0006：对应增益为5.01；
     0007：对应增益为10.0；
    */
    double gain;

    //死时间
    quint8 deathTime;
    
    //触发阈值
    quint16 triggerThold;

    // 探测器网络设置，用于界面匹配对应通道，并不下发指令
    //IP,端口地址 192.168.0:5000
    char det_Ip_port[IP_LENGTH];
 
    //能谱设置
    //能谱刷新时间（毫秒）
    quint32 spectrumRefreshTime;
    //能谱长度
    quint32 spectrumLength;

    //能谱能量范围(主界面勾选能量刻度后才起作用)
    //能谱最小能量
    double spectrumPlotEnMin;
    //能谱最大能量
    double spectrumPlotEnMax;

    //波形设置
    //触发模式
    quint8 waveformTriggerMode;
    //波形长度
    quint32 waveformLength;

    //梯形成型
    //是否启用
    bool trapShapeEnable;
    //时间常数D1
    quint16 trapShapeTimeConstD1;
    //时间常数D2
    quint16 trapShapeTimeConstD2;
    //上升沿
    quint8 trapShapeRisePoint;
    //平顶
    quint8 trapShapePeakPoint;
    //下降沿
    quint8 trapShapeFallPoint;

    //高压电源
    //是否启用
    bool highVoltageEnable;
    //DAC高压输出电平
    quint16 highVoltageOutLevel;

    _DetParameter(){
        id = 1;

        longitude = 0.0;

        latitude = 0.0;

        //基本设置
        //增益
        gain = 1.26;

        //死时间,单位*10ns
        deathTime = 30;

        //触发阈值
        triggerThold = 100;

        //网络设置
        //IP地址
        memset(det_Ip_port, 0, sizeof(det_Ip_port));
        qstrcpy(det_Ip_port, "0.0.0.0");

        //能谱设置
        //能谱刷新时间,单位ms
        spectrumRefreshTime = 1000;
        //能谱长度
        spectrumLength = 8192;

        //能谱能量范围(刻度后才起作用)
        //能谱最小能量/keV
        spectrumPlotEnMin = 0.0;
        //能谱最大能量/keV
        spectrumPlotEnMax = 3000.0;

        //波形设置
        //触发模式 0-定时触发 1-普通触发
        waveformTriggerMode = 0;
        //波形长度
        waveformLength = 512;

        //梯形成型
        //是否启用
        trapShapeEnable = true;
        //时间常数D1
        trapShapeTimeConstD1 = 24111;
        //时间常数D2
        trapShapeTimeConstD2 = 59310;
        //上升沿
        trapShapeRisePoint = 10;
        //平顶
        trapShapePeakPoint = 15;
        //下降沿
        trapShapeFallPoint = 10;

        //高压电源
        //是否启用
        highVoltageEnable = true;
        //DAC高压输出电平
        highVoltageOutLevel = 800;
    }
}DetParameter;
#pragma pack(pop)

#include "H5Cpp.h"
class HDF5Settings: public QObject
{
    Q_OBJECT

    Q_PROPERTY(quint32 pluseTime READ pluseTime WRITE setPluseTime NOTIFY pluseTimeChanged FINAL)
public:
    explicit HDF5Settings(QObject *parent = nullptr);
    ~HDF5Settings();

    static HDF5Settings *instance() {
        static HDF5Settings hdf5Settings;
        return &hdf5Settings;
    }

    QMap<quint8, DetParameter> getDetParameters() const;
    void setDetParameter(const QMap<quint8, DetParameter>& detParameters);
    
    // 写一个标量字符串 attribute（可变长）
    void writeStrAttr(H5::DataSet& ds, const std::string& key, const std::string& val);

    // 给数据集写入探测器参数结构属性说明
    void writeDetParStrAttr(H5::DataSet& dataset);

    // 定义FullSpectrum的HDF5复合类型
    H5::CompType createH5SpectrumType();

    // 定义DetParameter的HDF5复合类型
    static H5::CompType createCfgDataType();

    // 加载本地文件Config/Settings.H5中的探测器参数配置数据到内存
    // 如果没有文件或文件中没有相关数据，则使用默认参数，并创建文件和数据集
    void load_localDetPara();

    // 导出探测器参数配置数据(内存数据）到本地文件Config/Settings.H5
    void export_LocalDetPara();

    void createH5Spectrum(QString filePath);
    void closeH5Spectrum();

    // 创建指定探测器的能谱数据集 Detector#%1/Spectrum
    H5::DataSet createSpecDataSet(H5::H5File* H5File, quint8 index);

    /**
     * @brief 写入单个FullSpectrum结构体到HDF5文件
     * @param data 要写入的结构体数据
     */
    void writeH5Spectrum(quint8 index, const H5Spectrum& data);

    /**
     * @brief 批量写入多个 H5Spectrum 到同一探测器的数据集中
     * @param index 探测器索引（1~DET_NUM)
     * @param data  要写入的结构体数据容器
     */
    void writeH5SpectrumBatch(quint8 index, const QVector<H5Spectrum>& data);

        /**
     * @brief 从HDF5文件一次性指定探测器的全部读取H5Spectrum结构体
     * @param filePath H5文件路径
     * @param detectorId 探测器编号
     * @param outData H5Spectrum结构体类型容器
     * @return 是否读取成功
     */
    static bool readAllH5Spectrum(const std::string& filePath, const quint32 detectorId,
        QVector<H5Spectrum>& outData);

    /**
     * @brief 从HDF5文件逐行读取H5Spectrum结构体
     * @param filePath H5文件路径
     * @param groupName 分组名称(Detector#1)
     * @param datasetName 数据集名称(Spectrum)
     * @param callback 数据回调
     * @return 是否读取成功
     */
    bool readFullSpectrum(const std::string& filePath,
                                  const std::string& groupName,
                                  const std::string& datasetName,
                                  std::function<void(const H5Spectrum&)> callback);

    /**
     * @brief 保存测量参数到H5文件
     * @param filePath H5文件路径
     * @param parameters 包含所有探测器参数的映射
     * @return 是否保存成功
     */
    bool saveMeasureParametersToH5(const QString& filePath, const QMap<quint8, DetParameter>& parameters);

    // Getter函数
    quint32 pluseTime() const { return mPluseTime; }

    // Setter函数（修改属性并发射信号）
    void setPluseTime(quint32 newValue) {
        if (mPluseTime != newValue) {
            mPluseTime = newValue;
            emit pluseTimeChanged(); // 通知属性变化
        }
    }

    Q_SIGNAL void pluseTimeChanged(); // 信号声明
    Q_SIGNAL void sigSpectrum(const H5Spectrum&);

private:
    // 打开本地文件Config/Settings.H5，返回H5File对象指针
    H5::H5File *open_localDetParaFile();

    H5::H5File *mfH5Spectrum = nullptr; // H5能谱文件
    QString mSpectrumFilePath;          // 当前能谱文件路径
    bool mSpectrumFileFull = false;     // 文件是否已达到容量上限
    quint32 mSpectrumRef = 0;// 能谱计数
    H5::DataSet mSpectrumDataset[DET_NUM];
    
    H5::CompType mConfigDataType;//复合数据类型
    H5::CompType mSpectrumDataType;//复合数据类型
    QMap<quint8, DetParameter> mMapDetParameter;
    QMutex mWrite_mutex;
    quint32 mPluseTime = 10;//心跳时间(秒)
};

#endif // GLOBALSETTINGS_H

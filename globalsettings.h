#ifndef GLOBALSETTINGS_H
#define GLOBALSETTINGS_H

#include "spectypes.h"

#include <QObject>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QMutex>
#include <QReadWriteLock>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QColor>
#include <QSettings>

#define SYSTEM_CONFIG_FILE "./Config/system_config.ini" //系统配置文件,保存UI界面等全局配置信息,用户不建议编辑
#define DEVICE_CONFIG_FILE "./Config/device_config.ini" //设备配置文件,保存设备相关的配置信息，如探测器参数等，用户可以编辑
#define ENERGY_CALIBRATION_CSV_FILE "./Config/csv/energy_calibration.csv" // energy calibration: E = kx + b
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

    QMap<quint8, DetParameter*> getDetParameters() const;
    void setDetParameter(const QMap<quint8, DetParameter*>& detParameters);
    
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

    // 创建测量数据 H5文件
    void createH5Spectrum(QString filePath,  const QString& startTime, const QString& shotTime = QString());

    /** @brief 创建测量数据 H5 文件并写入指定探测器配置（不修改内存中的 mMapDetParameter） */
    void createH5Spectrum(QString filePath, const QString& startTime, const QString& shotTime,
                          const QVector<DetParameter>& configuration);
    
    // 向当前测量H5文件写入/更新打靶时刻（Config/shotTime）
    bool writeShotTimeToCurrentH5(const QString& shotTime);

    // 关闭测量数据 H5文件
    void closeH5Spectrum();

    // 创建指定探测器的测量数据H5文件中的数据集 Detector#%1/Spectrum
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

    /** @brief 解析 Config/StartTime、Config/ShotTime 等字符串（yyyy-MM-dd_HHmmss 或 yyyy-MM-dd HH:mm:ss） */
    static QDateTime parseConfigTimeString(const QString &raw);

    /** @brief 从 H5 文件名解析开测时刻，如 2026-05-20_161318.H5 */
    static QDateTime measureStartTimeFromFileName(const QString &filePath);

    /** @brief 读取 Config/StartTime（与 createH5Spectrum 写入的 H5T_VARIABLE 字符串一致） */
    static QDateTime readConfigStartTime(const std::string &filePathStd);

    /** @brief 读取 Config/ShotTime（可选） */
    static QDateTime readConfigShotTime(const std::string &filePathStd);

    /** @brief 开测时刻：优先 Config/StartTime，否则文件名 */
    static QDateTime resolveMeasureStartTime(const QString &filePath, const std::string &filePathStd);

    /** @brief 从已打开的 Config 组读取字符串数据集（StartTime / ShotTime 等） */
    static QString readConfigStringFromGroup(H5::Group &cfgGroup, const char *datasetName);

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
    bool saveMeasureParametersToH5(const QString& filePath, const QMap<quint8, DetParameter*>& parameters);

    /**
     * @brief 从测量参数 H5 读取探测器配置（分批次 MeasureParameters / 全通道 Configuration）
     * @param filePath H5文件路径
     * @param outParameters 按 id 1..DET_NUM 顺序的探测器参数
     * @return 是否读取成功
     */
    static bool loadMeasureParametersFromH5(const QString& filePath, QVector<DetParameter>& outParameters);

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
    QMap<quint8, DetParameter*> mMapDetParameter;
    QMutex mWrite_mutex;
    quint32 mPluseTime = 10;//心跳时间(秒)
};

#endif // GLOBALSETTINGS_H

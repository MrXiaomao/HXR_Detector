#include "globalsettings.h"
#include <QFileInfo>
#include <QApplication>
#include <QTextCodec>
#include <QMessageBox>
#include <QDateTime>
#include <QThread>
#include <QDebug>
#include <QDir>

namespace {

/** 内存中只保留通道 1..DET_NUM，去掉 id=0 等非法键，并强制每路 id 与键一致。 */
inline void normalizeDetParameterMap(QMap<quint8, DetParameter*>& map)
{
    map.remove(0);
    for (quint8 k : map.keys()) {
        if (k > DET_NUM)
            map.remove(k);
    }
    for (int i = 1; i <= DET_NUM; ++i) {
        const quint8 ki = static_cast<quint8>(i);
        if (!map.contains(ki)) {
            DetParameter *d = new DetParameter;
            d->id = ki;
            map[ki] = d;
        } else {
            map[ki]->id = ki;
        }
    }
}

/** 写入 HDF5 Configuration：固定 DET_NUM 行，行序与 id 一一对应为 1..DET_NUM。 */
QVector<DetParameter*> configurationRowsForWrite(const QMap<quint8, DetParameter*>& map)
{
    QVector<DetParameter*> data;
    data.reserve(DET_NUM);
    for (int i = 1; i <= DET_NUM; ++i) {
        const quint8 ki = static_cast<quint8>(i);
        if (!map.contains(ki)) {
            DetParameter *d = new DetParameter;
            d->id = ki;
            data.push_back(d);
        } else {
            map[ki]->id = ki;
            data.push_back(map[ki]);
        }
    }
    return data;
}

} // namespace

H5Spectrum FullSpectoH5Spec(const FullSpectrum& src)
{
    H5Spectrum h5Spec{};
    h5Spec.sequence    = src.sequence;
    h5Spec.timeUTCs   = src.timeUTCs;
    h5Spec.timeUTCms   = src.timeUTCms;
    h5Spec.measureTime = src.measureTime;
    h5Spec.deathTime   = src.deathTime;

    std::memcpy(h5Spec.spectrum,
                src.spectrum,
                sizeof(h5Spec.spectrum));

    return h5Spec;
}

/*#########################################################*/
GlobalSettings::GlobalSettings(QObject *parent)
    : QSettings(SYSTEM_CONFIG_FILE, QSettings::IniFormat, parent)
{
    this->setIniCodec(QTextCodec::codecForName("utf-8"));
}

GlobalSettings::GlobalSettings(QString filePath, QObject *parent)
    : QSettings(filePath, QSettings::IniFormat, parent)
{
    this->setIniCodec(QTextCodec::codecForName("utf-8"));
}

GlobalSettings::~GlobalSettings() {

}

#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
void GlobalSettings::setValue(QAnyStringView key, const QVariant &value)
#else
void GlobalSettings::setValue(const QString &key, const QVariant &value)
#endif
{
    QSettings::setValue(key, value);
    if(realtime)
        sync();
}

void GlobalSettings::setIntVector(const QString& key, const QVector<int>& vec)
{
    QStringList sl;
    sl.reserve(vec.size());
    for (int v : vec)
        sl << QString::number(v);
    this->setValue(key, sl);
}

QVector<int> GlobalSettings::GetIntVector(const QString& key,
                                            const QVector<int>& def) const
{
    const QVariant v = value(key);
    if (!v.isValid())
        return def;
}

void GlobalSettings::setDoubleVector(const QString& key, const QVector<double>& vec)
{
    QStringList sl;
    sl.reserve(vec.size());

    // 'g', 17：尽量保证 double 往返不丢精度
    for (double v : vec)
        sl << QString::number(v, 'g', 17);

    this->setValue(key, sl);
    // 如果你们希望立刻落盘，可选sync()：
    if(realtime)
        sync();
}

// 从配置读取 double 向量
// eg：QVector<double> loaded = settings.doubleVector("Global/Keys");
QVector<double> GlobalSettings::GetDoubleVector(const QString& key,
                                            const QVector<double>& def) const
{
    const QVariant v = value(key);
    if (!v.isValid())
        return def;

    // Qt/ini 里通常会以 QStringList 形式读回
    const QStringList sl = v.toStringList();
    if (sl.isEmpty())
        return QVector<double>{}; // 或 def，看你语义

    QVector<double> vec;
    vec.reserve(sl.size());
    for (const QString& s : sl)
        vec << s.toDouble();

    return vec;
}

void GlobalSettings::setRealtimeSave(bool realtime)
{
    this->realtime = realtime;
}

//////////////////////////////
HDF5Settings::HDF5Settings(QObject *parent) : QObject(parent), mPluseTime(10)
{
    //初始化数据类型
    mConfigDataType = createCfgDataType();
    mSpectrumDataType = createH5SpectrumType();

    // 检查梯形成型默认参数项
    if (!GlobalSettings(DEVICE_CONFIG_FILE).contains("FPGA")){
        GlobalSettings(DEVICE_CONFIG_FILE).setValue("FPGA/RisePoint", 10);
        GlobalSettings(DEVICE_CONFIG_FILE).setValue("FPGA/Peakoint", 15);
        GlobalSettings(DEVICE_CONFIG_FILE).setValue("FPGA/FallPoint", 10);
    }

    // 创建配置文件
    load_localDetPara();
    //readFullSpectrum("./cache/000_61/2026-01-13_124611_1.H5", "Detector#1", "Spectrum", nullptr);
}

HDF5Settings::~HDF5Settings()
{
    if (mfH5Spectrum)
    {
        mfH5Spectrum->close();
        delete mfH5Spectrum;
        mfH5Spectrum = nullptr;
    }

    if (mMapDetParameter.size() > 0){
        for (auto iter=mMapDetParameter.begin(); iter!=mMapDetParameter.end(); ++iter){
            DetParameter* detParameter = iter.value();
            delete detParameter;
            detParameter = nullptr;
        }

        mMapDetParameter.clear();
    }
}

QMap<quint8, DetParameter*> HDF5Settings::getDetParameters() const
{
    return mMapDetParameter;
}

void HDF5Settings::setDetParameter(const QMap<quint8, DetParameter*>& detParameters)
{
    QMutexLocker locker(&mWrite_mutex);
    for (const auto& detParameter : detParameters)
        mMapDetParameter[detParameter->id] = detParameter;
    normalizeDetParameterMap(mMapDetParameter);
}

void HDF5Settings::export_LocalDetPara()
{
    QMutexLocker locker(&mWrite_mutex);
    normalizeDetParameterMap(mMapDetParameter);
    H5::H5File *mfH5Setting = open_localDetParaFile();
    if (mfH5Setting == nullptr)
        return;

    try {
        // 1 打开文件（不清空）
        // H5::H5File fH5Setting(DEFAULT_HDF5_FILENAME, H5F_ACC_RDWR);
        // 2 打开或创建 Config group
        H5::Group cfg;
        if (H5Lexists(mfH5Setting->getId(), "Config", H5P_DEFAULT) > 0) {
            cfg = mfH5Setting->openGroup("Config");
        } else {
            cfg = mfH5Setting->createGroup("Config");
        }
        // 3 准备数据：固定 DET_NUM 行，id 为 1..DET_NUM（文件中不再出现 id=0）
        const QVector<DetParameter*> data = configurationRowsForWrite(mMapDetParameter);

        QVector<DetParameter> dataTemp;
        dataTemp.reserve(DET_NUM);
        for (int i = 0; i < data.size(); ++i) {
            dataTemp.push_back(*data[i]);
        }

        hsize_t dims[1] = {DET_NUM};
        H5::DataSpace dataspace(1, dims);
        // 4 dataset 存在 → 打开并写
        if (H5Lexists(cfg.getId(), "Configuration", H5P_DEFAULT) > 0) {
            H5::DataSet ds = cfg.openDataSet("Configuration");
            ds.write(dataTemp.data(), mConfigDataType);
        }
        else {
            // 5 dataset 不存在 → 创建并写
            H5::DataSet dataset = cfg.createDataSet("Configuration", mConfigDataType, dataspace);
            dataset.write(dataTemp.data(), mConfigDataType, dataspace);
            writeDetParStrAttr(dataset);
        }
        // mfH5Setting->close();
    } catch (H5::FileIException& error) {
        QMessageBox::warning(nullptr, "打开配置文件", "H5配置文件打开失败，文件被占用！");
    } catch (H5::DataSetIException& error) {
        QMessageBox::warning(nullptr, "打开配置文件", "H5配置文件打开失败，数据集已损坏！");
    } catch (H5::DataSpaceIException& error) {
        QMessageBox::warning(nullptr, "打开配置文件", "H5配置文件打开失败，数据空间已损坏！");
    }

    if (mfH5Setting)
    {
        mfH5Setting->close();
        delete mfH5Setting;
        mfH5Setting = nullptr;
    }
}

void HDF5Settings::writeDetParStrAttr(H5::DataSet& dataset)
{
    // 给每个字段写解释（多个键）
    writeStrAttr(dataset, "id",   "通道号(1~32)");
    writeStrAttr(dataset, "longitude",  "经度(-180~180)");
    writeStrAttr(dataset, "latitude",   "纬度(-90~90)");
    writeStrAttr(dataset, "gain", "增益(0.08~10.0)");
    writeStrAttr(dataset, "deathT", "探测器死时间设定(*10ns)");
    writeStrAttr(dataset, "Threshold", "触发阈值，低于阈值的波形不计入能谱");
    writeStrAttr(dataset, "detIpPort", "探测器 IP 地址");
    writeStrAttr(dataset, "specDeltaT", "能谱刷新时间(毫秒)");
    writeStrAttr(dataset, "specLen", "能谱长度");
    writeStrAttr(dataset, "specEnergyMin", "能谱绘制最小能量(keV)");
    writeStrAttr(dataset, "specEnergyMax", "能谱绘制最大能量(keV)");
    writeStrAttr(dataset, "waveTrigMode", "波形触发模式(0-定时触发 1-普通触发)");
    writeStrAttr(dataset, "waveLen", "波形长度");
    writeStrAttr(dataset, "TShapeEnable", "梯形成型使能(0/1)");
    writeStrAttr(dataset, "TShapeD1", "梯形成型时间常数 D1");
    writeStrAttr(dataset, "TShapeD2", "梯形成型时间常数 D2");
    writeStrAttr(dataset, "TShapeRise", "梯形成型上升沿点数");
    writeStrAttr(dataset, "TShapePeak", "梯形成型平顶点数");
    writeStrAttr(dataset, "TShapeFall", "梯形成型下降沿点数");
    writeStrAttr(dataset, "HV_Enable", "高压使能(0/1)");
    writeStrAttr(dataset, "HV_Out", "DAC 高压输出电平(0~4095)");
    writeStrAttr(dataset, "AlphaGammaUpper", "α/γ甄别上阈值(1~8192)");
    writeStrAttr(dataset, "AlphaGammaLower", "α/γ甄别下阈值(1~8192)");
}

// 写一个标量字符串 attribute（可变长）
void HDF5Settings::writeStrAttr(H5::DataSet& ds, const std::string& key, const std::string& val) {
    H5::StrType vstr(H5::PredType::C_S1, H5T_VARIABLE);
    H5::DataSpace scalar(H5S_SCALAR);

    H5::Attribute a = ds.createAttribute(key, vstr, scalar);
    // H5Cpp 常见写法：写 const char*
    const char* s = val.c_str();
    a.write(vstr, &s);
}

H5::H5File *HDF5Settings::open_localDetParaFile()
{
    H5::H5File *mfH5Setting = nullptr; // H5配置文件
    if (QFileInfo::exists(DEFAULT_HDF5_FILENAME))
    {
        try {
            mfH5Setting = new H5::H5File(DEFAULT_HDF5_FILENAME, H5F_ACC_RDWR);
        } catch (H5::FileIException& error) {
            QMessageBox::warning(nullptr, "打开配置文件", "H5配置文件打开失败，文件被占用！");
        } catch (H5::DataSetIException& error) {
            QMessageBox::warning(nullptr, "打开配置文件", "H5配置文件打开失败，数据集已损坏！");
        } catch (H5::DataSpaceIException& error) {
            QMessageBox::warning(nullptr, "打开配置文件", "H5配置文件打开失败，数据空间已损坏！");
        }
    }
    else
    {
        // 文件不存在，仅创建空文件并返回句柄（不在此处写入配置）
        H5::FileAccPropList fapl;
        fapl.setFcloseDegree(H5F_CLOSE_STRONG);  // 关闭时强制同步所有数据

        try {
            mfH5Setting = new H5::H5File(DEFAULT_HDF5_FILENAME, H5F_ACC_TRUNC, H5::FileCreatPropList::DEFAULT, fapl);
        } catch (H5::FileIException& error) {
            QMessageBox::warning(nullptr, "打开配置文件", "H5配置文件打开失败，文件被占用！");
        } catch (H5::DataSetIException& error) {
            QMessageBox::warning(nullptr, "打开配置文件", "H5配置文件打开失败，数据集已损坏！");
        } catch (H5::DataSpaceIException& error) {
            QMessageBox::warning(nullptr, "打开配置文件", "H5配置文件打开失败，数据空间已损坏！");
        }
    }

    return mfH5Setting;
}

void HDF5Settings::load_localDetPara()
{
    H5::H5File *mfH5Setting = nullptr; // H5配置文件
    bool configNeedsRewrite = false;
    if (QFileInfo::exists(DEFAULT_HDF5_FILENAME))
    {
        try {
            mfH5Setting = new H5::H5File(DEFAULT_HDF5_FILENAME, H5F_ACC_RDWR);
            H5::Group cfgGroup = mfH5Setting->openGroup("Config");
            H5::DataSet globalDataset = cfgGroup.openDataSet("Configuration");
            H5::DataSpace file_space = globalDataset.getSpace();

            // 获取数据集维度(行)
            int ndims = file_space.getSimpleExtentNdims();
            std::vector<hsize_t> dims(ndims);
            file_space.getSimpleExtentDims(dims.data());

            // 读取数据
            //一次性读取所有数据（兼容旧文件中带 id=0 的行：只采纳 id 在 1..DET_NUM 的记录）
            QVector<DetParameter> read_data(dims[0]);
            globalDataset.read(read_data.data(), mConfigDataType);
            if (static_cast<hsize_t>(dims[0]) != static_cast<hsize_t>(DET_NUM))
                configNeedsRewrite = true;
            else {
                for (const auto& item : read_data) {
                    if (item.id == 0 || item.id > DET_NUM) {
                        configNeedsRewrite = true;
                        break;
                    }
                }
            }
            mMapDetParameter.clear();
            for (const auto item : read_data) {
                if (item.id >= 1 && item.id <= DET_NUM){
                    DetParameter* det = new DetParameter;
                    memcpy(det, &item, sizeof(DetParameter));
                    det->trapShapeRisePoint = static_cast<quint8>(GlobalSettings(DEVICE_CONFIG_FILE).value("FPGA/RisePoint", 10).toUInt());//static_cast<quint8>(ui->spinBox_trapShapeRisePoint->value());
                    det->trapShapePeakPoint = static_cast<quint8>(GlobalSettings(DEVICE_CONFIG_FILE).value("FPGA/Peakoint", 15).toUInt());//static_cast<quint8>(ui->spinBox_trapShapePeakPoint->value());
                    det->trapShapeFallPoint = static_cast<quint8>(GlobalSettings(DEVICE_CONFIG_FILE).value("FPGA/FallPoint", 10).toUInt());//static_cast<quint8>(ui->spinBox_trapShapeFallPoint->value());
                    mMapDetParameter[item.id] = det;
                }
            }

            normalizeDetParameterMap(mMapDetParameter);
        } catch (H5::FileIException& error) {
            QMessageBox::warning(nullptr, "打开配置文件", "H5配置文件打开失败，文件被占用！");
        } catch (H5::DataSetIException& error) {
            QMessageBox::warning(nullptr, "打开配置文件", "H5配置文件打开失败，数据集已损坏！");
        } catch (H5::DataSpaceIException& error) {
            QMessageBox::warning(nullptr, "打开配置文件", "H5配置文件打开失败，数据空间已损坏！");
        }
    }
    else
    {
        // 文件不存在，创建新文件
        H5::FileAccPropList fapl;
        fapl.setFcloseDegree(H5F_CLOSE_STRONG);  // 关闭时强制同步所有数据

        try {
            mfH5Setting = new H5::H5File(DEFAULT_HDF5_FILENAME, H5F_ACC_TRUNC, H5::FileCreatPropList::DEFAULT, fapl);

            for (int i=1; i<=DET_NUM; ++i){
                DetParameter *detParameter = new DetParameter;
                detParameter->id = i;
                mMapDetParameter[i] = detParameter;
            }

            // 创建配置信息分组
            if (1){
                // 以前的实现方式
                const QVector<DetParameter*> data = configurationRowsForWrite(mMapDetParameter);

                QVector<DetParameter> dataTemp;
                dataTemp.reserve(DET_NUM);
                for (int i = 0; i < data.size(); ++i) {
                    dataTemp.push_back(*data[i]);
                }

                hsize_t dims[1] = {DET_NUM};
                H5::DataSpace dataspace(1, dims);
                H5::Group cfgGroup = mfH5Setting->createGroup("Config");
                H5::DataSet dataset = cfgGroup.createDataSet("Configuration", mConfigDataType, dataspace);

                // 写字段属性
                writeDetParStrAttr(dataset);
                // 写数据
                dataset.write(dataTemp.data(), mConfigDataType);

                H5Fflush(mfH5Setting->getId(), H5F_SCOPE_GLOBAL);  // 同步文件元数据
            }
            else{
                const QVector<DetParameter*> data = configurationRowsForWrite(mMapDetParameter);

                hsize_t dims[1] = {DET_NUM};
                H5::DataSpace dataspace(1, dims);
                H5::Group cfgGroup = mfH5Setting->createGroup("Config");
                H5::DataSet dataset = cfgGroup.createDataSet("Configuration", mConfigDataType, dataspace);

                // 写字段属性
                writeDetParStrAttr(dataset);

                // 写数据
                for (const auto& detParamter : data){
                    // 获取当前维度
                    H5::DataSpace file_space = dataset.getSpace();
                    hsize_t current_dims[2];
                    file_space.getSimpleExtentDims(current_dims, NULL);

                    // 1. 扩展数据集维度
                    current_dims[0] += 1;
                    dataset.extend(current_dims);

                    // 2. 重新获取扩展后的文件空间
                    file_space = dataset.getSpace();  // 关键：更新文件空间

                    // 3. 定义内存空间（1行数据）
                    hsize_t mem_dims[2] = {1, sizeof(DetParameter)};
                    H5::DataSpace mem_space(2, mem_dims);

                    // 4. 设置写入偏移（新行位置）
                    hsize_t offset[2] = {current_dims[0] - 1, 0};  // 偏移到最后一行
                    file_space.selectHyperslab(H5S_SELECT_SET, mem_dims, offset);

                    // 5. 写入数据（示例：填充i值）
                    dataset.write(detParamter, mConfigDataType);

                    // 6. 同步当前数据集，防止发生异常，数据丢失
                    //H5Dflush(dataset.getId());
                    H5Fflush(dataset.getId(), H5F_SCOPE_GLOBAL);
                }

                H5Fflush(mfH5Setting->getId(), H5F_SCOPE_GLOBAL);  // 同步文件元数据
            }
        } catch (H5::FileIException& error) {
            QMessageBox::warning(nullptr, "打开配置文件", "H5配置文件打开失败，文件被占用！");
        } catch (H5::DataSetIException& error) {
            QMessageBox::warning(nullptr, "打开配置文件", "H5配置文件打开失败，数据集已损坏！");
        } catch (H5::DataSpaceIException& error) {
            QMessageBox::warning(nullptr, "打开配置文件", "H5配置文件打开失败，数据空间已损坏！");
        }
    }

    if (mfH5Setting)
    {
        mfH5Setting->close();
        delete mfH5Setting;
        mfH5Setting = nullptr;
    }

    if (configNeedsRewrite)
        export_LocalDetPara();
}

H5::DataSet HDF5Settings::createSpecDataSet(H5::H5File* H5File, quint8 index)
{
    hsize_t columns = 8197;
    hsize_t init_dims[2] = {0, columns};       // 初始维度
    hsize_t max_dims[2] = {H5S_UNLIMITED, columns};  // 最大维度
    H5::DataSpace dataspace(2, init_dims, max_dims);

    // 设置分块存储
    H5::DSetCreatPropList prop_list;
    hsize_t chunk_dims[2] = {1, columns};  // 分块大小
    prop_list.setChunk(2, chunk_dims);

    // 创建数据集
    H5::Group cfgGroup = H5File->createGroup(QString("Detector#%1").arg(index).toStdString());
    H5::DataSet dataset = cfgGroup.createDataSet("Spectrum", H5::PredType::NATIVE_UINT, dataspace, prop_list);

    writeStrAttr(dataset, "0", "能谱序号");
    writeStrAttr(dataset, "1", "utc整数秒");
    writeStrAttr(dataset, "2", "utc小数秒");
    writeStrAttr(dataset, "3", "能量测量时间间隔,单位ms");
    writeStrAttr(dataset, "4", "死时间,单位*10ns");

    return dataset;
}

void HDF5Settings::createH5Spectrum(QString filePath, const QString& startTime, const QString& shotTime,
                                    const QVector<DetParameter>& configuration)
{
    if (!QFileInfo::exists(filePath))
    {
        H5::FileAccPropList fapl;
        fapl.setFcloseDegree(H5F_CLOSE_STRONG);

        try {
            QMutexLocker locker(&mWrite_mutex);
            closeAllChannelH5SpectrumUnlocked();

            mSpectrumRef = 0;
            mSpectrumFileFull = false;
            mSpectrumFilePath = filePath;
            QTextCodec* gbk_codec = QTextCodec::codecForName("GBK");
            QByteArray filePathBytes = gbk_codec->fromUnicode(filePath);
            mfH5Spectrum = new H5::H5File(filePathBytes.toStdString(), H5F_ACC_TRUNC);

            QVector<DetParameter> dataTemp = configuration;
            dataTemp.resize(DET_NUM);
            for (int i = 0; i < DET_NUM; ++i) {
                if (dataTemp[i].id == 0)
                    dataTemp[i].id = static_cast<quint8>(i + 1);
            }

            hsize_t dims[1] = {DET_NUM};
            H5::DataSpace dataspace(1, dims);
            H5::Group cfgGroup = mfH5Spectrum->createGroup("Config");
            H5::DataSet dataset = cfgGroup.createDataSet("Configuration", mConfigDataType, dataspace);
            dataset.write(dataTemp.data(), mConfigDataType);

            std::string timeStr = startTime.toStdString();
            H5::StrType strType(H5::PredType::C_S1, H5T_VARIABLE);
            H5::DataSpace scalarSpace(H5S_SCALAR);
            H5::DataSet dataset2 = cfgGroup.createDataSet("StartTime", strType, scalarSpace);
            dataset2.write(timeStr, strType);

            if (!shotTime.isEmpty()) {
                std::string shotTimeStr = shotTime.toStdString();
                H5::DataSet shotTimeDataset = cfgGroup.createDataSet("ShotTime", strType, scalarSpace);
                shotTimeDataset.write(shotTimeStr, strType);
            }

            qInfo().noquote() << QStringLiteral("已创建H5能谱文件：%1")
                                     .arg(QDir::toNativeSeparators(QFileInfo(filePath).absoluteFilePath()));
        } catch (H5::FileIException& error) {
            qWarning().noquote() << QStringLiteral("创建H5能谱文件失败：%1").arg(filePath);
            QMessageBox::warning(nullptr, "打开配置文件", "H5能谱数据文件打开失败，文件被占用！");
        } catch (H5::DataSetIException& error) {
            qWarning().noquote() << QStringLiteral("创建H5能谱文件失败：%1").arg(filePath);
            QMessageBox::warning(nullptr, "打开配置文件", "H5能谱数据文件打开失败，数据集已损坏！");
        } catch (H5::DataSpaceIException& error) {
            qWarning().noquote() << QStringLiteral("创建H5能谱文件失败：%1").arg(filePath);
            QMessageBox::warning(nullptr, "打开配置文件", "H5能谱数据文件打开失败，数据空间已损坏！");
        }
    }
}

void HDF5Settings::createH5Spectrum(QString filePath, const QString& startTime, const QString& shotTime)
{
    normalizeDetParameterMap(mMapDetParameter);
    const QVector<DetParameter*> data = configurationRowsForWrite(mMapDetParameter);

    QVector<DetParameter> dataTemp;
    dataTemp.reserve(DET_NUM);
    for (int i = 0; i < data.size(); ++i)
        dataTemp.push_back(*data[i]);

    createH5Spectrum(filePath, startTime, shotTime, dataTemp);
}

void HDF5Settings::closeAllChannelH5SpectrumUnlocked()
{
    for (auto it = mChannelSpectrumH5.begin(); it != mChannelSpectrumH5.end(); ++it) {
        ChannelSpectrumH5 &ch = it.value();
        try {
            if (ch.dataset.getId() >= 0)
                ch.dataset.close();
        } catch (H5::Exception &) {
        }
        if (ch.file) {
            try {
                H5Fflush(ch.file->getId(), H5F_SCOPE_GLOBAL);
                ch.file->close();
            } catch (H5::Exception &) {
            }
            delete ch.file;
            ch.file = nullptr;
        }
    }
    mChannelSpectrumH5.clear();
}

void HDF5Settings::closeChannelH5Spectrum(quint8 index)
{
    if (index < 1 || index > DET_NUM)
        return;

    QMutexLocker locker(&mWrite_mutex);
    auto it = mChannelSpectrumH5.find(index);
    if (it == mChannelSpectrumH5.end())
        return;

    ChannelSpectrumH5 &ch = it.value();
    try {
        if (ch.dataset.getId() >= 0)
            ch.dataset.close();
    } catch (H5::Exception &) {
    }
    if (ch.file) {
        try {
            H5Fflush(ch.file->getId(), H5F_SCOPE_GLOBAL);
            ch.file->close();
        } catch (H5::Exception &) {
        }
        delete ch.file;
        ch.file = nullptr;
    }
    mChannelSpectrumH5.erase(it);
}

bool HDF5Settings::hasChannelH5Spectrum(quint8 index) const
{
    return mChannelSpectrumH5.contains(index);
}

QString HDF5Settings::createChannelH5Spectrum(quint8 index, QString filePath, const QString& startTime,
                                           const QString& shotTime)
{
    normalizeDetParameterMap(mMapDetParameter);
    const QVector<DetParameter*> data = configurationRowsForWrite(mMapDetParameter);

    QVector<DetParameter> dataTemp;
    dataTemp.reserve(DET_NUM);
    for (int i = 0; i < data.size(); ++i)
        dataTemp.push_back(*data[i]);

    return createChannelH5Spectrum(index, filePath, startTime, shotTime, dataTemp);
}

QString HDF5Settings::createChannelH5Spectrum(quint8 index, QString filePath, const QString& startTime,
                                           const QString& shotTime,
                                           const QVector<DetParameter>& configuration)
{
    if (index < 1 || index > DET_NUM || filePath.isEmpty())
        return {};

    // 同秒重复重连时避免覆盖：在文件名后追加序号
    QString uniquePath = filePath;
    if (QFileInfo::exists(uniquePath)) {
        const QFileInfo info(filePath);
        const QString dir = info.path();
        const QString base = info.completeBaseName();
        const QString suffix = info.suffix();
        int n = 2;
        do {
            uniquePath = QStringLiteral("%1/%2_%3.%4").arg(dir, base).arg(n).arg(suffix);
            ++n;
        } while (QFileInfo::exists(uniquePath) && n < 1000);
    }

    H5::FileAccPropList fapl;
    fapl.setFcloseDegree(H5F_CLOSE_STRONG);

    QMutexLocker locker(&mWrite_mutex);

    // 关闭该通道已有独立文件（若有）
    {
        auto it = mChannelSpectrumH5.find(index);
        if (it != mChannelSpectrumH5.end()) {
            ChannelSpectrumH5 &old = it.value();
            try {
                if (old.dataset.getId() >= 0)
                    old.dataset.close();
            } catch (H5::Exception &) {
            }
            if (old.file) {
                try {
                    H5Fflush(old.file->getId(), H5F_SCOPE_GLOBAL);
                    old.file->close();
                } catch (H5::Exception &) {
                }
                delete old.file;
            }
            mChannelSpectrumH5.erase(it);
        }
    }

    try {
        QTextCodec *gbk_codec = QTextCodec::codecForName("GBK");
        QByteArray filePathBytes = gbk_codec->fromUnicode(uniquePath);

        ChannelSpectrumH5 &ch = mChannelSpectrumH5[index];
        ch.file = new H5::H5File(filePathBytes.toStdString(), H5F_ACC_TRUNC);
        ch.path = uniquePath;
        ch.full = false;
        ch.flushRef = 0;

        QVector<DetParameter> dataTemp = configuration;
        dataTemp.resize(DET_NUM);
        for (int i = 0; i < DET_NUM; ++i) {
            if (dataTemp[i].id == 0)
                dataTemp[i].id = static_cast<quint8>(i + 1);
        }

        hsize_t dims[1] = {DET_NUM};
        H5::DataSpace dataspace(1, dims);
        H5::Group cfgGroup = ch.file->createGroup("Config");
        H5::DataSet cfgDs = cfgGroup.createDataSet("Configuration", mConfigDataType, dataspace);
        cfgDs.write(dataTemp.data(), mConfigDataType);

        H5::StrType strType(H5::PredType::C_S1, H5T_VARIABLE);
        H5::DataSpace scalarSpace(H5S_SCALAR);
        std::string timeStr = startTime.toStdString();
        H5::DataSet startDs = cfgGroup.createDataSet("StartTime", strType, scalarSpace);
        startDs.write(timeStr, strType);

        if (!shotTime.isEmpty()) {
            std::string shotTimeStr = shotTime.toStdString();
            H5::DataSet shotDs = cfgGroup.createDataSet("ShotTime", strType, scalarSpace);
            shotDs.write(shotTimeStr, strType);
        }

        {
            hsize_t chDims[1] = {1};
            H5::DataSpace chSpace(1, chDims);
            H5::DataSet chIdDs = cfgGroup.createDataSet("ChannelId", H5::PredType::NATIVE_UINT8, chSpace);
            quint8 channelId = index;
            chIdDs.write(&channelId, H5::PredType::NATIVE_UINT8);
        }

        ch.dataset = createSpecDataSet(ch.file, index);

        qInfo().noquote() << QStringLiteral("谱仪[#%1]已创建H5能谱文件：%2")
                                 .arg(index)
                                 .arg(QDir::toNativeSeparators(QFileInfo(uniquePath).absoluteFilePath()));
        return uniquePath;
    } catch (H5::Exception &e) {
        qWarning().noquote() << QStringLiteral("谱仪[#%1]创建H5能谱文件失败：%2（%3）")
                                    .arg(index)
                                    .arg(uniquePath)
                                    .arg(QString::fromLocal8Bit(e.getCDetailMsg()));
        auto it = mChannelSpectrumH5.find(index);
        if (it != mChannelSpectrumH5.end()) {
            if (it->file) {
                try {
                    it->file->close();
                } catch (H5::Exception &) {
                }
                delete it->file;
            }
            mChannelSpectrumH5.erase(it);
        }
        return {};
    }
}

void HDF5Settings::closeH5Spectrum()
{
    QMutexLocker locker(&mWrite_mutex);
    closeAllChannelH5SpectrumUnlocked();

    if (mfH5Spectrum)
    {
        for (int i=0;i<DET_NUM; ++i){
            try {
                if (mSpectrumDataset[i].getId() >= 0)
                    mSpectrumDataset[i].close();
            } catch (H5::Exception &) {
            }
        }
        H5Fflush(mfH5Spectrum->getId(), H5F_SCOPE_GLOBAL);  // 同步文件元数据
        mfH5Spectrum->close();
        delete mfH5Spectrum;
        mfH5Spectrum = nullptr;
        mSpectrumFileFull = false;
        mSpectrumFilePath.clear();
    }
}

bool HDF5Settings::writeShotTimeToFileUnlocked(H5::H5File *file, const QString &shotTime)
{
    if (!file || shotTime.isEmpty())
        return false;

    try {
        H5::Group cfgGroup;
        if (H5Lexists(file->getId(), "Config", H5P_DEFAULT) > 0) {
            cfgGroup = file->openGroup("Config");
        } else {
            cfgGroup = file->createGroup("Config");
        }

        H5::StrType strType(H5::PredType::C_S1, H5T_VARIABLE);
        H5::DataSpace scalarSpace(H5S_SCALAR);
        std::string shotTimeStr = shotTime.toStdString();

        if (H5Lexists(cfgGroup.getId(), "ShotTime", H5P_DEFAULT) > 0) {
            H5::DataSet shotTimeDataset = cfgGroup.openDataSet("ShotTime");
            shotTimeDataset.write(shotTimeStr, strType);
        } else {
            H5::DataSet shotTimeDataset = cfgGroup.createDataSet("ShotTime", strType, scalarSpace);
            shotTimeDataset.write(shotTimeStr, strType);
        }

        H5Fflush(file->getId(), H5F_SCOPE_GLOBAL);
        return true;
    } catch (H5::Exception &e) {
        qWarning() << "写入shotTime失败:" << e.getCDetailMsg();
        return false;
    }
}

bool HDF5Settings::writeShotTimeToCurrentH5(const QString& shotTime)
{
    if (shotTime.isEmpty())
        return false;

    QMutexLocker locker(&mWrite_mutex);
    bool ok = false;
    if (mfH5Spectrum && !mSpectrumFilePath.isEmpty() && QFileInfo::exists(mSpectrumFilePath))
        ok = writeShotTimeToFileUnlocked(mfH5Spectrum, shotTime) || ok;

    for (auto it = mChannelSpectrumH5.begin(); it != mChannelSpectrumH5.end(); ++it) {
        if (it->file)
            ok = writeShotTimeToFileUnlocked(it->file, shotTime) || ok;
    }
    return ok;
}

H5::CompType HDF5Settings::createCfgDataType()
{
    H5::CompType type = H5::CompType(sizeof(DetParameter));//复合数据类型

    // 定义固定长度字符串类型(20字符)
    H5::StrType ipStrType(H5::PredType::C_S1, IP_LENGTH);

    type.insertMember("id", HOFFSET(DetParameter, id), H5::PredType::NATIVE_UINT8);
    // 经度
    type.insertMember("longitude", HOFFSET(DetParameter, longitude), H5::PredType::NATIVE_DOUBLE);
    // 纬度
    type.insertMember("latitude", HOFFSET(DetParameter, latitude), H5::PredType::NATIVE_DOUBLE);

    //基本设置
    {
        //增益
        type.insertMember("gain", HOFFSET(DetParameter, gain), H5::PredType::NATIVE_DOUBLE);
        //死时间
        type.insertMember("deathT", HOFFSET(DetParameter, deathTime), H5::PredType::NATIVE_UINT8);
        //触发阈值
        type.insertMember("Threshold", HOFFSET(DetParameter, triggerThold), H5::PredType::NATIVE_UINT16);
    }

    // 探测器网络设置，用于界面匹配对应通道
    {
        //IP地址,port号
        type.insertMember("detIpPort", HOFFSET(DetParameter, det_Ip_port), ipStrType);
    }

    //能谱设置
    {
        //能谱刷新时间（毫秒）
        type.insertMember("specDeltaT", HOFFSET(DetParameter, spectrumRefreshTime), H5::PredType::NATIVE_ULONG);
        //能谱长度
        type.insertMember("specLen", HOFFSET(DetParameter, spectrumLength), H5::PredType::NATIVE_ULONG);
    }

    //能谱能量范围
    //能谱绘制最小能量
    type.insertMember("specEnergyMin", HOFFSET(DetParameter, spectrumPlotEnMin), H5::PredType::NATIVE_DOUBLE);
    //能谱最大能量
    type.insertMember("specEnergyMax", HOFFSET(DetParameter, spectrumPlotEnMax), H5::PredType::NATIVE_DOUBLE);

    //波形设置
    {
        //触发模式
        type.insertMember("waveTrigMode", HOFFSET(DetParameter, waveformTriggerMode), H5::PredType::NATIVE_UINT8);
        //波形长度
        type.insertMember("waveLen", HOFFSET(DetParameter, waveformLength), H5::PredType::NATIVE_ULONG);
    }

    //梯形成型
    {
        //是否启用

        type.insertMember("TShapeEnable", HOFFSET(DetParameter, trapShapeEnable), H5::PredType::NATIVE_HBOOL);
        //时间常数D1
        type.insertMember("TShapeD1", HOFFSET(DetParameter, trapShapeTimeConstD1), H5::PredType::NATIVE_UINT16);
        //时间常数D2
        type.insertMember("TShapeD2", HOFFSET(DetParameter, trapShapeTimeConstD2), H5::PredType::NATIVE_UINT16);
        //上升沿
        type.insertMember("TShapeRise", HOFFSET(DetParameter, trapShapeRisePoint), H5::PredType::NATIVE_UINT8);
        //平顶
        type.insertMember("TShapePeak", HOFFSET(DetParameter, trapShapePeakPoint), H5::PredType::NATIVE_UINT8);
        //下降沿
        type.insertMember("TShapeFall", HOFFSET(DetParameter, trapShapeFallPoint), H5::PredType::NATIVE_UINT8);
    }

    //高压电源
    {
        //是否启用
        type.insertMember("HV_Enable", HOFFSET(DetParameter, highVoltageEnable), H5::PredType::NATIVE_HBOOL);
        //DAC高压输出电平
        type.insertMember("HV_Out", HOFFSET(DetParameter, highVoltageOutLevel), H5::PredType::NATIVE_USHORT);
    }

    //α/γ甄别阈值
    {
        type.insertMember("AlphaGammaUpper", HOFFSET(DetParameter, alphaGammaUpperThold), H5::PredType::NATIVE_USHORT);
        type.insertMember("AlphaGammaLower", HOFFSET(DetParameter, alphaGammaLowerThold), H5::PredType::NATIVE_USHORT);
    }

    return type;
}

// 定义FullSpectrum的HDF5复合类型
H5::CompType HDF5Settings::createH5SpectrumType()
{
    H5::CompType type(sizeof(H5Spectrum));

    // 逐个成员映射（注意内存对齐，需与结构体一致）
    type.insertMember("sequence", offsetof(H5Spectrum, sequence), H5::PredType::NATIVE_UINT32);
    type.insertMember("utcSecond", offsetof(H5Spectrum, timeUTCs), H5::PredType::NATIVE_UINT32);
    type.insertMember("utcMsecond", offsetof(H5Spectrum, timeUTCms), H5::PredType::NATIVE_UINT32);
    type.insertMember("measureTime", offsetof(H5Spectrum, measureTime), H5::PredType::NATIVE_UINT32);
    type.insertMember("deathTime", offsetof(H5Spectrum, deathTime), H5::PredType::NATIVE_UINT32);

    // 数组成员：spectrum[8192]
    hsize_t spectrum_dims[] = {8192};
    H5::ArrayType spectrum_array(H5::PredType::NATIVE_UINT32, 1, spectrum_dims);
    type.insertMember("spectrum", offsetof(H5Spectrum, spectrum), spectrum_array);

    return type;
}

/**
 * @brief 写入单个FullSpectrum结构体到HDF5文件
 * @param index 探测器索引（1~DET_NUM)
 * @param data 要写入的结构体数据
 */
void HDF5Settings::writeH5Spectrum(quint8 index, const H5Spectrum& data)
{
    QVector<H5Spectrum> batch;
    batch.append(data);
    writeH5SpectrumBatch(index, batch);
}

void HDF5Settings::writeH5SpectrumBatchToTarget(H5::H5File *file, H5::DataSet &dataset, quint8 index,
                                                const QVector<H5Spectrum> &data, const QString &filePath,
                                                bool *fileFullFlag, quint32 *flushRef)
{
    if (!file || data.isEmpty() || !fileFullFlag || !flushRef)
        return;
    if (*fileFullFlag)
        return;

    try {
        const hsize_t columns = sizeof(H5Spectrum) / sizeof(quint32);
        const hsize_t rows = static_cast<hsize_t>(data.size());

        if (!filePath.isEmpty()) {
            const qint64 maxBytes = 512LL * 1024 * 1024 * 1024; // 512GB
            QFileInfo info(filePath);
            if (info.exists()) {
                const qint64 currentSize = info.size();
                const qint64 estimateSize = currentSize
                    + static_cast<qint64>(rows) * static_cast<qint64>(sizeof(H5Spectrum));
                if (estimateSize > maxBytes) {
                    *fileFullFlag = true;
                    qWarning().noquote()
                        << QObject::tr("HDF5 能谱文件已达到容量上限，丢弃后续能谱写入：")
                        << filePath;
                    return;
                }
            }
        }

        if (dataset.getId() == -1)
            dataset = createSpecDataSet(file, index);

        H5::DataSpace file_space = dataset.getSpace();
        hsize_t current_dims[2];
        file_space.getSimpleExtentDims(current_dims, nullptr);

        current_dims[0] += rows;
        dataset.extend(current_dims);

        file_space = dataset.getSpace();
        hsize_t mem_dims[2] = {rows, columns};
        H5::DataSpace mem_space(2, mem_dims);
        hsize_t offset[2] = {current_dims[0] - rows, 0};
        file_space.selectHyperslab(H5S_SELECT_SET, mem_dims, offset);
        dataset.write(data.constData(), H5::PredType::NATIVE_UINT, mem_space, file_space);

        *flushRef += static_cast<quint32>(rows);
        if (*flushRef >= 100) {
            H5Fflush(file->getId(), H5F_SCOPE_GLOBAL);
            *flushRef = 0;
        }
    } catch (H5::Exception &e) {
        e.printErrorStack();
    }
}

/**
 * @brief 批量写入多个 H5Spectrum 到同一探测器的数据集中
 * @param index 探测器索引（1~DET_NUM)
 * @param data  要写入的结构体数据容器
 */
void HDF5Settings::writeH5SpectrumBatch(quint8 index, const QVector<H5Spectrum>& data)
{
    if (data.isEmpty() || index < 1 || index > DET_NUM)
        return;

    QMutexLocker locker(&mWrite_mutex);

    auto chIt = mChannelSpectrumH5.find(index);
    if (chIt != mChannelSpectrumH5.end()) {
        ChannelSpectrumH5 &ch = chIt.value();
        if (!ch.file)
            return;
        writeH5SpectrumBatchToTarget(ch.file, ch.dataset, index, data, ch.path,
                                     &ch.full, &ch.flushRef);
        return;
    }

    if (mSpectrumFileFull || !mfH5Spectrum)
        return;

    writeH5SpectrumBatchToTarget(mfH5Spectrum, mSpectrumDataset[index - 1], index, data,
                                 mSpectrumFilePath, &mSpectrumFileFull, &mSpectrumRef);
}

QDateTime HDF5Settings::parseConfigTimeString(const QString &raw)
{
    if (raw.isEmpty())
        return {};

    QDateTime dt = QDateTime::fromString(raw, QStringLiteral("yyyy-MM-dd_HHmmss"));
    if (!dt.isValid()) {
        QString normalized = raw;
        dt = QDateTime::fromString(normalized.replace('_', ' '), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    }
    if (!dt.isValid())
        dt = QDateTime::fromString(raw, QStringLiteral("yyyy-MM-dd HHmmss"));
    return dt;
}

QDateTime HDF5Settings::measureStartTimeFromFileName(const QString &filePath)
{
    const QString timePart = QFileInfo(filePath).baseName().left(17).replace('_', ' ');
    QDateTime dt = QDateTime::fromString(timePart, QStringLiteral("yyyy-MM-dd HHmmss"));
    if (!dt.isValid())
        dt = QDateTime::fromString(timePart, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    return dt;
}

QString HDF5Settings::readConfigStringFromGroup(H5::Group &cfgGroup, const char *datasetName)
{
    if (H5Lexists(cfgGroup.getId(), datasetName, H5P_DEFAULT) <= 0)
        return {};

    H5::DataSet ds = cfgGroup.openDataSet(datasetName);
    H5::StrType fileType = ds.getStrType();
    if (fileType.isVariableStr()) {
        char *str = nullptr;
        ds.read(&str, fileType);
        const QString raw = QString::fromUtf8(str);
        H5free_memory(str);
        return raw;
    }

    const size_t len = fileType.getSize();
    if (len == 0)
        return {};

    QByteArray buffer(static_cast<int>(len + 1), '\0');
    H5::StrType memType(H5::PredType::C_S1, len);
    memType.setCset(H5T_CSET_UTF8);
    ds.read(buffer.data(), memType);
    return QString::fromUtf8(buffer.constData());
}

QDateTime HDF5Settings::readConfigStartTime(const std::string &filePathStd)
{
    try {
        H5::H5File file(filePathStd, H5F_ACC_RDONLY);
        if (H5Lexists(file.getId(), "Config", H5P_DEFAULT) <= 0)
            return {};
        H5::Group cfgGroup = file.openGroup("Config");
        return parseConfigTimeString(readConfigStringFromGroup(cfgGroup, "StartTime"));
    } catch (H5::Exception &) {
        return {};
    }
}

QDateTime HDF5Settings::readConfigShotTime(const std::string &filePathStd)
{
    try {
        H5::H5File file(filePathStd, H5F_ACC_RDONLY);
        if (H5Lexists(file.getId(), "Config", H5P_DEFAULT) <= 0)
            return {};
        H5::Group cfgGroup = file.openGroup("Config");
        return parseConfigTimeString(readConfigStringFromGroup(cfgGroup, "ShotTime"));
    } catch (H5::Exception &) {
        return {};
    }
}

QDateTime HDF5Settings::resolveMeasureStartTime(const QString &filePath, const std::string &filePathStd)
{
    QDateTime startDt = readConfigStartTime(filePathStd);
    if (!startDt.isValid())
        startDt = measureStartTimeFromFileName(filePath);
    return startDt;
}

bool HDF5Settings::readAllH5Spectrum(const std::string& filePath, const quint32 detectorId,
    QVector<H5Spectrum>& outData)
{
    outData.clear();
    try {
        H5::H5File file(filePath, H5F_ACC_RDONLY);

        const std::string groupName = QString("Detector#%1").arg(detectorId).toStdString();
        if (H5Lexists(file.getId(), groupName.c_str(), H5P_DEFAULT) <= 0)
            return false;

        H5::Group group = file.openGroup(groupName);
        H5::DataSet dataset = group.openDataSet("Spectrum");

        H5::DataSpace file_space = dataset.getSpace();
        hsize_t dims[2] = {0, 0};
        file_space.getSimpleExtentDims(dims, nullptr);

        const hsize_t rows = dims[0];
        const hsize_t columns = dims[1];

        if (rows == 0)
            return true;

        const hsize_t expectedColumns = sizeof(H5Spectrum) / sizeof(quint32);
        if (columns != expectedColumns) {
            qWarning() << "H5Spectrum column mismatch:"
                       << columns << "!=" << expectedColumns;
            return false;
        }

        outData.resize(static_cast<int>(rows));

        hsize_t mem_dims[2] = {rows, columns};
        H5::DataSpace mem_space(2, mem_dims);

        dataset.read(outData.data(),
                     H5::PredType::NATIVE_UINT,
                     mem_space,
                     file_space);

        return true;

    } catch (H5::Exception &) {
        return false;
    }
}

bool HDF5Settings::readFullSpectrum(const std::string& filePath,
                                             const std::string& groupName,
                                             const std::string& datasetName,
                                             std::function<void(const H5Spectrum&)> callback)
{
    H5Spectrum data{};  // 初始化默认值

    try {
        // 1. 打开文件
        H5::H5File file(filePath, H5F_ACC_RDONLY);

        H5::Group cfgGroup = file.openGroup(groupName.c_str());
        H5::DataSet dataset = cfgGroup.openDataSet(datasetName.c_str());
        //readHDF5Table(dataset);
        // 3. 确认数据类型匹配（可选，用于错误检查）
        H5::CompType fileType = dataset.getCompType();
        if (fileType != H5::PredType::NATIVE_UINT) {
            return false;
        }

        H5::DataSpace file_space = dataset.getSpace();  // 初始文件空间
        hsize_t current_dims[2];
        file_space.getSimpleExtentDims(current_dims, NULL);
        hsize_t rows = current_dims[0];
        hsize_t cols = current_dims[1];

        // 内存空间：每次读取一行
        hsize_t mem_dims[2] = {1, cols};
        H5::DataSpace mem_space(2, mem_dims);
        uint32_t* row_data = new uint32_t[cols];
        for (hsize_t i = 0; i < rows; ++i) {
            hsize_t start[2] = {i, 0};
            hsize_t count[2] = {1, cols};
            file_space.selectHyperslab(H5S_SELECT_SET, count, start);

            dataset.read(&data, H5::PredType::NATIVE_UINT, mem_space, file_space);
            if (callback)
                callback(data);
            else
                QMetaObject::invokeMethod(this, "sigSpectrum",
                                          Qt::QueuedConnection,
                                          Q_ARG(H5Spectrum, data));
            file_space.selectNone();
        }
        delete[] row_data;

        // 5. 关闭文件
        file.close();
    } catch (H5::Exception& e) {
        e.printErrorStack();
        return false;
    }

    return true;
}

/**
 * @brief 保存测量参数到H5文件
 * @param filePath H5文件路径
 * @param parameters 包含所有探测器参数的映射
 * @return 是否保存成功
 */
bool HDF5Settings::saveMeasureParametersToH5(const QString& filePath, const QMap<quint8, DetParameter*>& parameters)
{
    try {
        // 文本编码处理（支持中文路径）
        QTextCodec* gbk_codec = QTextCodec::codecForName("GBK");
        QByteArray filePathBytes = gbk_codec->fromUnicode(filePath);

        // 1. 创建或打开H5文件
        H5::H5File file(filePathBytes.toStdString(), H5F_ACC_TRUNC);

        // 2. 创建或打开Config分组
        H5::Group cfgGroup;
        if (H5Lexists(file.getId(), "Config", H5P_DEFAULT) > 0) {
            cfgGroup = file.openGroup("Config");
        } else {
            cfgGroup = file.createGroup("Config");
        }

        // 3. 准备数据：按照 id 1..DET_NUM 的顺序
        QVector<DetParameter> data;
        data.reserve(DET_NUM);
        for (int i = 1; i <= DET_NUM; ++i) {
            const quint8 ki = static_cast<quint8>(i);
            if (parameters.contains(ki)){
                DetParameter row = *parameters.value(ki);
                data.push_back(row);
            }
            else{
                DetParameter row;
                row.id = ki;
                data.push_back(row);
            }
        }

        // 4. 创建数据空间
        hsize_t dims[1] = {DET_NUM};
        H5::DataSpace dataspace(1, dims);

        // 5. 写入或更新数据集
        if (H5Lexists(cfgGroup.getId(), "MeasureParameters", H5P_DEFAULT) > 0) {
            H5::DataSet ds = cfgGroup.openDataSet("MeasureParameters");
            ds.write(data.data(), mConfigDataType);
        } else {
            H5::DataSet dataset = cfgGroup.createDataSet("MeasureParameters", mConfigDataType, dataspace);
            dataset.write(data.data(), mConfigDataType, dataspace);
            
            // 写入字段说明属性
            writeDetParStrAttr(dataset);
        }

        // 6. 写入时间戳属性
        QString startTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        std::string timeStr = startTime.toStdString();
        H5::StrType strType(H5::PredType::C_S1, H5T_VARIABLE);
        H5::DataSpace scalarSpace(H5S_SCALAR);
        
        if (H5Lexists(cfgGroup.getId(), "SaveTime", H5P_DEFAULT) > 0) {
            H5::DataSet dsTime = cfgGroup.openDataSet("SaveTime");
            dsTime.write(timeStr, strType);
        } else {
            H5::DataSet dsTime = cfgGroup.createDataSet("SaveTime", strType, scalarSpace);
            dsTime.write(timeStr, strType);
        }

        // 7. 同步并关闭文件
        H5Fflush(file.getId(), H5F_SCOPE_GLOBAL);
        file.close();

        qInfo().noquote().nospace() << QObject::tr("测量参数已保存到：") << filePath;
        return true;

    } catch (H5::FileIException& error) {
        QMessageBox::warning(nullptr, QObject::tr("保存失败"), QObject::tr("H5文件打开失败，文件被占用！"));
        qWarning() << error.getCDetailMsg();
        return false;
    } catch (H5::DataSetIException& error) {
        QMessageBox::warning(nullptr, QObject::tr("保存失败"), QObject::tr("H5文件打开失败，数据集已损坏！"));
        qWarning() << error.getCDetailMsg();
        return false;
    } catch (H5::DataSpaceIException& error) {
        QMessageBox::warning(nullptr, QObject::tr("保存失败"), QObject::tr("H5文件打开失败，数据空间已损坏！"));
        qWarning() << error.getCDetailMsg();
        return false;
    } catch (H5::Exception& e) {
        e.printErrorStack();
        return false;
    }
}

bool HDF5Settings::loadMeasureParametersFromH5(const QString& filePath, QVector<DetParameter>& outParameters)
{
    outParameters.clear();
    if (!QFileInfo::exists(filePath))
        return false;

    auto loadFromDataset = [&](const char *datasetName) -> bool {
        try {
            QTextCodec* gbk_codec = QTextCodec::codecForName("GBK");
            QByteArray filePathBytes = gbk_codec->fromUnicode(filePath);
            H5::H5File file(filePathBytes.toStdString(), H5F_ACC_RDONLY);

            H5::Group cfgGroup = file.openGroup("Config");
            if (H5Lexists(cfgGroup.getId(), datasetName, H5P_DEFAULT) <= 0)
                return false;

            H5::DataSet dataset = cfgGroup.openDataSet(datasetName);
            H5::CompType cfgType = createCfgDataType();

            outParameters.resize(DET_NUM);
            dataset.read(outParameters.data(), cfgType);

            dataset.close();
            cfgGroup.close();
            file.close();
            return true;
        } catch (H5::Exception& e) {
            e.printErrorStack();
            outParameters.clear();
            return false;
        }
    };

    if (loadFromDataset("MeasureParameters"))
        return true;
    return loadFromDataset("Configuration");
}

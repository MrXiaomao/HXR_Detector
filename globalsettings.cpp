#include "globalsettings.h"
#include <QFileInfo>
#include <QApplication>
#include <QTextCodec>

H5Spectrum FullSpectoH5Spec(const FullSpectrum& src)
{
    H5Spectrum h5Spec{};
    h5Spec.sequence    = src.sequence;
    h5Spec.measureTime = src.measureTime;
    h5Spec.deathTime   = src.deathTime;

    std::memcpy(h5Spec.spectrum,
                src.spectrum,
                sizeof(h5Spec.spectrum));

    return h5Spec;
}

/*#########################################################*/
GlobalSettings::GlobalSettings(QObject *parent)
    : QSettings(GLOBAL_CONFIG_FILENAME, QSettings::IniFormat, parent)
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
    setValue(key, sl);
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

    setValue(key, sl);
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
    mCompDataType = createCfgDataType();
    mSpectrumDataType = createH5SpectrumType();

    // 创建配置文件
    createH5Config();

    //readFullSpectrum("./cache/000_61/2026-01-13_124611_1.H5", "Detector#1", "Spectrum", nullptr);
}

HDF5Settings::~HDF5Settings()
{
    if (mfH5Setting)
    {
        mfH5Setting->close();
        delete mfH5Setting;
        mfH5Setting = nullptr;
    }

    if (mfH5Spectrum)
    {
        mfH5Spectrum->close();
        delete mfH5Spectrum;
        mfH5Spectrum = nullptr;
    }
}

QMap<quint8, DetParameter>& HDF5Settings::detParameters()
{
    return mMapDetParameter;
}

void HDF5Settings::setDetParameter(QMap<quint8, DetParameter>& detParameters)
{
    mMapDetParameter = detParameters;
}

void HDF5Settings::sync()
{
    QMutexLocker locker(&mWrite_mutex);
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
        // 3 准备数据
        QVector<DetParameter> data;
        for (auto &pair : mMapDetParameter.toStdMap())
        {
            data.push_back(pair.second);
        }

        hsize_t dims[1] = {DET_NUM};
        H5::DataSpace dataspace(1, dims);
        // 4 dataset 存在 → 打开并写
        if (H5Lexists(cfg.getId(), "Detector", H5P_DEFAULT) > 0) {
            H5::DataSet ds = cfg.openDataSet("Detector");
            ds.write(data.data(), mCompDataType);
        }
        else {
            // 5 dataset 不存在 → 创建并写
            H5::DataSet dataset = cfg.createDataSet("Detector", mCompDataType, dataspace);
            dataset.write(data.data(), mCompDataType, dataspace);
            writeDetParStrAttr(dataset);
        }
        // mfH5Setting->close();
    } catch (H5::FileIException& error) {
        error.printErrorStack();
        return ;
    } catch (H5::DataSetIException& error) {
        error.printErrorStack();
        return ;
    } catch (H5::DataSpaceIException& error) {
        error.printErrorStack();
        return ;
    }
}

void HDF5Settings::writeDetParStrAttr(H5::DataSet& dataset)
{
    // 给每个字段写解释（多个键）
    writeStrAttr(dataset, "id",   "通道号(0~23)");
    writeStrAttr(dataset, "longitude",  "经度(0~360)");
    writeStrAttr(dataset, "latitude",   "纬度(0~180)");
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

void HDF5Settings::writeBytes(quint8 index, QByteArray& data)
{
    QMutexLocker locker(&mWrite_mutex);
    H5::DataSet dataset = mSpectrumDataset[index-1];//cfgGroup.openDataSet("Spectrum");

    // 获取当前维度
    H5::DataSpace file_space = dataset.getSpace();  // 初始文件空间
    hsize_t current_dims[2];
    file_space.getSimpleExtentDims(current_dims, NULL);

    // 1. 扩展数据集维度
    current_dims[0] += 1;
    dataset.extend(current_dims);

    // 2. 重新获取扩展后的文件空间
    file_space = dataset.getSpace();  // 关键：更新文件空间

    // 3. 定义内存空间（1行数据）
    hsize_t mem_dims[2] = {1, (hsize_t)data.size()};
    H5::DataSpace mem_space(2, mem_dims);

    // 4. 设置写入偏移（新行位置）
    hsize_t offset[2] = {current_dims[0] - 1, 0};  // 偏移到最后一行
    file_space.selectHyperslab(H5S_SELECT_SET, mem_dims, offset);

    // 5. 写入数据（示例：填充i值）
    dataset.write(data.constData(), H5::PredType::NATIVE_UCHAR, mem_space, file_space);

    // 6. 同步当前数据集，防止发生异常，数据丢失
    H5Dflush(dataset.getId());

    H5Fflush(mfH5Setting->getId(), H5F_SCOPE_GLOBAL);  // 同步文件元数据
}

void HDF5Settings::createH5Config()
{
    if (QFileInfo::exists(DEFAULT_HDF5_FILENAME))
    {
        mfH5Setting = new H5::H5File(DEFAULT_HDF5_FILENAME, H5F_ACC_RDWR);
        H5::Group cfgGroup = mfH5Setting->openGroup("Config");
        H5::DataSet globalDataset = cfgGroup.openDataSet("Detector");
        H5::DataSpace file_space = globalDataset.getSpace();

        // 获取数据集维度(行)
        int ndims = file_space.getSimpleExtentNdims();
        std::vector<hsize_t> dims(ndims);
        file_space.getSimpleExtentDims(dims.data());

        // 读取数据
        //一次性读取所有数据
        QVector<DetParameter> read_data(dims[0]);
        globalDataset.read(read_data.data(), mCompDataType);
        for (const auto& item : read_data) {
            mMapDetParameter[item.id] = item;
        }
        // mfH5Setting->close();
    }
    else
    {
        // 文件不存在，创建新文件
        H5::FileAccPropList fapl;
        fapl.setFcloseDegree(H5F_CLOSE_STRONG);  // 关闭时强制同步所有数据

        try {
            mfH5Setting = new H5::H5File(DEFAULT_HDF5_FILENAME, H5F_ACC_TRUNC, H5::FileCreatPropList::DEFAULT, fapl);

            for (int i=1; i<=DET_NUM; ++i){
                DetParameter detParameter;
                detParameter.id = i;
                mMapDetParameter[i] = detParameter;
            }

            // 创建配置信息分组
            {
                QVector<DetParameter> data;
                for (auto &pair : mMapDetParameter.toStdMap())
                {
                    data.push_back(pair.second);
                }

                hsize_t dims[1] = {DET_NUM};
                H5::DataSpace dataspace(1, dims);
                H5::Group cfgGroup = mfH5Setting->createGroup("Config");
                H5::DataSet dataset = cfgGroup.createDataSet("Detector", mCompDataType, dataspace);
                dataset.write(data.data(), mCompDataType);

                // 写字段属性
                writeDetParStrAttr(dataset);

                H5Fflush(mfH5Setting->getId(), H5F_SCOPE_GLOBAL);  // 同步文件元数据
            }                    
            // mfH5Setting->close();
            
            // 创建表格数组
            if (0)
            {
                hsize_t init_dims[2] = {0, 1060};       // 初始维度
                hsize_t max_dims[2] = {H5S_UNLIMITED, 1060};  // 最大维度
                H5::DataSpace dataspace(2, init_dims, max_dims);

                // 设置分块存储
                H5::DSetCreatPropList prop_list;
                hsize_t chunk_dims[2] = {1, 1060};  // 分块大小
                prop_list.setChunk(2, chunk_dims);

                for (int i=1; i<=DET_NUM; ++i){
                    // 创建数据集
                    H5::Group cfgGroup = mfH5Setting->createGroup(QString("Detector#%1").arg(i).toStdString());
                    H5::DataSet dataset = cfgGroup.createDataSet("Spectrum", mSpectrumDataType/*H5::PredType::NATIVE_UCHAR*/, dataspace, prop_list);
                    mSpectrumDataset[i-1] = dataset;
                }

                for (int i=1; i<=DET_NUM; ++i){
                    // 创建数据集
                    H5::DataSet dataset = mSpectrumDataset[i-1];

                    // 获取当前维度
                    H5::DataSpace file_space = dataset.getSpace();  // 初始文件空间
                    hsize_t current_dims[2];
                    file_space.getSimpleExtentDims(current_dims, NULL);

                    // 1. 扩展数据集维度
                    current_dims[0] += 1;
                    dataset.extend(current_dims);

                    // 2. 重新获取扩展后的文件空间
                    file_space = dataset.getSpace();  // 关键：更新文件空间

                    // 3. 定义内存空间（1行数据）
                    hsize_t mem_dims[2] = {1, 1060};
                    H5::DataSpace mem_space(2, mem_dims);

                    // 4. 设置写入偏移（新行位置）
                    hsize_t offset[2] = {current_dims[0] - 1, 0};  // 偏移到最后一行
                    file_space.selectHyperslab(H5S_SELECT_SET, mem_dims, offset);

                    // 5. 写入数据（示例：填充i值）
                    QByteArray data(1060, static_cast<uchar>(i));
                    dataset.write(data.constData(), H5::PredType::NATIVE_UCHAR, mem_space, file_space);

                    // 6. 同步当前数据集，防止发生异常，数据丢失
                    //H5Dflush(dataset.getId());
                    H5Fflush(dataset.getId(), H5F_SCOPE_GLOBAL);
                }
            }
        } catch (H5::FileIException& error) {
            error.printErrorStack();
            return ;
        } catch (H5::DataSetIException& error) {
            error.printErrorStack();
            return ;
        } catch (H5::DataSpaceIException& error) {
            error.printErrorStack();
            return ;
        }
    }
}

void HDF5Settings::createH5Spectrum(QString filePath)
{
    // 创建表格数组
    if (!QFileInfo::exists(filePath))
    {
        // 文件不存在，创建新文件
        H5::FileAccPropList fapl;
        fapl.setFcloseDegree(H5F_CLOSE_STRONG);  // 关闭时强制同步所有数据

        try {
            mSpectrumRef = 0;
            QTextCodec* gbk_codec = QTextCodec::codecForName("GBK");
            QByteArray filePathBytes = gbk_codec->fromUnicode(filePath);
            mfH5Spectrum = new H5::H5File(filePathBytes.toStdString(), H5F_ACC_TRUNC);//, H5::FileCreatPropList::DEFAULT, fapl);

            // 写入配置信息分组
            {
                QVector<DetParameter> data;
                for (auto &pair : mMapDetParameter.toStdMap())
                {
                    data.push_back(pair.second);
                }

                hsize_t dims[1] = {DET_NUM};
                H5::DataSpace dataspace(1, dims);
                H5::Group cfgGroup = mfH5Spectrum->createGroup("Config");
                H5::DataSet dataset = cfgGroup.createDataSet("Detector", mCompDataType, dataspace);
                dataset.write(data.data(), mCompDataType);

                // 写入当前时间到"Config"组中，以字符串的形式写入
                QString startTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
                std::string timeStr = startTime.toStdString();

                // 1. 创建可变长度字符串类型
                H5::StrType strType(H5::PredType::C_S1, H5T_VARIABLE);

                // 2. 创建标量 dataspace（不是数组！）
                H5::DataSpace scalarSpace(H5S_SCALAR);

                // 3. 创建数据集
                H5::DataSet dataset2 = cfgGroup.createDataSet(
                    "StartTime",
                    strType,
                    scalarSpace);

                // 4. 写入
                dataset2.write(timeStr, strType);

                // dataset2.write(startTime.toStdString().c_str(), H5::PredType::NATIVE_CHAR);
                // writeStrAttr(dataset, "StartTime", startTime.toStdString());
                //H5Dflush(dataset.getId());
                //H5Fflush(dataset.getId(), H5F_SCOPE_GLOBAL);
                //H5Fflush(mfH5Spectrum->getId(), H5F_SCOPE_GLOBAL);  // 同步文件元数据
            }

            hsize_t columns = 8195;
            hsize_t init_dims[2] = {0, columns};       // 初始维度
            hsize_t max_dims[2] = {H5S_UNLIMITED, columns};  // 最大维度
            H5::DataSpace dataspace(2, init_dims, max_dims);

            // 设置分块存储
            H5::DSetCreatPropList prop_list;
            hsize_t chunk_dims[2] = {1, columns};  // 分块大小
            prop_list.setChunk(2, chunk_dims);

            for (int i=1; i<=DET_NUM; ++i){
                // 创建数据集
                H5::Group cfgGroup = mfH5Spectrum->createGroup(QString("Detector#%1").arg(i).toStdString());
                H5::DataSet dataset = cfgGroup.createDataSet("Spectrum", H5::PredType::NATIVE_UINT, dataspace, prop_list);
                mSpectrumDataset[i-1] = dataset;
            }

            // for (int i=1; i<=DET_NUM; ++i){
            //     // 创建数据集
            //     H5::DataSet dataset = mSpectrumDataset[i-1];

            //     // 获取当前维度
            //     H5::DataSpace file_space = dataset.getSpace();  // 初始文件空间
            //     hsize_t current_dims[2];
            //     file_space.getSimpleExtentDims(current_dims, NULL);

            //     // 1. 扩展数据集维度
            //     current_dims[0] += 1;
            //     dataset.extend(current_dims);

            //     // 2. 重新获取扩展后的文件空间
            //     file_space = dataset.getSpace();  // 关键：更新文件空间

            //     // 3. 定义内存空间（1行数据）
            //     hsize_t mem_dims[2] = {1, columns};
            //     H5::DataSpace mem_space(2, mem_dims);

            //     // 4. 设置写入偏移（新行位置）
            //     hsize_t offset[2] = {current_dims[0] - 1, 0};  // 偏移到最后一行
            //     file_space.selectHyperslab(H5S_SELECT_SET, mem_dims, offset);

            //     // 5. 写入数据（示例：填充i值）
            //     QByteArray data(columns, static_cast<uchar>(i));
            //     dataset.write(data.constData(), H5::PredType::NATIVE_UCHAR, mem_space, file_space);

            //     // 6. 同步当前数据集，防止发生异常，数据丢失
            //     //H5Dflush(dataset.getId());
            //     H5Fflush(dataset.getId(), H5F_SCOPE_GLOBAL);
            // }
        } catch (H5::FileIException& error) {
            error.printErrorStack();
            return ;
        } catch (H5::DataSetIException& error) {
            error.printErrorStack();
            return ;
        } catch (H5::DataSpaceIException& error) {
            error.printErrorStack();
            return ;
        }
    }
}

void HDF5Settings::closeH5Spectrum()
{
    if (mfH5Spectrum)
    {
        for (int i=0;i<DET_NUM; ++i)
            mSpectrumDataset[i].close();
        H5Fflush(mfH5Spectrum->getId(), H5F_SCOPE_GLOBAL);  // 同步文件元数据
        mfH5Spectrum->close();
        delete mfH5Spectrum;
        mfH5Spectrum = nullptr;
    }
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
        char det_Ip_port[16];
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

    return type;
}

// 定义FullSpectrum的HDF5复合类型
H5::CompType HDF5Settings::createH5SpectrumType()
{
    H5::CompType type(sizeof(H5Spectrum));

    // 逐个成员映射（注意内存对齐，需与结构体一致）
    type.insertMember("sequence", offsetof(H5Spectrum, sequence), H5::PredType::NATIVE_UINT32);
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
 * @param index 探测器索引（1~24)
 * @param data 要写入的结构体数据
 */
void HDF5Settings::writeH5Spectrum(quint8 index, const H5Spectrum& data)
{
    QMutexLocker locker(&mWrite_mutex);
    if (!mfH5Spectrum)
        return;

    try {
        // hsize_t columns = 8195;
        hsize_t columns = sizeof(H5Spectrum) / sizeof(quint32);

        // 获取当前维度
        H5::DataSet dataset = mSpectrumDataset[index-1];

        // 获取当前维度
        H5::DataSpace file_space = dataset.getSpace();  // 初始文件空间
        hsize_t current_dims[2];
        file_space.getSimpleExtentDims(current_dims, NULL);

        // 1. 扩展数据集维度
        current_dims[0] += 1;
        dataset.extend(current_dims);

        // 2. 重新获取扩展后的文件空间
        file_space = dataset.getSpace();  // 关键：更新文件空间

        // 3. 定义内存空间（1行数据）
        hsize_t mem_dims[2] = {1, columns};
        H5::DataSpace mem_space(2, mem_dims);

        // 4. 设置写入偏移（新行位置）
        hsize_t offset[2] = {current_dims[0] - 1, 0};  // 偏移到最后一行
        file_space.selectHyperslab(H5S_SELECT_SET, mem_dims, offset);

        // 5. 写入数据（示例：填充i值）
        //QByteArray data(columns, static_cast<uchar>(i));
        dataset.write(&data, H5::PredType::NATIVE_UINT, mem_space, file_space);

        // 强制同步（避免异常丢失）
        //H5Fflush(dataset.getId(), H5F_SCOPE_GLOBAL);
        if (++mSpectrumRef >= 100)
        {
            H5Fflush(mfH5Spectrum->getId(), H5F_SCOPE_GLOBAL);  // 同步文件元数据
            mSpectrumRef = 0;
        }
    } catch (H5::Exception& e) {
        e.printErrorStack();
    }
}

bool HDF5Settings::readAllH5Spectrum(const std::string& filePath, const quint32 detectorId,
    QVector<H5Spectrum>& outData)
{    
    try {
        // 1. 打开文件
        H5::H5File file(filePath, H5F_ACC_RDONLY);

        // 2. 打开分组核数据集
        H5::Group group = file.openGroup(QString("Detector#%1").arg(detectorId).toStdString());
        H5::DataSet dataset = group.openDataSet("Spectrum");

        // 3. 获取数据集维度
        H5::DataSpace file_space = dataset.getSpace();
        hsize_t dims[2] = {0, 0};
        file_space.getSimpleExtentDims(dims, nullptr);

        hsize_t rows    = dims[0];
        hsize_t columns = dims[1];

        if (rows == 0)
            return true;

        // 4. 校验列数（非常重要，防文件结构不一致）
        const hsize_t expectedColumns =
            sizeof(H5Spectrum) / sizeof(quint32);

        if (columns != expectedColumns) {
            qWarning() << "H5Spectrum column mismatch:"
                       << columns << "!=" << expectedColumns;
            return false;
        }

        // 5. 分配输出容器
        outData.resize(rows);

        // 6. 定义内存空间（一次性读所有行）
        hsize_t mem_dims[2] = {rows, columns};
        H5::DataSpace mem_space(2, mem_dims);

        // 7. 一次性读
        dataset.read(outData.data(),
                     H5::PredType::NATIVE_UINT,
                     mem_space,
                     file_space);

        return true;

    } catch (H5::Exception& e) {
        e.printErrorStack();
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

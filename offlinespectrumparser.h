#ifndef OFFLINESPECTRUMPARSER_H
#define OFFLINESPECTRUMPARSER_H

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>

#include "spectypes.h"

/** @brief 从原始字节流中拆出 dtSpectrum 数据包（逻辑同 CommandAdapter::analyzeCommands） */
class SpectrumStreamParser
{
public:
    static int extractSpectrumPackets(QByteArray &cachePool, QVector<QByteArray> &outPackets);
};

/** @brief 将 2 个子包拼成 1024 道完整能谱（逻辑同 DataProcessor::mergeSubSpec_Thread） */
class FullSpectrumAssembler
{
public:
    void reset();

    /** @brief 喂入一个完整子包；拼出完整能谱时返回 true 并写入 completeOut */
    bool ingestPacket(const QByteArray &packet, FullSpectrum *completeOut);

private:
    static bool parseSubSpectrumPacket(const QByteArray &packetData, SubSpectrumPacket &packet);

    QHash<quint32, FullSpectrum> m_pending;
};

/** @brief 离线读取 *_能谱.dat，输出与在线 H5 一致的 H5Spectrum 列表 */
class OfflineSpectrumDatReader
{
public:
    static bool read(const QString &filePath, QVector<H5Spectrum> &outSpectra, QString *errorMessage = nullptr);
};

#endif // OFFLINESPECTRUMPARSER_H

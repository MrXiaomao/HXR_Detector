#include "offlinespectrumparser.h"

#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QtEndian>
#include <cstring>

namespace {

const QByteArray kSpecHead = QByteArray::fromHex("FF FF AA B1");
const QByteArray kSpecTail = QByteArray::fromHex("FF FF CC D1");
constexpr quint16 kDtSpectrum = 0x00D2;
constexpr int kSpectrumPacketSize = 4 + 2 + 4 + 4 + 4 + 2 + 256 * 4 + 4 + 8 + 4;

} // namespace

int SpectrumStreamParser::extractSpectrumPackets(QByteArray &cachePool, QVector<QByteArray> &outPackets)
{
    int count = 0;
    int pos = 0;

    while (cachePool.size() - pos >= static_cast<int>(kSpecHead.size())) {
        if (memcmp(cachePool.constData() + pos, kSpecHead.constData(), kSpecHead.size()) != 0) {
            ++pos;
            continue;
        }

        if (cachePool.size() - pos < 6)
            break;

        const quint16 type = qFromBigEndian<quint16>(
            reinterpret_cast<const uchar *>(cachePool.constData() + pos + 4));
        if (type != kDtSpectrum) {
            pos += 4;
            continue;
        }

        if (cachePool.size() - pos < kSpectrumPacketSize)
            break;

        const char *p = cachePool.constData() + pos;
        if (memcmp(p + kSpectrumPacketSize - 4, kSpecTail.constData(), 4) != 0) {
            pos += 4;
            continue;
        }

        outPackets.append(QByteArray(p, kSpectrumPacketSize));
        pos += kSpectrumPacketSize;
        ++count;
    }

    if (pos > 0)
        cachePool.remove(0, pos);

    return count;
}

void FullSpectrumAssembler::reset()
{
    m_pending.clear();
}

bool FullSpectrumAssembler::parseSubSpectrumPacket(const QByteArray &packetData, SubSpectrumPacket &packet)
{
    const int expectedSize = static_cast<int>(sizeof(SubSpectrumPacket));
    if (packetData.size() < expectedSize)
        return false;

    memcpy(&packet, packetData.constData(), sizeof(SubSpectrumPacket));
    packet.convertNetworkToHost();
    return true;
}

bool FullSpectrumAssembler::ingestPacket(const QByteArray &packet, FullSpectrum *completeOut)
{
    SubSpectrumPacket subSpecPacket;
    if (!parseSubSpectrumPacket(packet, subSpecPacket))
        return false;

    const quint32 spectrumSeq = subSpecPacket.spectrumSeq;
    const quint16 part = subSpecPacket.spectrumSubNo;
    if (part < 1 || part > 32)
        return false;

    auto it = m_pending.find(spectrumSeq);
    if (it == m_pending.end()) {
        FullSpectrum newSpectrum;
        newSpectrum.sequence = spectrumSeq;
        newSpectrum.timeUTCs = subSpecPacket.timeUTCs;
        newSpectrum.timeUTCms = subSpecPacket.timeUTCms;
        newSpectrum.measureTime = subSpecPacket.measureTime;
        newSpectrum.deathTime = subSpecPacket.deathTime;
        it = m_pending.insert(spectrumSeq, newSpectrum);
    }

    FullSpectrum &fullSpectrum = it.value();
    const quint32 bit = 1u << (part - 1);
    if (fullSpectrum.receivedMask & bit)
        return false;

    memcpy(fullSpectrum.spectrum + (part - 1) * 256, subSpecPacket.spectrum, 256 * 4);
    fullSpectrum.receivedMask |= bit;

    if (fullSpectrum.receivedMask != 0xFFFFFFFFu)
        return false;

    fullSpectrum.isComplete = true;
    if (completeOut) {
        *completeOut = fullSpectrum;
        completeOut->spectrum[8191] = 0;
    }
    m_pending.remove(spectrumSeq);
    return true;
}

bool OfflineSpectrumDatReader::read(const QString &filePath, QVector<H5Spectrum> &outSpectra, QString *errorMessage)
{
    outSpectra.clear();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage)
            *errorMessage = QObject::tr("无法打开文件：%1").arg(filePath);
        return false;
    }

    constexpr qint64 kReadChunk = 10 * 1024 * 1024;
    QByteArray cache;
    cache.reserve(kReadChunk);
    FullSpectrumAssembler assembler;

    auto consumeCache = [&](QVector<H5Spectrum> &target) {
        QVector<QByteArray> packets;
        SpectrumStreamParser::extractSpectrumPackets(cache, packets);
        for (const QByteArray &packet : packets) {
            FullSpectrum complete;
            if (assembler.ingestPacket(packet, &complete))
                target.append(FullSpectoH5Spec(complete));
        }
    };

    while (!file.atEnd()) {
        cache.append(file.read(kReadChunk));
        consumeCache(outSpectra);
    }
    consumeCache(outSpectra);

    file.close();

    if (outSpectra.isEmpty()) {
        if (errorMessage)
            *errorMessage = QObject::tr("文件中未解析到完整能谱：%1").arg(QFileInfo(filePath).fileName());
        return false;
    }

    return true;
}

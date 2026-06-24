#ifndef NEUTRONYIELDPARSEWORKER_H
#define NEUTRONYIELDPARSEWORKER_H

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QMap>
#include <QObject>
#include <QStringList>

#include <atomic>

#include "parsedata.h"

struct NeutronYieldParseJob {
    QStringList h5FilePaths;
    QList<int> detIds;
    QDateTime fusionTime;
    QDateTime windowLeft;
    QDateTime windowRight;
    int timeStepSec = 0;
    QMap<int, int> beginTimeByDet;
    bool perChannelMode = false;
    double spectrumFitR2Min = CurveFit::kDefaultSpectrumFitR2Min; //谱峰拟合R2最小值
};

class NeutronYieldParseWorker : public QObject
{
    Q_OBJECT

public:
    explicit NeutronYieldParseWorker(QObject *parent = nullptr);
    ~NeutronYieldParseWorker() override;

    void requestCancel();
    ParseData *takeParseData(int detId);

public slots:
    void run(const NeutronYieldParseJob &job);

signals:
    void progress(int value, int maximum, const QString &message);
    void logMessage(const QString &msg, QtMsgType type = QtInfoMsg);
    void channelFinished(int detId, bool mergeOk, const QStringList &fitErrors);
    void finished(bool cancelled, const QString &summaryMessage);

private:
    std::atomic<bool> mCancelled{false};
    QHash<int, ParseData *> mParseResults;
};

#endif // NEUTRONYIELDPARSEWORKER_H

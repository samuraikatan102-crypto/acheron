#pragma once

#include <QByteArray>

#include <opus.h>

namespace Acheron {
namespace Core {
namespace Audio {

class OpusEncoder
{
public:
    OpusEncoder();
    ~OpusEncoder();

    OpusEncoder(const OpusEncoder &) = delete;
    OpusEncoder &operator=(const OpusEncoder &) = delete;

    bool init(int sampleRate, int channels, int application = OPUS_APPLICATION_VOIP);

    QByteArray encode(const QByteArray &pcmFrame);

    void setBitrate(int bitrate);
    void setComplexity(int complexity);
    void setSignalType(int signalType);
    void setFec(bool enabled);
    void setDtx(bool enabled);
    void setPacketLossPercent(int percent);

private:
    // libopus refuses anything over 300 kbps per channel, so setBitrate gets past
    // that by spending a bigger packet instead of a bigger target rate: the class
    // can then honestly follow the number the user typed.
    static constexpr int MaxPacketBytes = 4000;
    static constexpr int CodecBitratePerChannel = 300000;

    ::OpusEncoder *encoder = nullptr;
    int frameSamples = 0;
    int frameChannels = 0;
    int packetBudget = MaxPacketBytes;
};

} // namespace Audio
} // namespace Core
} // namespace Acheron

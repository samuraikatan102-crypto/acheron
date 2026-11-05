#pragma once

#include <QByteArray>

namespace Acheron {
namespace Core {
namespace Audio {

class VoiceManager;

// Every voice knob is read and written here so the settings page, the F2/F3
// console and a brand new connection all agree on one number.
class AudioSettings
{
public:
    static constexpr double MaxGain = 100.0;
    static constexpr double MaxVolume = 100.0;
    static constexpr int MinBitrate = 8000;
    static constexpr int MaxBitrate = 1000000;

    static double inputGain();
    static void setInputGain(double gain);

    static double outputVolume();
    static void setOutputVolume(double volume);

    static double vadThreshold();
    static void setVadThreshold(double threshold);

    [[nodiscard]] static int bitrate();
    static void setBitrate(int bitsPerSecond);

    static int complexity();
    static void setComplexity(int value);

    static int packetLossPercent();
    static void setPacketLossPercent(int value);

    static bool fec();
    static void setFec(bool enabled);

    static int application();
    static void setApplication(int value);

    static int signalType();
    static void setSignalType(int value);

    static bool noiseSuppression();
    static void setNoiseSuppression(bool enabled);

    static bool rnnoiseVad();
    static void setRnnoiseVad(bool enabled);

    static QByteArray inputDevice();
    static void setInputDevice(const QByteArray &deviceId);

    static QByteArray outputDevice();
    static void setOutputDevice(const QByteArray &deviceId);

    static double clampGain(double value);
    static int clampBitrate(int bitsPerSecond);

    static void applyTo(VoiceManager *manager);
};

} // namespace Audio
} // namespace Core
} // namespace Acheron

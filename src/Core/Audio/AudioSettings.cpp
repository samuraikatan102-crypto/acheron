#include "AudioSettings.hpp"

#include <QByteArray>
#include <QSettings>
#include <QString>

#include <cmath>

#ifndef ACHERON_NO_VOICE
#include "Core/Audio/VoiceManager.hpp"
#endif

namespace Acheron {
namespace Core {
namespace Audio {

namespace {

constexpr char LegacyKey[] = "audio/settings_version";
constexpr int CurrentVersion = 1;

// opus constants, kept as numbers so this file builds without the opus headers
constexpr int DefaultApplication = 2048; // OPUS_APPLICATION_VOIP
constexpr int DefaultSignalType = 3001;  // OPUS_SIGNAL_VOICE

double clampUnit(double value)
{
    return std::isfinite(value) ? qBound(0.0, value, AudioSettings::MaxGain) : 1.0;
}

bool migrated = false;

void migrateLegacyValues()
{
    if (migrated)
        return;
    migrated = true;

    QSettings settings;
    if (settings.value(QLatin1String(LegacyKey)).toInt() >= CurrentVersion)
        return;

    struct Rename
    {
        const char *from;
        const char *to;
        double scale; // the old page stored percents and kilobits
    };

    const Rename renames[] = {
        { "voice/input_gain", "audio/input_gain", 0.01 },
        { "voice/output_volume", "audio/output_volume", 0.01 },
        { "voice/vad_threshold", "audio/vad_threshold", 1.0 },
        { "voice/codec/bitrate", "audio/bitrate", 1000.0 },
        { "voice/codec/complexity", "audio/complexity", 1.0 },
        { "voice/codec/packet_loss", "audio/packet_loss", 1.0 },
        { "voice/codec/fec", "audio/fec", 1.0 },
        { "voice/codec/application", "audio/application", 1.0 },
        { "voice/codec/signal_type", "audio/signal_type", 1.0 },
        { "voice/noise_suppression", "audio/noise_suppression", 1.0 },
        { "voice/rnnoise_vad", "audio/rnnoise_vad", 1.0 },
    };

    for (const Rename &rename : renames) {
        if (!settings.contains(QLatin1String(rename.from)))
            continue;

        const QVariant stored = settings.value(QLatin1String(rename.from));
        if (rename.scale == 1.0)
            settings.setValue(QLatin1String(rename.to), stored);
        else
            settings.setValue(QLatin1String(rename.to), stored.toDouble() * rename.scale);
        settings.remove(QLatin1String(rename.from));
    }

    settings.setValue(QLatin1String(LegacyKey), CurrentVersion);
}

double readUnit(const char *key, double fallback)
{
    migrateLegacyValues();
    return clampUnit(QSettings().value(QLatin1String(key), fallback).toDouble());
}

void writeUnit(const char *key, double value)
{
    migrateLegacyValues();
    QSettings().setValue(QLatin1String(key), clampUnit(value));
}

int readInt(const char *key, int fallback)
{
    migrateLegacyValues();
    return QSettings().value(QLatin1String(key), fallback).toInt();
}

void writeInt(const char *key, int value)
{
    migrateLegacyValues();
    QSettings().setValue(QLatin1String(key), value);
}

bool readBool(const char *key, bool fallback)
{
    migrateLegacyValues();
    return QSettings().value(QLatin1String(key), fallback).toBool();
}

void writeBool(const char *key, bool value)
{
    migrateLegacyValues();
    QSettings().setValue(QLatin1String(key), value);
}

QByteArray readDevice(const char *key)
{
    migrateLegacyValues();
    return QByteArray::fromBase64(QSettings().value(QLatin1String(key)).toByteArray());
}

void writeDevice(const char *key, const QByteArray &deviceId)
{
    migrateLegacyValues();
    QSettings().setValue(QLatin1String(key), deviceId.toBase64());
}

} // namespace

double AudioSettings::inputGain()
{
    return readUnit("audio/input_gain", 1.0);
}

void AudioSettings::setInputGain(double gain)
{
    writeUnit("audio/input_gain", gain);
}

double AudioSettings::outputVolume()
{
    return readUnit("audio/output_volume", 1.0);
}

void AudioSettings::setOutputVolume(double volume)
{
    writeUnit("audio/output_volume", volume);
}

double AudioSettings::vadThreshold()
{
    migrateLegacyValues();
    const double threshold = QSettings().value("audio/vad_threshold", 100.0).toDouble();
    return std::isfinite(threshold) ? qBound(0.0, threshold, 2000.0) : 100.0;
}

void AudioSettings::setVadThreshold(double threshold)
{
    migrateLegacyValues();
    QSettings().setValue("audio/vad_threshold",
                         std::isfinite(threshold) ? qBound(0.0, threshold, 2000.0) : 100.0);
}

int AudioSettings::bitrate()
{
    migrateLegacyValues();
    return clampBitrate(readInt("audio/bitrate", 64000));
}

void AudioSettings::setBitrate(int bitsPerSecond)
{
    writeInt("audio/bitrate", clampBitrate(bitsPerSecond));
}

int AudioSettings::complexity()
{
    return qBound(0, readInt("audio/complexity", 5), 10);
}

void AudioSettings::setComplexity(int value)
{
    writeInt("audio/complexity", qBound(0, value, 10));
}

int AudioSettings::packetLossPercent()
{
    return qBound(0, readInt("audio/packet_loss", 0), 100);
}

void AudioSettings::setPacketLossPercent(int value)
{
    writeInt("audio/packet_loss", qBound(0, value, 100));
}

bool AudioSettings::fec()
{
    return readBool("audio/fec", true);
}

void AudioSettings::setFec(bool enabled)
{
    writeBool("audio/fec", enabled);
}

int AudioSettings::application()
{
    return readInt("audio/application", DefaultApplication);
}

void AudioSettings::setApplication(int value)
{
    writeInt("audio/application", value);
}

int AudioSettings::signalType()
{
    return readInt("audio/signal_type", DefaultSignalType);
}

void AudioSettings::setSignalType(int value)
{
    writeInt("audio/signal_type", value);
}

bool AudioSettings::noiseSuppression()
{
    return readBool("audio/noise_suppression", true);
}

void AudioSettings::setNoiseSuppression(bool enabled)
{
    writeBool("audio/noise_suppression", enabled);
}

bool AudioSettings::rnnoiseVad()
{
    return readBool("audio/rnnoise_vad", true);
}

void AudioSettings::setRnnoiseVad(bool enabled)
{
    writeBool("audio/rnnoise_vad", enabled);
}

QByteArray AudioSettings::inputDevice()
{
    return readDevice("audio/input_device");
}

void AudioSettings::setInputDevice(const QByteArray &deviceId)
{
    writeDevice("audio/input_device", deviceId);
}

QByteArray AudioSettings::outputDevice()
{
    return readDevice("audio/output_device");
}

void AudioSettings::setOutputDevice(const QByteArray &deviceId)
{
    writeDevice("audio/output_device", deviceId);
}

double AudioSettings::clampGain(double value)
{
    return clampUnit(value);
}

int AudioSettings::clampBitrate(int bitsPerSecond)
{
    return qBound(MinBitrate, bitsPerSecond, MaxBitrate);
}

void AudioSettings::applyTo(VoiceManager *manager)
{
#ifdef ACHERON_NO_VOICE
    Q_UNUSED(manager);
#else
    if (!manager)
        return;

    manager->setInputGain(static_cast<float>(inputGain()));
    manager->setOutputVolume(static_cast<float>(outputVolume()));
    manager->setVadThreshold(static_cast<float>(vadThreshold()));
    manager->setOpusBitrate(bitrate());
    manager->setOpusComplexity(complexity());
    manager->setOpusPacketLossPercent(packetLossPercent());
    manager->setOpusFec(fec());
    manager->setOpusApplication(application());
    manager->setOpusSignalType(signalType());
    manager->setNoiseSuppressionEnabled(noiseSuppression());
    manager->setUseRnnoiseVad(rnnoiseVad());

    const QByteArray input = inputDevice();
    if (!input.isEmpty())
        manager->setInputDevice(input);

    const QByteArray output = outputDevice();
    if (!output.isEmpty())
        manager->setOutputDevice(output);
#endif
}

} // namespace Audio
} // namespace Core
} // namespace Acheron

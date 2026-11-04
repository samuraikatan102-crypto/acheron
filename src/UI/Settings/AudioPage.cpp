#include "AudioPage.hpp"

#include "Core/Audio/AudioBackends.hpp"

#ifndef ACHERON_NO_VOICE
#include "Core/Audio/VoiceManager.hpp"
#include "UI/VolumeMeter.hpp"
#endif

#ifdef ACHERON_HAVE_OPUS
#include <opus.h>
#endif

#include <QCheckBox>
#include <QComboBox>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QSlider>
#include <QVBoxLayout>

namespace Acheron {
namespace UI {

namespace {

#ifdef ACHERON_HAVE_OPUS
struct Option
{
    const char *label;
    int value;
};
#endif

QLabel *sectionTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("SettingsSectionTitle"));
    return label;
}

QLabel *fieldLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("SettingsFieldLabel"));
    return label;
}

} // namespace

AudioPage::AudioPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 12, 24, 24);
    layout->setSpacing(6);

#ifndef ACHERON_NO_VOICE
    layout->addWidget(sectionTitle(tr("Voice Devices"), this), 0);

    layout->addWidget(fieldLabel(tr("Input Device"), this), 0);
    inputDeviceCombo = new QComboBox(this);
    layout->addWidget(inputDeviceCombo, 0);

    layout->addWidget(fieldLabel(tr("Output Device"), this), 0);
    outputDeviceCombo = new QComboBox(this);
    layout->addWidget(outputDeviceCombo, 0);

    layout->addWidget(fieldLabel(tr("Microphone Test"), this), 0);
    volumeMeter = new VolumeMeter(this);
    volumeMeter->setThreshold(100.0f);
    layout->addWidget(volumeMeter, 0);

    deviceHint = new QLabel(tr("Speak to make the bar move. Device changes apply right away."),
                            this);
    deviceHint->setObjectName(QStringLiteral("SettingsPageHint"));
    deviceHint->setWordWrap(true);
    layout->addWidget(deviceHint, 0);

    layout->addSpacing(12);

    connect(inputDeviceCombo, qOverload<int>(&QComboBox::activated), this, [this](int index) {
        if (voiceManager)
            voiceManager->setInputDevice(inputDeviceCombo->itemData(index).toByteArray());
    });

    connect(outputDeviceCombo, qOverload<int>(&QComboBox::activated), this, [this](int index) {
        if (voiceManager)
            voiceManager->setOutputDevice(outputDeviceCombo->itemData(index).toByteArray());
    });
#endif

    layout->addWidget(sectionTitle(tr("Audio Backend"), this), 0);

    backendCombo = new QComboBox(this);
    backendCombo->addItem(tr("Automatic"), QString());
    for (const QString &name : Core::Audio::supportedAudioBackends())
        backendCombo->addItem(name, name);

    const int current = backendCombo->findData(Core::Audio::configuredAudioBackend());
    backendCombo->setCurrentIndex(current >= 0 ? current : 0);
    layout->addWidget(backendCombo, 0);

    auto *backendNote = new QLabel(tr("* Takes effect after a restart."), this);
    QFont noteFont = backendNote->font();
    noteFont.setItalic(true);
    if (noteFont.pointSize() > 1)
        noteFont.setPointSize(noteFont.pointSize() - 1);
    backendNote->setFont(noteFont);
    layout->addWidget(backendNote, 0);

    layout->addSpacing(12);
    layout->addWidget(sectionTitle(tr("Levels"), this), 0);

    auto addSlider = [this, layout](const QString &title, const QString &key, int fallback, int min,
                                   int max, const QString &suffix) {
        layout->addWidget(fieldLabel(title, this), 0);

        auto *row = new QHBoxLayout();
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(8);

        auto *slider = new QSlider(Qt::Horizontal, this);
        slider->setRange(min, max);
        slider->setValue(QSettings().value(key, fallback).toInt());

        auto *valueLabel = new QLabel(this);
        valueLabel->setFixedWidth(64);
        valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        valueLabel->setText(QString::number(slider->value()) + suffix);

        row->addWidget(slider, 1);
        row->addWidget(valueLabel, 0);
        layout->addLayout(row, 0);

        connect(slider, &QSlider::valueChanged, this, [this, slider, valueLabel, key, suffix]() {
            const int value = slider->value();
            valueLabel->setText(QString::number(value) + suffix);
            QSettings().setValue(key, value);

#ifndef ACHERON_NO_VOICE
            if (voiceManager) {
                if (key == QLatin1String("voice/input_gain"))
                    voiceManager->setInputGain(static_cast<float>(value) / 100.0f);
                else if (key == QLatin1String("voice/output_volume"))
                    voiceManager->setOutputVolume(static_cast<float>(value) / 100.0f);
                else if (key == QLatin1String("voice/vad_threshold")) {
                    voiceManager->setVadThreshold(static_cast<float>(value));
                    if (volumeMeter)
                        volumeMeter->setThreshold(static_cast<float>(value));
                } else if (key == QLatin1String("voice/codec/bitrate"))
                    voiceManager->setOpusBitrate(value * 1000);
                else if (key == QLatin1String("voice/codec/complexity"))
                    voiceManager->setOpusComplexity(value);
                else if (key == QLatin1String("voice/codec/packet_loss"))
                    voiceManager->setOpusPacketLossPercent(value);
            }
#endif

            emit voiceSettingsChanged();
        });
    };

    auto addCheck = [this, layout](const QString &title, const QString &key, bool fallback) {
        auto *check = new QCheckBox(title, this);
        check->setChecked(QSettings().value(key, fallback).toBool());
        connect(check, &QCheckBox::toggled, this, [this, key](bool checked) {
            QSettings().setValue(key, checked);

#ifndef ACHERON_NO_VOICE
            if (voiceManager) {
                if (key == QLatin1String("voice/codec/fec"))
                    voiceManager->setOpusFec(checked);
                else if (key == QLatin1String("voice/noise_suppression"))
                    voiceManager->setNoiseSuppressionEnabled(checked);
                else if (key == QLatin1String("voice/rnnoise_vad"))
                    voiceManager->setUseRnnoiseVad(checked);
            }
#endif

            emit voiceSettingsChanged();
        });
        layout->addWidget(check, 0);
    };

    addSlider(tr("Input Gain"), QStringLiteral("voice/input_gain"), 100, 0, 200,
              QStringLiteral("%"));
    addSlider(tr("Output Volume"), QStringLiteral("voice/output_volume"), 100, 0, 200,
              QStringLiteral("%"));
    addSlider(tr("Open Mic Sensitivity"), QStringLiteral("voice/vad_threshold"), 100, 0, 2000,
              QString());

#ifdef ACHERON_HAVE_OPUS
    auto addCombo = [this, layout](const QString &title, const QString &key, int fallback,
                                   const QList<Option> &options) {
        layout->addWidget(fieldLabel(title, this), 0);

        auto *combo = new QComboBox(this);
        for (const Option &option : options)
            combo->addItem(tr(option.label), option.value);
        const int index = combo->findData(QSettings().value(key, fallback).toInt());
        combo->setCurrentIndex(index >= 0 ? index : 0);

        layout->addWidget(combo, 0);

        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this, combo, key]() {
                    const int value = combo->currentData().toInt();
                    QSettings().setValue(key, value);

#ifndef ACHERON_NO_VOICE
                    if (voiceManager) {
                        if (key == QLatin1String("voice/codec/application"))
                            voiceManager->setOpusApplication(value);
                        else if (key == QLatin1String("voice/codec/signal_type"))
                            voiceManager->setOpusSignalType(value);
                    }
#endif

                    emit voiceSettingsChanged();
                });
    };

    layout->addSpacing(12);
    layout->addWidget(sectionTitle(tr("Voice Processing"), this), 0);

    addCombo(tr("Audio Quality"), QStringLiteral("voice/codec/application"),
             OPUS_APPLICATION_VOIP, { { "Speech", OPUS_APPLICATION_VOIP },
                                     { "Music", OPUS_APPLICATION_AUDIO } });
    addCombo(tr("Encoding Mode"), QStringLiteral("voice/codec/signal_type"), OPUS_SIGNAL_VOICE,
             { { "Automatic", OPUS_AUTO }, { "Voice", OPUS_SIGNAL_VOICE },
               { "Music", OPUS_SIGNAL_MUSIC } });
    addSlider(tr("Bitrate"), QStringLiteral("voice/codec/bitrate"), 64, 8, 510,
              QStringLiteral(" kbps"));
    addSlider(tr("Encoder Complexity"), QStringLiteral("voice/codec/complexity"), 5, 0, 10,
              QString());
    addCheck(tr("Forward Error Correction"), QStringLiteral("voice/codec/fec"), true);
    addSlider(tr("Packet Loss Recovery"), QStringLiteral("voice/codec/packet_loss"), 0, 0, 100,
              QStringLiteral("%"));
#endif

#ifdef ACHERON_HAVE_RNNOISE
    addCheck(tr("Noise Suppression"), QStringLiteral("voice/noise_suppression"), true);
    addCheck(tr("Use Noise Suppression For Activation Detection"),
             QStringLiteral("voice/rnnoise_vad"), true);
#endif

    layout->addStretch(1);

    connect(backendCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        Core::Audio::setConfiguredAudioBackend(backendCombo->itemData(index).toString());
    });
}

#ifndef ACHERON_NO_VOICE
void AudioPage::setVoiceManager(Core::Audio::VoiceManager *manager)
{
    if (voiceManager == manager)
        return;

    if (voiceManager) {
        disconnect(voiceManager, nullptr, this, nullptr);
        disconnect(voiceManager, nullptr, volumeMeter, nullptr);
    }

    voiceManager = manager;

    if (!voiceManager) {
        inputDeviceCombo->clear();
        outputDeviceCombo->clear();
        return;
    }

    connect(voiceManager, &Core::Audio::VoiceManager::devicesChanged, this,
            &AudioPage::refreshDevices);
    connect(voiceManager, &Core::Audio::VoiceManager::audioLevelChanged, volumeMeter,
            &VolumeMeter::setLevel);

    refreshDevices();
}

void AudioPage::refreshDevices()
{
    if (!voiceManager)
        return;

    auto populate = [this](QComboBox *combo, const QList<Core::Audio::AudioDeviceInfo> &devices,
                           const QByteArray &currentId) {
        combo->blockSignals(true);
        combo->clear();
        int selected = 0;
        for (int i = 0; i < devices.size(); ++i) {
            const Core::Audio::AudioDeviceInfo &device = devices.at(i);
            QString label = device.description;
            if (device.isDefault)
                label += tr(" (Default)");
            combo->addItem(label, device.id);

            if (!currentId.isEmpty() && device.id == currentId)
                selected = i;
            else if (currentId.isEmpty() && device.isDefault)
                selected = i;
        }
        combo->setCurrentIndex(selected);
        combo->blockSignals(false);
    };

    populate(inputDeviceCombo, voiceManager->availableInputDevices(),
             voiceManager->currentInputDevice());
    populate(outputDeviceCombo, voiceManager->availableOutputDevices(),
             voiceManager->currentOutputDevice());
}
#else
void AudioPage::setVoiceManager(Core::Audio::VoiceManager *) {}

void AudioPage::refreshDevices() {}
#endif

} // namespace UI
} // namespace Acheron

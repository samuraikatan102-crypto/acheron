#include "AudioPage.hpp"

#include "Core/Audio/AudioBackends.hpp"
#include "Core/Audio/AudioSettings.hpp"

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

// Each knob reaches the settings service and the live manager through one id,
// so the sliders, the F2/F3 console and a new connection cannot drift apart.
enum class Control
{
    InputGain,
    OutputVolume,
    VadThreshold,
    Bitrate,
    Complexity,
    PacketLoss,
    Fec,
    Application,
    SignalType,
    NoiseSuppression,
    RnnoiseVad
};

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

// the value as the slider wants to see it
int sliderValue(Control control)
{
    using Core::Audio::AudioSettings;

    switch (control) {
    case Control::InputGain:
        return qMax(0, qRound(AudioSettings::inputGain()));
    case Control::OutputVolume:
        return qMax(0, qRound(AudioSettings::outputVolume()));
    case Control::VadThreshold:
        return qRound(AudioSettings::vadThreshold());
    case Control::Bitrate:
        return AudioSettings::bitrate() / 1000;
    case Control::Complexity:
        return AudioSettings::complexity();
    case Control::PacketLoss:
        return AudioSettings::packetLossPercent();
    default:
        return 0;
    }
}

void storeSlider(Control control, int value)
{
    using Core::Audio::AudioSettings;

    switch (control) {
    case Control::InputGain:
        AudioSettings::setInputGain(value);
        break;
    case Control::OutputVolume:
        AudioSettings::setOutputVolume(value);
        break;
    case Control::VadThreshold:
        AudioSettings::setVadThreshold(value);
        break;
    case Control::Bitrate:
        AudioSettings::setBitrate(value * 1000);
        break;
    case Control::Complexity:
        AudioSettings::setComplexity(value);
        break;
    case Control::PacketLoss:
        AudioSettings::setPacketLossPercent(value);
        break;
    default:
        break;
    }
}

[[maybe_unused]] void storeCheck(Control control, bool checked)
{
    using Core::Audio::AudioSettings;

    switch (control) {
    case Control::Fec:
        AudioSettings::setFec(checked);
        break;
    case Control::NoiseSuppression:
        AudioSettings::setNoiseSuppression(checked);
        break;
    case Control::RnnoiseVad:
        AudioSettings::setRnnoiseVad(checked);
        break;
    default:
        break;
    }
}

[[maybe_unused]] void storeCombo(Control control, int value)
{
    switch (control) {
    case Control::Application:
        Core::Audio::AudioSettings::setApplication(value);
        break;
    case Control::SignalType:
        Core::Audio::AudioSettings::setSignalType(value);
        break;
    default:
        break;
    }
}

bool checkValue(Control control)
{
    using Core::Audio::AudioSettings;

    switch (control) {
    case Control::Fec:
        return AudioSettings::fec();
    case Control::NoiseSuppression:
        return AudioSettings::noiseSuppression();
    case Control::RnnoiseVad:
        return AudioSettings::rnnoiseVad();
    default:
        return false;
    }
}

[[maybe_unused]] int comboValue(Control control, int fallback)
{
    using Core::Audio::AudioSettings;

    const int stored = control == Control::Application ? AudioSettings::application()
                                                     : AudioSettings::signalType();
    return stored == 0 ? fallback : stored;
}

#ifndef ACHERON_NO_VOICE
void pushToVoiceManager(Core::Audio::VoiceManager *manager, Control control)
{
    if (!manager)
        return;

    using Core::Audio::AudioSettings;

    switch (control) {
    case Control::InputGain:
        manager->setInputGain(static_cast<float>(AudioSettings::inputGain()));
        break;
    case Control::OutputVolume:
        manager->setOutputVolume(static_cast<float>(AudioSettings::outputVolume()));
        break;
    case Control::VadThreshold:
        manager->setVadThreshold(static_cast<float>(AudioSettings::vadThreshold()));
        break;
    case Control::Bitrate:
        manager->setOpusBitrate(AudioSettings::bitrate());
        break;
    case Control::Complexity:
        manager->setOpusComplexity(AudioSettings::complexity());
        break;
    case Control::PacketLoss:
        manager->setOpusPacketLossPercent(AudioSettings::packetLossPercent());
        break;
    case Control::Fec:
        manager->setOpusFec(AudioSettings::fec());
        break;
    case Control::Application:
        manager->setOpusApplication(AudioSettings::application());
        break;
    case Control::SignalType:
        manager->setOpusSignalType(AudioSettings::signalType());
        break;
    case Control::NoiseSuppression:
        manager->setNoiseSuppressionEnabled(AudioSettings::noiseSuppression());
        break;
    case Control::RnnoiseVad:
        manager->setUseRnnoiseVad(AudioSettings::rnnoiseVad());
        break;
    }
}
#endif

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
    volumeMeter->setThreshold(static_cast<float>(Core::Audio::AudioSettings::vadThreshold()));
    layout->addWidget(volumeMeter, 0);

    deviceHint = new QLabel(tr("Speak to make the bar move. Device changes apply right away and are "
                              "remembered. F2 sets the gain, F3 the bitrate."),
                            this);
    deviceHint->setObjectName(QStringLiteral("SettingsPageHint"));
    deviceHint->setWordWrap(true);
    layout->addWidget(deviceHint, 0);

    layout->addSpacing(12);

    connect(inputDeviceCombo, qOverload<int>(&QComboBox::activated), this, [this](int index) {
        const QByteArray id = inputDeviceCombo->itemData(index).toByteArray();
        Core::Audio::AudioSettings::setInputDevice(id);
        if (voiceManager)
            voiceManager->setInputDevice(id);
    });

    connect(outputDeviceCombo, qOverload<int>(&QComboBox::activated), this, [this](int index) {
        const QByteArray id = outputDeviceCombo->itemData(index).toByteArray();
        Core::Audio::AudioSettings::setOutputDevice(id);
        if (voiceManager)
            voiceManager->setOutputDevice(id);
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

    auto addSlider = [this, layout](const QString &title, Control control, int min, int max,
                                   const QString &suffix) {
        layout->addWidget(fieldLabel(title, this), 0);

        auto *row = new QHBoxLayout();
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(8);

        auto *slider = new QSlider(Qt::Horizontal, this);
        slider->setRange(min, max);
        slider->setValue(qBound(min, sliderValue(control), max));

        auto *valueLabel = new QLabel(this);
        valueLabel->setFixedWidth(64);
        valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        valueLabel->setText(QString::number(slider->value()) + suffix);

        row->addWidget(slider, 1);
        row->addWidget(valueLabel, 0);
        layout->addLayout(row, 0);

        connect(slider, &QSlider::valueChanged, this, [this, slider, valueLabel, control, suffix]() {
            const int value = slider->value();
            valueLabel->setText(QString::number(value) + suffix);
            storeSlider(control, value);

#ifndef ACHERON_NO_VOICE
            pushToVoiceManager(voiceManager, control);
            if (control == Control::VadThreshold && volumeMeter)
                volumeMeter->setThreshold(static_cast<float>(value));
#endif

            emit voiceSettingsChanged();
        });
    };

    auto addCheck = [this, layout](const QString &title, Control control) {
        auto *check = new QCheckBox(title, this);
        check->setChecked(checkValue(control));
        connect(check, &QCheckBox::toggled, this, [this, control](bool checked) {
            storeCheck(control, checked);

#ifndef ACHERON_NO_VOICE
            pushToVoiceManager(voiceManager, control);
#endif

            emit voiceSettingsChanged();
        });
        layout->addWidget(check, 0);
    };

    addSlider(tr("Input Gain"), Control::InputGain, 0,
              static_cast<int>(Core::Audio::AudioSettings::MaxGain), QStringLiteral(" ×"));
    addSlider(tr("Output Volume"), Control::OutputVolume, 0,
              static_cast<int>(Core::Audio::AudioSettings::MaxVolume), QStringLiteral(" ×"));
    addSlider(tr("Open Mic Sensitivity"), Control::VadThreshold, 0, 2000, QString());

#ifdef ACHERON_HAVE_OPUS
    auto addCombo = [this, layout](const QString &title, Control control, int fallback,
                                  const QList<Option> &options) {
        layout->addWidget(fieldLabel(title, this), 0);

        auto *combo = new QComboBox(this);
        for (const Option &option : options)
            combo->addItem(tr(option.label), option.value);
        const int index = combo->findData(comboValue(control, fallback));
        combo->setCurrentIndex(index >= 0 ? index : 0);

        layout->addWidget(combo, 0);

        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, combo, control]() {
            const int value = combo->currentData().toInt();
            storeCombo(control, value);

#ifndef ACHERON_NO_VOICE
            pushToVoiceManager(voiceManager, control);
#endif

            emit voiceSettingsChanged();
        });
    };

    layout->addSpacing(12);
    layout->addWidget(sectionTitle(tr("Voice Processing"), this), 0);

    addCombo(tr("Audio Quality"), Control::Application, OPUS_APPLICATION_VOIP,
             { { "Speech", OPUS_APPLICATION_VOIP }, { "Music", OPUS_APPLICATION_AUDIO } });
    addCombo(tr("Encoding Mode"), Control::SignalType, OPUS_SIGNAL_VOICE,
             { { "Automatic", OPUS_AUTO }, { "Voice", OPUS_SIGNAL_VOICE },
               { "Music", OPUS_SIGNAL_MUSIC } });
    addSlider(tr("Bitrate"), Control::Bitrate,
              Core::Audio::AudioSettings::MinBitrate / 1000,
              Core::Audio::AudioSettings::MaxBitrate / 1000, QStringLiteral(" kbps"));
    addSlider(tr("Encoder Complexity"), Control::Complexity, 0, 10, QString());
    addCheck(tr("Forward Error Correction"), Control::Fec);
    addSlider(tr("Packet Loss Recovery"), Control::PacketLoss, 0, 100, QStringLiteral("%"));
#endif

#ifdef ACHERON_HAVE_RNNOISE
    addCheck(tr("Noise Suppression"), Control::NoiseSuppression);
    addCheck(tr("Use Noise Suppression For Activation Detection"), Control::RnnoiseVad);
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

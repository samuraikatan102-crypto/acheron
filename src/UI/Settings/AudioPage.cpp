#include "AudioPage.hpp"

#include "Core/Audio/AudioBackends.hpp"

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

} // namespace

AudioPage::AudioPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);

    auto *backendRow = new QHBoxLayout();
    backendRow->addWidget(new QLabel(tr("Backend *"), this));
    backendRow->addStretch(1);

    backendCombo = new QComboBox(this);
    backendCombo->addItem(tr("Automatic"), QString());
    for (const QString &name : Core::Audio::supportedAudioBackends())
        backendCombo->addItem(name, name);

    const int current = backendCombo->findData(Core::Audio::configuredAudioBackend());
    backendCombo->setCurrentIndex(current >= 0 ? current : 0);

    backendRow->addWidget(backendCombo);
    layout->addLayout(backendRow);

    auto addSlider = [this, layout](const QString &title, const QString &key, int fallback, int min, int max,
                                   const QString &suffix) {
        auto *row = new QHBoxLayout();
        row->addWidget(new QLabel(title, this));

        auto *slider = new QSlider(Qt::Horizontal, this);
        slider->setRange(min, max);
        slider->setValue(QSettings().value(key, fallback).toInt());
        slider->setMaximumWidth(160);

        auto *valueLabel = new QLabel(this);
        valueLabel->setFixedWidth(56);
        valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        valueLabel->setText(QString::number(slider->value()) + suffix);

        connect(slider, &QSlider::valueChanged, this, [this, valueLabel, key, suffix](int value) {
            valueLabel->setText(QString::number(value) + suffix);
            QSettings().setValue(key, value);
            emit voiceSettingsChanged();
        });

        row->addWidget(slider);
        row->addWidget(valueLabel);
        row->addStretch(1);
        layout->addLayout(row);
    };

    auto addCheck = [this, layout](const QString &title, const QString &key, bool fallback) {
        auto *check = new QCheckBox(title, this);
        check->setChecked(QSettings().value(key, fallback).toBool());
        connect(check, &QCheckBox::toggled, this, [this, key](bool checked) {
            QSettings().setValue(key, checked);
            emit voiceSettingsChanged();
        });
        layout->addWidget(check);
    };

#ifdef ACHERON_HAVE_OPUS
    auto addCombo = [this, layout](const QString &title, const QString &key, int fallback,
                                   const QList<Option> &options) {
        auto *row = new QHBoxLayout();
        row->addWidget(new QLabel(title, this));

        auto *combo = new QComboBox(this);
        for (const Option &option : options)
            combo->addItem(tr(option.label), option.value);
        const int index = combo->findData(QSettings().value(key, fallback).toInt());
        combo->setCurrentIndex(index >= 0 ? index : 0);

        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, combo, key](int) {
            QSettings().setValue(key, combo->currentData().toInt());
            emit voiceSettingsChanged();
        });

        row->addWidget(combo);
        row->addStretch(1);
        layout->addLayout(row);
    };

    auto *opusHeader = new QLabel(tr("Codec"), this);
    opusHeader->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(opusHeader);

    addCombo(tr("Application"), "voice/codec/application", OPUS_APPLICATION_VOIP,
             { { "VOIP", OPUS_APPLICATION_VOIP }, { "Audio", OPUS_APPLICATION_AUDIO } });
    addCombo(tr("Signal"), "voice/codec/signal_type", OPUS_SIGNAL_VOICE,
             { { "Auto", OPUS_AUTO }, { "Voice", OPUS_SIGNAL_VOICE }, { "Music", OPUS_SIGNAL_MUSIC } });
    addSlider(tr("Bitrate"), "voice/codec/bitrate", 64, 8, 510, QStringLiteral(" kbps"));
    addSlider(tr("Complexity"), "voice/codec/complexity", 5, 0, 10, QString());
    addSlider(tr("Packet loss"), "voice/codec/packet_loss", 0, 0, 100, QStringLiteral("%"));
    addCheck(tr("Forward error correction"), "voice/codec/fec", true);
#endif

    auto *levelsHeader = new QLabel(tr("Levels"), this);
    levelsHeader->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(levelsHeader);

    addSlider(tr("Input gain"), "voice/input_gain", 100, 0, 200, QStringLiteral("%"));
    addSlider(tr("Output volume"), "voice/output_volume", 100, 0, 200, QStringLiteral("%"));
    addSlider(tr("Voice activity threshold"), "voice/vad_threshold", 100, 0, 2000, QString());

#ifdef ACHERON_HAVE_RNNOISE
    addCheck(tr("Noise suppression"), "voice/noise_suppression", true);
    addCheck(tr("Use RNNoise voice detection"), "voice/rnnoise_vad", true);
#endif

    auto *note = new QLabel(tr("* Takes effect after a restart."), this);
    QFont noteFont = note->font();
    noteFont.setItalic(true);
    if (noteFont.pointSize() > 1)
        noteFont.setPointSize(noteFont.pointSize() - 1);
    note->setFont(noteFont);
    layout->addWidget(note);

    layout->addStretch(1);

    connect(backendCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        Core::Audio::setConfiguredAudioBackend(backendCombo->itemData(index).toString());
    });
}

} // namespace UI
} // namespace Acheron

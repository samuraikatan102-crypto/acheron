#pragma once

#include <QWidget>

class QComboBox;
class QLabel;

namespace Acheron {
namespace Core {
namespace Audio {
class VoiceManager;
}
} // namespace Core
namespace UI {

class VolumeMeter;

// Discord's "Voice & Video" page: devices, levels and the codec knobs, applied to
// the live voice manager as soon as they change.
class AudioPage : public QWidget
{
    Q_OBJECT
public:
    explicit AudioPage(QWidget *parent = nullptr);

    void setVoiceManager(Core::Audio::VoiceManager *manager);

signals:
    void voiceSettingsChanged();

private:
    void refreshDevices();

    Core::Audio::VoiceManager *voiceManager = nullptr;

    QComboBox *backendCombo;
    QComboBox *inputDeviceCombo = nullptr;
    QComboBox *outputDeviceCombo = nullptr;
    VolumeMeter *volumeMeter = nullptr;
    QLabel *deviceHint = nullptr;
};

} // namespace UI
} // namespace Acheron

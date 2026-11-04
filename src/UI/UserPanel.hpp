#pragma once

#include <QUrl>
#include <QWidget>

#include "Core/Presence/PresenceBadge.hpp"
#include "Core/Snowflake.hpp"

class QHBoxLayout;
class QLabel;
class QPushButton;

namespace Acheron {
namespace Core {
class ImageManager;
namespace Audio {
class VoiceManager;
}
} // namespace Core

namespace UI {

class FriendAvatar;

// The bottom-left corner of Discord: a voice card that only exists while a call
// is up, and below it the account row with the microphone, headphone and
// user-settings buttons. Nothing here opens a separate window.
class UserPanel : public QWidget
{
    Q_OBJECT
public:
    explicit UserPanel(QWidget *parent = nullptr);

    void setImageManager(Core::ImageManager *manager);
    void setVoiceManager(Core::Audio::VoiceManager *manager);
    void setSelf(const QString &name, const QString &statusLine, const QUrl &avatarUrl,
                 Core::Snowflake accountId, const Core::PresenceBadge &badge);
    void setChannelLabel(const QString &text);
    void setStageMode(bool stage);
    void updateConnectionState();

signals:
    void settingsRequested();
    void disconnectRequested();
    void voiceFlagsRequested(bool mute, bool deaf);
    void handToggleRequested(bool raise);

private:
    void setupUi();
    void refreshThemeColors();
    void syncToggleIcons();
    void toggleMute();
    void toggleDeafen();

    Core::ImageManager *imageManager = nullptr;
    Core::Audio::VoiceManager *voiceManager = nullptr;

    QWidget *voiceCard = nullptr;
    QLabel *connectionIcon = nullptr;
    QLabel *connectionLabel = nullptr;
    QLabel *channelLabel = nullptr;

    QPushButton *micButton = nullptr;
    QPushButton *deafenButton = nullptr;
    QPushButton *handButton = nullptr;
    QPushButton *disconnectButton = nullptr;
    QPushButton *userMicButton = nullptr;
    QPushButton *userDeafenButton = nullptr;
    QPushButton *settingsButton = nullptr;

    FriendAvatar *avatar = nullptr;
    QLabel *nameLabel = nullptr;
    QLabel *statusLabel = nullptr;

    bool selfMute = false;
    bool selfDeaf = false;
    bool selfHand = false;
    bool stageMode = false;
};

} // namespace UI
} // namespace Acheron

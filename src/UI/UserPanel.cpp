#include "UserPanel.hpp"

#include "Core/ImageManager.hpp"
#include "Core/Theme/Icons.hpp"
#include "Core/Theme/Manager.hpp"
#include "UI/Friends/FriendRow.hpp"
#include "UI/StatusIndicator.hpp"

#ifndef ACHERON_NO_VOICE
#include "Core/Audio/VoiceManager.hpp"
#include "Discord/Voice/VoiceClient.hpp"
#endif

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace Acheron {
namespace UI {

namespace {

constexpr int ButtonSide = 30;
constexpr int GlyphSize = 18;
constexpr int AvatarSide = 32;

QPushButton *makeIconButton(const QString &glyph, const QColor &color, const QString &tip,
                           QWidget *parent)
{
    auto *button = new QPushButton(parent);
    button->setObjectName(QStringLiteral("PanelIconButton"));
    button->setFixedSize(ButtonSide, ButtonSide);
    button->setIconSize(QSize(GlyphSize, GlyphSize));
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::NoFocus);
    button->setToolTip(tip);
    button->setIcon(Core::Theme::Icons::icon(glyph, color));
    return button;
}

QColor tokenColor(Core::Theme::Token token)
{
    return Core::Theme::Manager::instance().color(token);
}

} // namespace

UserPanel::UserPanel(QWidget *parent) : QWidget(parent)
{
    setupUi();
    refreshThemeColors();
}

void UserPanel::setupUi()
{
    setObjectName(QStringLiteral("UserPanel"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    const QColor idle = tokenColor(Core::Theme::Token::PlaceholderText);

    voiceCard = new QWidget(this);
    voiceCard->setObjectName(QStringLiteral("VoiceCard"));
    voiceCard->setAttribute(Qt::WA_StyledBackground, true);

    auto *cardLayout = new QVBoxLayout(voiceCard);
    cardLayout->setContentsMargins(8, 8, 6, 6);
    cardLayout->setSpacing(4);

    auto *cardHeader = new QHBoxLayout();
    cardHeader->setContentsMargins(0, 0, 0, 0);
    cardHeader->setSpacing(6);

    connectionIcon = new QLabel(voiceCard);
    connectionIcon->setObjectName(QStringLiteral("VoiceCardIcon"));
    connectionIcon->setFixedSize(16, 16);
    connectionIcon->setScaledContents(true);
    cardHeader->addWidget(connectionIcon, 0);

    connectionLabel = new QLabel(voiceCard);
    connectionLabel->setObjectName(QStringLiteral("VoiceCardTitle"));
    cardHeader->addWidget(connectionLabel, 0);
    cardHeader->addStretch(1);

    disconnectButton = makeIconButton(Core::Theme::Icons::Name::PhoneOff, idle,
                                      tr("Disconnect"), voiceCard);
    connect(disconnectButton, &QPushButton::clicked, this, &UserPanel::disconnectRequested);
    cardHeader->addWidget(disconnectButton, 0);
    cardLayout->addLayout(cardHeader, 0);

    channelLabel = new QLabel(voiceCard);
    channelLabel->setObjectName(QStringLiteral("VoiceCardChannel"));
    cardLayout->addWidget(channelLabel, 0);

    auto *cardActions = new QHBoxLayout();
    cardActions->setContentsMargins(0, 2, 0, 0);
    cardActions->setSpacing(6);

    micButton = makeIconButton(Core::Theme::Icons::Name::Mic, idle, tr("Mute"), voiceCard);
    connect(micButton, &QPushButton::clicked, this, &UserPanel::toggleMute);
    cardActions->addWidget(micButton, 0);

    deafenButton = makeIconButton(Core::Theme::Icons::Name::Headphones, idle, tr("Deafen"),
                                  voiceCard);
    connect(deafenButton, &QPushButton::clicked, this, &UserPanel::toggleDeafen);
    cardActions->addWidget(deafenButton, 0);

    handButton = makeIconButton(Core::Theme::Icons::Name::Hand, idle, tr("Raise Hand"), voiceCard);
    connect(handButton, &QPushButton::clicked, this, [this]() {
        selfHand = !selfHand;
        syncToggleIcons();
        emit handToggleRequested(selfHand);
    });
    handButton->hide();
    cardActions->addWidget(handButton, 0);

    cardActions->addStretch(1);
    cardLayout->addLayout(cardActions, 0);

    voiceCard->hide();
    root->addWidget(voiceCard, 0);

    auto *userRow = new QWidget(this);
    userRow->setObjectName(QStringLiteral("UserRow"));
    userRow->setAttribute(Qt::WA_StyledBackground, true);
    userRow->setFixedHeight(54);

    auto *rowLayout = new QHBoxLayout(userRow);
    rowLayout->setContentsMargins(8, 6, 6, 6);
    rowLayout->setSpacing(8);

    avatar = new FriendAvatar(userRow);
    avatar->setSize(AvatarSide);
    rowLayout->addWidget(avatar, 0);

    auto *identity = new QVBoxLayout();
    identity->setContentsMargins(0, 0, 0, 0);
    identity->setSpacing(1);

    nameLabel = new QLabel(userRow);
    nameLabel->setObjectName(QStringLiteral("UserPanelName"));
    nameLabel->setText(tr("Not signed in"));
    identity->addWidget(nameLabel, 0);

    statusLabel = new QLabel(userRow);
    statusLabel->setObjectName(QStringLiteral("UserPanelStatus"));
    identity->addWidget(statusLabel, 0);

    rowLayout->addLayout(identity, 1);

    userMicButton = makeIconButton(Core::Theme::Icons::Name::Mic, idle, tr("Mute"), userRow);
    connect(userMicButton, &QPushButton::clicked, this, &UserPanel::toggleMute);
    rowLayout->addWidget(userMicButton, 0);

    userDeafenButton = makeIconButton(Core::Theme::Icons::Name::Headphones, idle, tr("Deafen"),
                                      userRow);
    connect(userDeafenButton, &QPushButton::clicked, this, &UserPanel::toggleDeafen);
    rowLayout->addWidget(userDeafenButton, 0);

    settingsButton = makeIconButton(Core::Theme::Icons::Name::Settings, idle,
                                    tr("User Settings"), userRow);
    connect(settingsButton, &QPushButton::clicked, this, &UserPanel::settingsRequested);
    rowLayout->addWidget(settingsButton, 0);

    root->addWidget(userRow, 0);
}

void UserPanel::refreshThemeColors()
{
    const QColor idle = tokenColor(Core::Theme::Token::PlaceholderText);
    const QColor danger = tokenColor(Core::Theme::Token::StatusDnd);

    connectionIcon->setPixmap(
            Core::Theme::Icons::pixmap(Core::Theme::Icons::Name::Wifi, 16, idle, devicePixelRatioF()));

    disconnectButton->setIcon(Core::Theme::Icons::icon(Core::Theme::Icons::Name::PhoneOff, danger));

    nameLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; }")
                                     .arg(tokenColor(Core::Theme::Token::BrightText).name()));
    statusLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; }")
                                       .arg(idle.name()));
    channelLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(idle.name()));

    syncToggleIcons();
}

void UserPanel::syncToggleIcons()
{
    const QColor idle = tokenColor(Core::Theme::Token::PlaceholderText);
    const QColor danger = tokenColor(Core::Theme::Token::StatusDnd);
    const QColor blurple = tokenColor(Core::Theme::Token::Highlight);

    const QString micGlyph = selfMute ? Core::Theme::Icons::Name::MicOff
                                      : Core::Theme::Icons::Name::Mic;
    const QColor micColor = selfMute ? danger : idle;
    micButton->setIcon(Core::Theme::Icons::icon(micGlyph, micColor));
    userMicButton->setIcon(Core::Theme::Icons::icon(micGlyph, micColor));

    const QString deafenGlyph = selfDeaf ? Core::Theme::Icons::Name::HeadphonesOff
                                         : Core::Theme::Icons::Name::Headphones;
    const QColor deafenColor = selfDeaf ? danger : idle;
    deafenButton->setIcon(Core::Theme::Icons::icon(deafenGlyph, deafenColor));
    userDeafenButton->setIcon(Core::Theme::Icons::icon(deafenGlyph, deafenColor));

    handButton->setIcon(Core::Theme::Icons::icon(Core::Theme::Icons::Name::Hand,
                                                 selfHand ? blurple : idle));
}

void UserPanel::toggleMute()
{
    selfMute = !selfMute;
    if (selfDeaf)
        selfMute = true;
    syncToggleIcons();
    emit voiceFlagsRequested(selfMute, selfDeaf);
}

void UserPanel::toggleDeafen()
{
    selfDeaf = !selfDeaf;
    if (selfDeaf)
        selfMute = true;
    else
        selfMute = false;
    syncToggleIcons();
    emit voiceFlagsRequested(selfMute, selfDeaf);
}

void UserPanel::setImageManager(Core::ImageManager *manager)
{
    imageManager = manager;
}

void UserPanel::setVoiceManager(Core::Audio::VoiceManager *manager)
{
    if (voiceManager == manager)
        return;

    selfMute = false;
    selfDeaf = false;
    selfHand = false;

#ifdef ACHERON_NO_VOICE
    Q_UNUSED(manager);
    voiceManager = nullptr;
#else
    if (voiceManager)
        disconnect(voiceManager, nullptr, this, nullptr);

    voiceManager = manager;

    if (voiceManager) {
        connect(voiceManager, &Core::Audio::VoiceManager::voiceStateChanged, this,
                &UserPanel::updateConnectionState);
        connect(voiceManager, &Core::Audio::VoiceManager::handRaisedChanged, this,
                [this](bool raised) {
                    selfHand = raised;
                    syncToggleIcons();
                });
    }
#endif

    syncToggleIcons();
    updateConnectionState();
}

void UserPanel::setSelf(const QString &name, const QString &statusLine, const QUrl &avatarUrl,
                        Core::Snowflake accountId, const Core::PresenceBadge &badge)
{
    nameLabel->setText(name.isEmpty() ? tr("Not signed in") : name);
    statusLabel->setText(statusLine);
    avatar->setBadge(badge);
    avatar->setToolTip(StatusIndicator::tooltip(badge));

    if (imageManager)
        imageManager->assign(avatar, avatarUrl, QSize(AvatarSide * 2, AvatarSide * 2), accountId);
}

void UserPanel::setChannelLabel(const QString &text)
{
    channelLabel->setText(text);
}

void UserPanel::setStageMode(bool stage)
{
    stageMode = stage;
    handButton->setVisible(stage && voiceCard->isVisible());
}

void UserPanel::updateConnectionState()
{
#ifdef ACHERON_NO_VOICE
    voiceCard->setVisible(false);
#else
    using State = Discord::Voice::VoiceClient::State;

    State state = State::Disconnected;
    if (voiceManager)
        state = voiceManager->clientState();

    const bool connected = (state == State::Connected);
    const bool connecting = (state == State::Connecting || state == State::Identifying ||
                             state == State::WaitingForReady || state == State::DiscoveringIP ||
                             state == State::SelectingProtocol || state == State::WaitingForSession);

    QString text;
    QColor color;
    if (connected) {
        text = tr("Voice Connected");
        color = tokenColor(Core::Theme::Token::StatusOnline);
    } else if (connecting) {
        text = state == State::Connecting ? tr("Connecting...") : tr("Securing...");
        color = tokenColor(Core::Theme::Token::StatusIdle);
    }

    const bool active = connected || connecting;
    voiceCard->setVisible(active);
    handButton->setVisible(stageMode && active);

    if (active) {
        connectionLabel->setText(text);
        connectionLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(color.name()));
        connectionIcon->setPixmap(Core::Theme::Icons::pixmap(Core::Theme::Icons::Name::Wifi, 16,
                                                             color, devicePixelRatioF()));
    }

    if (!active) {
        selfMute = false;
        selfDeaf = false;
        selfHand = false;
    }

    syncToggleIcons();
#endif
}

} // namespace UI
} // namespace Acheron

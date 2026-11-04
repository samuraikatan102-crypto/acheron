#include "VoiceStatusBar.hpp"
#include "VoiceWindow.hpp"

#include "Core/Audio/VoiceManager.hpp"
#include "Core/Theme/Manager.hpp"

#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>

namespace Acheron {
namespace UI {

namespace {

// idk why i cant just set some text so this will have to do ig
QIcon makeXIcon(const QColor &color)
{
    QPixmap px(16, 16);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(4, 4, 12, 12);
    p.drawLine(12, 4, 4, 12);
    return QIcon(px);
}

} // namespace

VoiceStatusBar::VoiceStatusBar(QWidget *parent) : QWidget(parent)
{
    setupUi();
    hide();

    connect(&Core::Theme::Manager::instance(), &Core::Theme::Manager::themeChanged,
            this, &VoiceStatusBar::refreshThemeColors);

    closeTimer.setSingleShot(true);
    closeTimer.setInterval(500);
    connect(&closeTimer, &QTimer::timeout, this, [this]() {
        if (voiceWindow)
            voiceWindow->close();
    });
}

VoiceStatusBar::~VoiceStatusBar()
{
    delete voiceWindow;
}

void VoiceStatusBar::setupUi()
{
    setFixedHeight(32);
    setCursor(Qt::PointingHandCursor);

    setStyleSheet("QLabel { background: transparent; }");

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 0, 6, 0);
    layout->setSpacing(6);

    statusDot = new QLabel(this);
    statusDot->setFixedSize(8, 8);

    statusLabel = new QLabel(this);

    channelLabel = new QLabel(this);
    channelLabel->setStyleSheet("QLabel { font-size: 11px; }");

    disconnectBtn = new QPushButton(this);
    disconnectBtn->setFixedSize(24, 24);
    disconnectBtn->setIconSize(QSize(16, 16));
    disconnectBtn->setCursor(Qt::PointingHandCursor);
    connect(disconnectBtn, &QPushButton::clicked, this, &VoiceStatusBar::disconnectRequested);

    layout->addWidget(statusDot);
    layout->addWidget(statusLabel);
    layout->addWidget(channelLabel);
    layout->addStretch();
    layout->addWidget(disconnectBtn);

    refreshThemeColors();
}

void VoiceStatusBar::refreshThemeColors()
{
    Core::Theme::Manager &theme = Core::Theme::Manager::instance();

    channelLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; font-size: 11px; }")
                                        .arg(theme.color(Core::Theme::Token::PlaceholderText).name()));

    const QColor errorColor = theme.color(Core::Theme::Token::StatusDnd);
    disconnectBtn->setIcon(makeXIcon(errorColor));
    disconnectBtn->setStyleSheet(QStringLiteral(
                                         "QPushButton { background: transparent; border: none; }"
                                         "QPushButton:hover { background-color: rgba(%1, %2, %3, 30); border-radius: 4px; }")
                                         .arg(errorColor.red())
                                         .arg(errorColor.green())
                                         .arg(errorColor.blue()));

    updateConnectionState();
}

void VoiceStatusBar::setVoiceManager(Core::Audio::VoiceManager *manager)
{
    if (voiceManager == manager)
        return;

    disconnectManager();
    voiceManager = manager;

    if (!voiceManager) {
        if (voiceWindow)
            voiceWindow->setVoiceManager(nullptr);
        updateConnectionState();
        return;
    }

    connect(voiceManager, &Core::Audio::VoiceManager::voiceStateChanged,
            this, &VoiceStatusBar::updateConnectionState);

    configureVoiceWindow();
    updateConnectionState();
}

void VoiceStatusBar::setNameResolver(NameResolver resolver)
{
    nameResolver = std::move(resolver);
    if (voiceWindow)
        voiceWindow->setNameResolver(nameResolver);
}

void VoiceStatusBar::setAvatarResolver(AvatarResolver resolver)
{
    avatarResolver = std::move(resolver);
    if (voiceWindow)
        voiceWindow->setAvatarResolver(avatarResolver);
}

void VoiceStatusBar::setStatusResolver(StatusResolver resolver)
{
    statusResolver = std::move(resolver);
    if (voiceWindow)
        voiceWindow->setStatusResolver(statusResolver);
}

void VoiceStatusBar::refreshPresences(const QList<Core::Snowflake> &userIds)
{
    if (voiceWindow)
        voiceWindow->refreshPresences(userIds);
}

void VoiceStatusBar::setImageManager(Core::ImageManager *manager)
{
    imageManager = manager;
    if (voiceWindow)
        voiceWindow->setImageManager(manager);
}

void VoiceStatusBar::setAccount(Core::Snowflake id)
{
    accountId = id;
    if (voiceWindow)
        voiceWindow->setAccount(id);
}

void VoiceStatusBar::setChannelName(const QString &name)
{
    if (name.isEmpty())
        channelLabel->hide();
    else {
        channelLabel->setText(name);
        channelLabel->show();
    }
}

void VoiceStatusBar::updateConnectionState()
{
    using State = Discord::Voice::VoiceClient::State;

    State state = State::Disconnected;
    if (voiceManager)
        state = voiceManager->clientState();

    bool connected = (state == State::Connected);
    bool connecting = (state == State::Connecting || state == State::Identifying ||
                       state == State::WaitingForReady || state == State::DiscoveringIP ||
                       state == State::SelectingProtocol || state == State::WaitingForSession);
    bool active = connected || connecting;

    QString dotColor;
    QString textColor;
    QString stateText;

    auto *theme = &Core::Theme::Manager::instance();
    if (connected) {
        stateText = tr("Voice Connected");
        dotColor = theme->color(Core::Theme::Token::StatusOnline).name();
        textColor = dotColor;
    } else if (connecting) {
        if (state == State::Connecting)
            stateText = tr("Connecting...");
        else if (state == State::Identifying)
            stateText = tr("Identifying...");
        else
            stateText = tr("Securing...");
        dotColor = theme->color(Core::Theme::Token::StatusIdle).name();
        textColor = dotColor;
    } else {
        stateText = tr("Disconnected");
        dotColor = theme->color(Core::Theme::Token::StatusOffline).name();
        textColor = dotColor;
    }

    statusDot->setStyleSheet(QStringLiteral("QLabel { background-color: %1; border-radius: 4px; }").arg(dotColor));
    statusLabel->setText(stateText);
    statusLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; font-size: 11px; font-weight: bold; }").arg(textColor));

    setVisible(active);

    if (active && wasDisconnected) {
        closeTimer.stop();
        showVoiceWindow();
        if (voiceWindow)
            voiceWindow->refreshDevices();
    } else if (active) {
        closeTimer.stop();
    }

    if (!active && voiceWindow)
        closeTimer.start();

    wasDisconnected = !active;
}

void VoiceStatusBar::mousePressEvent(QMouseEvent *event)
{
    if (disconnectBtn->geometry().contains(event->pos())) {
        QWidget::mousePressEvent(event);
        return;
    }

    toggleVoiceWindow();
}

void VoiceStatusBar::toggleVoiceWindow()
{
    if (voiceWindow && voiceWindow->isVisible() && !voiceWindow->isMinimized())
        voiceWindow->close();
    else
        showVoiceWindow();
}

void VoiceStatusBar::showVoiceWindow()
{
    if (!voiceWindow) {
        voiceWindow = new VoiceWindow();
        connect(voiceWindow, &VoiceWindow::userProfileRequested, this, &VoiceStatusBar::userProfileRequested);
        connect(voiceWindow, &VoiceWindow::handToggleRequested, this, &VoiceStatusBar::handToggleRequested);
        connect(voiceWindow, &VoiceWindow::stageSpeakerRequested, this, &VoiceStatusBar::stageSpeakerRequested);
        configureVoiceWindow();
    }

    voiceWindow->setWindowState(voiceWindow->windowState() & ~Qt::WindowMinimized);
    voiceWindow->show();
    voiceWindow->raise();
    voiceWindow->activateWindow();
}

void VoiceStatusBar::configureVoiceWindow()
{
    if (!voiceWindow)
        return;
    if (imageManager)
        voiceWindow->setImageManager(imageManager);
    voiceWindow->setAccount(accountId);
    if (nameResolver)
        voiceWindow->setNameResolver(nameResolver);
    if (avatarResolver)
        voiceWindow->setAvatarResolver(avatarResolver);
    if (statusResolver)
        voiceWindow->setStatusResolver(statusResolver);
    if (voiceManager)
        voiceWindow->setVoiceManager(voiceManager);
    voiceWindow->setStageChannelVisible(stageMode);
}

void VoiceStatusBar::setStageChannelVisible(bool stage)
{
    if (stageMode == stage)
        return;

    stageMode = stage;
    if (voiceWindow)
        voiceWindow->setStageChannelVisible(stage);
}

void VoiceStatusBar::reloadVoiceSettings()
{
    if (voiceWindow)
        voiceWindow->reloadSettings();
}

void VoiceStatusBar::disconnectManager()
{
    if (voiceManager)
        disconnect(voiceManager, nullptr, this, nullptr);
    voiceManager = nullptr;
}

} // namespace UI
} // namespace Acheron

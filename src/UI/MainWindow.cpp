#include "MainWindow.hpp"

#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QSettings>
#include <QSystemTrayIcon>

#include <utility>

#include "Chat/ChatModel.hpp"
#include "Chat/ChatDelegate.hpp"
#include "Chat/ChatView.hpp"
#include "Chat/FrameAnimator.hpp"
#include "Chat/GifPlayback.hpp"
#include "Chat/InlineVideoController.hpp"
#include "Forum/ForumBrowser.hpp"
#include "Forum/ForumPostModel.hpp"
#include "Forum/NewPostDialog.hpp"
#include "Friends/FriendRow.hpp"
#include "Friends/FriendsView.hpp"
#include "ThreadBrowser/ThreadBrowserPopup.hpp"
#include "Core/ForumManager.hpp"
#include "ChannelList/ChannelTreeModel.hpp"
#include "ChannelList/ChannelFilterProxyModel.hpp"
#include "ChannelList/ChannelDelegate.hpp"
#include "ChannelList/ChannelTreeView.hpp"
#include "ChannelList/ServerRailView.hpp"
#include "ChannelList/ServerRailModel.hpp"
#include "ChannelList/ServerRailDelegate.hpp"
#include "TabBar/TabBar.hpp"
#include "Settings/SettingsWindow.hpp"
#include "GuildSettings/GuildSettingsWindow.hpp"
#include "GuildSettings/MemberModeration.hpp"
#include "Accounts/AccountsModel.hpp"
#include "UI/Login/LoginView.hpp"
#include "Core/ClientInstance.hpp"
#include "Core/AccountInfo.hpp"
#include "Core/Emoji/EmojiManager.hpp"
#include "Core/UserManager.hpp"
#include "Core/TypingTracker.hpp"
#include "Core/Logging.hpp"
#include "Core/ReadStateManager.hpp"
#include "Core/RelationshipManager.hpp"
#include "Core/Theme/Icons.hpp"
#include "Discord/Events.hpp"
#include "TypingIndicator.hpp"
#include "SlowModeIndicator.hpp"
#include "ConnectionBanner.hpp"
#include "ChannelTopicLine.hpp"
#include "TrayIcon.hpp"
#include "BrowserCaptchaResolver.hpp"
#include "Dialogs/ChannelTopicPopup.hpp"
#include "Dialogs/ReactionsPopup.hpp"
#include "Dialogs/ConfirmPopup.hpp"
#include "Dialogs/UserProfilePopup.hpp"
#include "Chat/ReactionTooltip.hpp"
#include "Discord/ApiError.hpp"
#include "Discord/CdnUrls.hpp"
#include "Core/ImageManager.hpp"
#include "Core/MemberListManager.hpp"
#include "Core/Presence/PresenceManager.hpp"
#include "Core/Session.hpp"
#include "Core/Theme/Manager.hpp"
#include "Core/Audio/AudioSettings.hpp"
#include "CommandBar.hpp"
#include "UserPanel.hpp"
#ifndef ACHERON_NO_VOICE
#include "Core/Audio/VoiceManager.hpp"
#include "Discord/Voice/VoiceClient.hpp"
#endif

using namespace Acheron::Core;

namespace Acheron {
namespace UI {

namespace {
constexpr int LeftPaneIndex = 0;
constexpr int CenterPaneIndex = 1;
constexpr int MemberPaneIndex = 2;
constexpr int DefaultChannelListWidth = 240;
constexpr int DefaultMemberListWidth = 200;

QString guildChannelKey(Core::Snowflake accountId, Core::Snowflake guildId)
{
    return QStringLiteral("%1:%2")
            .arg(static_cast<quint64>(accountId))
            .arg(static_cast<quint64>(guildId));
}

bool voiceChatLocked(Core::ClientInstance *instance, Core::Snowflake channelId)
{
    auto channel = instance->getChannel(channelId);
    if (!channel || !channel->isVoice())
        return false;
    return !instance->permissions()->hasChannelPermission(instance->accountId(), channelId, Discord::Permission::CONNECT);
}

bool isArchivedThread(const std::optional<Discord::Channel> &channel)
{
    return channel && channel->isThread() && channel->isArchived();
}

bool hasPermissionIn(Core::ClientInstance *instance, const std::optional<Discord::Channel> &channel, Core::Snowflake channelId, Discord::Permission permission)
{
    const bool inheritsFromParent = channel && channel->isThread() && channel->parentId.hasValue();
    return instance->permissions()->hasChannelPermission(instance->accountId(), inheritsFromParent ? channel->parentId.get() : channelId, permission);
}

bool canAddReactionsIn(Core::ClientInstance *instance, Core::Snowflake channelId, Core::Snowflake guildId)
{
    const std::optional<Discord::Channel> channel = instance->getChannel(channelId);
    if (isArchivedThread(channel))
        return false;
    return !guildId.isValid() || hasPermissionIn(instance, channel, channelId, Discord::Permission::ADD_REACTIONS);
}

ReactionsPopup::RemovableReactors removableReactorsIn(Core::ClientInstance *instance, Core::Snowflake channelId, Core::Snowflake guildId)
{
    const std::optional<Discord::Channel> channel = instance->getChannel(channelId);
    if (isArchivedThread(channel))
        return ReactionsPopup::RemovableReactors::None;

    const bool canManageMessages = guildId.isValid() && hasPermissionIn(instance, channel, channelId, Discord::Permission::MANAGE_MESSAGES);
    return canManageMessages ? ReactionsPopup::RemovableReactors::Anyone : ReactionsPopup::RemovableReactors::OwnOnly;
}

void showReactionRejection(QWidget *parent, MessageManager::ReactionRejection reason)
{
    QString title;
    QString text;
    switch (reason) {
    case MessageManager::ReactionRejection::TooManyReactions:
        title = MainWindow::tr("We appreciate the enthusiasm, but...");
        text = MainWindow::tr("Your reaction was not added because there are too many reactions on this message.");
        break;
    case MessageManager::ReactionRejection::AlreadyReactedWithOtherType:
        title = MainWindow::tr("Oops!");
        text = MainWindow::tr("You can't react and super react with the same emoji. There's a lot of emoji to choose from, so maybe try another one.");
        break;
    }

    auto *box = new QMessageBox(QMessageBox::Information, title, text, QMessageBox::Ok, parent);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();
}

void applyGifSettings(GifPlayback *gifs)
{
    const QSettings settings;
    gifs->setPlayMode(gifPlayModeFromSetting(settings.value("chat/gif_playback").toString()));
    gifs->setMaxPlayingAtOnce(settings.value("chat/gif_max_playing", GifPlayback::DefaultPlayingAtOnce).toInt());
    gifs->setClipMemoryLimitMiB(settings.value("chat/gif_max_mb", GifPlayback::ClipMemoryLimit.fallback).toInt());
}
} // namespace

MainWindow::MainWindow(Session *session, QWidget *parent) : QMainWindow(parent), session(session)
{
    auto *captchaResolver = new BrowserCaptchaResolver(this, this);
    session->setCaptchaResolver(captchaResolver);

    chatModel = new ChatModel(session->getImageManager(), this);
    channelTreeModel = new ChannelTreeModel(session, this);
    channelFilterProxy = new ChannelFilterProxyModel(session, this);
    channelFilterProxy->setSourceModel(channelTreeModel);
    channelFilterProxy->setDynamicSortFilter(true);
    channelFilterProxy->sort(0);
    accountsModel = new AccountsModel(session, this);
    serverRailModel = new ServerRailModel(session, channelTreeModel, this);

    chatModel->setAvatarUrlResolver([this](const Discord::User &author, Snowflake guildId) -> QUrl {
        if (!currentInstance)
            return Discord::Cdn::userAvatar(author.id.get(), author.avatar.get(), 64);
        return currentInstance->users()->getAvatarUrl(author, guildId, 64);
    });

    chatModel->setDisplayNameResolver([this](const Discord::User &author, Snowflake guildId) -> QString {
        if (!currentInstance)
            return QString();
        return currentInstance->users()->getAuthorDisplayName(author, guildId);
    });

    chatModel->setRoleColorResolver(
            [this](Snowflake userId, Snowflake guildId) { return resolveRoleColor(userId, guildId); });

    chatModel->setChannelNameResolver([this](Snowflake channelId) -> QString {
        if (!currentInstance)
            return QString();
        auto channel = currentInstance->getChannel(channelId);
        if (channel && channel->name.hasValue())
            return channel->name.get();
        return QString();
    });

    chatModel->setGuildInfoResolver([this](Snowflake guildId) -> QPair<QString, QUrl> {
        if (!currentInstance)
            return {};
        auto guild = currentInstance->getGuild(guildId);
        if (!guild || !guild->name.hasValue())
            return {};
        QUrl iconUrl;
        if (guild->icon.hasValue() && !guild->icon.get().isEmpty())
            iconUrl = Discord::Cdn::guildIcon(guildId, guild->icon.get(), 64);
        return { guild->name.get(), iconUrl };
    });

    typingTracker = new TypingTracker(this);
    memberListModel = new MemberListModel(session->getImageManager(), this);

    channelListMode = (QSettings().value("ui/channelListMode").toString() == "classic")
                              ? ChannelListMode::Classic
                              : ChannelListMode::Tree;

    setupUi();
    setupMenu();
    applyCloseToTray();

    qApp->installEventFilter(this);

    typingIndicator->setRoleColorResolver(
            [this](Snowflake userId, Snowflake guildId) { return resolveRoleColor(userId, guildId); });

    connect(typingTracker, &TypingTracker::typersChanged, this,
            [this]() { typingIndicator->setTypers(typingTracker->getActiveTypers()); });

    connect(channelTreeModel, &ChannelTreeModel::totalMentionCountChanged, this,
            &MainWindow::onTotalMentionCountChanged);

    connect(session, &Session::ready, this, [this](const Discord::Ready &ready) {
        channelTreeModel->populateFromReady(ready);
        if (channelListMode == ChannelListMode::Tree)
            channelTree->performDefaultExpansion();
        applyTreeState();
        maybeActivatePendingChannel(ready.user->id);
        refreshTabReadStates();
        if (channelListMode == ChannelListMode::Classic)
            applyPendingRailSelection(ready.user->id);
    });

    // a stored account whose token the keychain no longer has would otherwise
    // leave the window connected to nothing at all
    connect(session, &Session::connectFailed, this,
            [this](Core::Snowflake accountId, const QString &reason) {
                if (currentInstance || session->client(accountId))
                    return;

                shellPages->setCurrentWidget(loginView);
                loginView->showExpiredSession(reason);
            });

    connect(accountsModel, &AccountsModel::dataChanged, this,
            [this, session](const QModelIndex &topLeft, const QModelIndex &bottomRight,
                            const QVector<int> &roles) {
                for (int row = topLeft.row(); row <= bottomRight.row(); ++row) {
                    QModelIndex idx = accountsModel->index(row, 0);
                    auto accPtr = idx.data(AccountsModel::AccountObjectRole).value<void *>();

                    Acheron::Core::AccountInfo acc =
                            *static_cast<Acheron::Core::AccountInfo *>(accPtr);

                    if (acc.state == Acheron::Core::ConnectionState::Connected) {
                        channelTreeModel->addAccount(acc);

                        ClientInstance *instance = session->client(acc.id);
                        if (instance)
                            setupPermanentConnections(instance);

                        for (int i = 0; i < channelFilterProxy->rowCount(QModelIndex()); ++i) {
                            QModelIndex proxyIdx = channelFilterProxy->index(i, 0, QModelIndex());
                            if (proxyIdx.data(ChannelTreeModel::IdRole).toULongLong() == acc.id) {
                                channelTree->expand(proxyIdx);
                                break;
                            }
                        }
                    } else if (acc.state == Acheron::Core::ConnectionState::Disconnected) {
                        detachInstance(acc.id);
                        bool wasShowing = channelListMode == ChannelListMode::Classic && railSelectedAccountId == acc.id;
                        channelTreeModel->removeAccount(acc.id);
                        instancesSignalsConnected.remove(acc.id);
                        if (wasShowing) {
                            railHasSelection = false;
                            channelTree->setRootIndex({});
                            selectInitialRailItem();
                        }
                    }
                }
            });

    for (const auto &instance : session->getClients()) {
        if (instance)
            setupPermanentConnections(instance);
    }

    restoreWindowState();

    // logging out of every account leaves nothing to show, so ask for a login again
    connect(accountsModel, &QAbstractItemModel::rowsRemoved, this, [this]() {
        if (accountsModel->rowCount() == 0)
            showLoginScreen();
    });

    if (accountsModel->rowCount() == 0)
        showLoginScreen();
}

MainWindow::~MainWindow()
{
    for (const QPointer<GuildSettingsWindow> &window : std::as_const(guildSettingsWindows))
        delete window.data();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveWindowState();

    const bool reachableFromTray = trayIcon && QSystemTrayIcon::isSystemTrayAvailable();
    qApp->setQuitOnLastWindowClosed(!reachableFromTray);
    hide();

    // if (session)
    //    session->shutdown();

    event->accept();
}

bool MainWindow::eventFilter(QObject *obj, QEvent *ev)
{
    if (ev->type() == QEvent::MouseButtonPress && isActiveWindow()) {
        auto *me = static_cast<QMouseEvent *>(ev);
        if (me->button() == Qt::BackButton) {
            tabBar->navigateBack();
            return true;
        }
        if (me->button() == Qt::ForwardButton) {
            tabBar->navigateForward();
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, ev);
}

void MainWindow::onChannelSelectionChanged(const QModelIndex &current, const QModelIndex &previous)
{
    if (!current.isValid())
        return;

    QModelIndex sourceIndex = channelFilterProxy->mapToSource(current);
    auto node = static_cast<ChannelNode *>(sourceIndex.internalPointer());

    if (!node || !node->opensChat())
        return;

    messageInput->emojiAutocomplete()->cancel();

    ChannelNode *accountNode = channelTreeModel->getAccountNodeFor(node);
    if (!accountNode) {
        messageInput->setEnabled(false);
        return;
    }

    channelFilterProxy->setSelectedChannel(node->id, accountNode->id);
    channelTree->viewport()->update();

    ClientInstance *selectedInstance = session->client(accountNode->id);
    if (!selectedInstance) {
        messageInput->setEnabled(false);
        return;
    }

    if (selectedInstance != currentInstance)
        switchActiveInstance(selectedInstance);

    channelTreeModel->clearTemporaryThread(node->id);

    ChannelNode *forumNode = node->type == ChannelNode::Type::Thread ? node->parent : node;

    if (forumNode && forumNode->type == ChannelNode::Type::Forum) {
        ChannelNode *gNode = ChannelTreeModel::findGuildNode(node);
        Snowflake gId = gNode ? gNode->id : Snowflake::Invalid;

        setThreadBrowserTarget(Snowflake::Invalid);
        openForumChannel(selectedInstance, forumNode->id, gId);
        tabBar->updateCurrentTab(makeTabEntry(forumNode, accountNode));

        if (gNode)
            recordLastViewedChannel(accountNode->id, gNode->id, forumNode->id);

        if (node->type == ChannelNode::Type::Thread)
            openForumPost(node->id, gId);
        return;
    }

    selectedInstance->readState()->setActiveChannel(node->id);
    setViewMode(ViewMode::TextChannel);

    ChannelNode *guildNode = ChannelTreeModel::findGuildNode(node);
    Snowflake guildId = guildNode ? guildNode->id : Snowflake::Invalid;
    bool isDm = node->type == ChannelNode::Type::DMChannel;

    applyChannelChrome(selectedInstance, node->id, node->name, isDm, guildId);
    switchChatChannel(node->id, guildId);
    selectedInstance->messages()->requestLoadChannel(node->id);

    tabBar->updateCurrentTab(makeTabEntry(node, accountNode));

    if ((node->type == ChannelNode::Type::Channel || node->type == ChannelNode::Type::VoiceChannel) && guildNode)
        recordLastViewedChannel(accountNode->id, guildNode->id, node->id);
    else if (isDm)
        recordLastViewedChannel(accountNode->id, Snowflake::Invalid, node->id);
}

void MainWindow::applyChannelChrome(Core::ClientInstance *instance, Core::Snowflake channelId,
                                    const QString &name, bool isDm, Core::Snowflake guildId)
{
    Snowflake userId = instance->accountId();
    messageInput->setMaxUploadSize(instance->discord()->getMaxUploadSize(channelId));

    Snowflake threadParentId = Snowflake::Invalid;
    bool archived = false;
    bool isVoice = false;
    if (auto ch = instance->getChannel(channelId); ch) {
        isVoice = ch->isVoice();
        if (ch->isThread()) {
            threadParentId = ch->parentId.hasValue() ? ch->parentId.get() : Snowflake::Invalid;
            archived = ch->isArchived();
        }
    }
    bool isThread = threadParentId.isValid();

    // voice channels cannot hold threads
    setThreadBrowserTarget(isDm || isVoice ? Snowflake::Invalid : (isThread ? threadParentId : channelId));
    showChannelTopic(instance, channelId);

    if (isDm) {
        messageInput->setEnabled(true);
        messageInput->setSendBlocked(false);
        messageInput->setPlaceholder("Message @" + name);
        chatView->setCanPinMessages(true);
        chatView->setCanManageMessages(false);
        slowModeIndicator->setSlowMode(channelId, 0, false);
        setMemberListVisible(false);
        instance->memberList()->clear();
        return;
    }

    Snowflake permChannel = isThread ? threadParentId : channelId;
    Discord::Permission sendPerm = isThread ? Discord::Permission::SEND_MESSAGES_IN_THREADS : Discord::Permission::SEND_MESSAGES;

    bool voiceLocked = voiceChatLocked(instance, channelId);
    bool canSend = !voiceLocked && instance->permissions()->hasChannelPermission(userId, permChannel, sendPerm);
    bool canPin = instance->permissions()->hasChannelPermission(userId, permChannel, Discord::Permission::PIN_MESSAGES);
    bool canManage = instance->permissions()->hasChannelPermission(userId, permChannel, Discord::Permission::MANAGE_MESSAGES);

    int rateLimit = instance->getChannelRateLimit(channelId);
    bool canBypass = instance->permissions()->hasChannelPermission(userId, permChannel, Discord::Permission::BYPASS_SLOWMODE);
    slowModeIndicator->setSlowMode(channelId, rateLimit, canBypass);

    bool onCooldown = rateLimit > 0 && !canBypass && slowModeIndicator->isOnCooldown(channelId);
    messageInput->setEnabled(canSend && !archived);
    messageInput->setSendBlocked(onCooldown);
    chatView->setCanPinMessages(canPin);
    chatView->setCanManageMessages(canManage);

    if (archived)
        messageInput->setPlaceholder("This thread is archived");
    else if (voiceLocked)
        messageInput->setPlaceholder("You do not have permission to connect to this channel");
    else if (!canSend)
        messageInput->setPlaceholder("You do not have permission to send messages");
    else if (onCooldown)
        messageInput->setPlaceholder("Slowmode is active");
    else
        messageInput->setPlaceholder((isThread ? "Message " : "Message #") + name);

    if (guildId.isValid()) {
        setMemberListVisible(true);
        instance->memberList()->setActiveChannel(guildId, isThread ? threadParentId : channelId);
    }
}

void MainWindow::switchChatChannel(Core::Snowflake channelId, Core::Snowflake guildId)
{
    if (channelId == chatModel->getActiveChannelId())
        return;

    if (guildId != cachedGuildId) {
        cachedGuildId = guildId;
        userColorCache.clear();
    }

    if (pendingLinkJump && pendingLinkJump->channelId != channelId)
        pendingLinkJump.reset();

    chatModel->setActiveChannel(channelId, guildId);
    typingTracker->setActiveChannel(channelId);
    messageInput->clearReplyTarget();
    chatView->setCanAddReactions(currentInstance && canAddReactionsIn(currentInstance, channelId, guildId));
}

// Session destroys a ClientInstance as soon as it reports Disconnected, so every
// pointer the UI holds into it has to be dropped while it is still alive.
void MainWindow::detachInstance(Core::Snowflake accountId)
{
    if (!currentInstance || currentInstance->accountId() != accountId)
        return;

    currentInstance->forums()->setCurrentForum({});

    auto *msgs = currentInstance->messages();
    disconnect(msgs, nullptr, chatModel, nullptr);
    disconnect(msgs, nullptr, this, nullptr);
    disconnect(msgs, nullptr, chatView, nullptr);
    disconnect(msgs->reactors(), nullptr, chatView, nullptr);
    disconnect(currentInstance->discord(), &Discord::Client::typingStart, this, nullptr);
    disconnect(currentInstance->permissions(), nullptr, this, nullptr);
    disconnect(currentInstance, &Core::ClientInstance::membersUpdated, this, nullptr);
    disconnect(currentInstance->relationships(), nullptr, this, nullptr);
    disconnect(memberListView, nullptr, currentInstance->memberList(), nullptr);
    disconnect(currentInstance->forums(), nullptr, this, nullptr);

    memberListModel->setManager(nullptr);
    memberListModel->setPresenceManager(nullptr);
    forumModel->setManager(nullptr);
    friendsView->setInstance(nullptr);
    typingTracker->clear();
    typingTracker->setUserManager(nullptr);
    if (expressionPicker)
        expressionPicker->hide();
    chatView->setEmojiManager(nullptr, Core::Snowflake());
    chatView->setFavoriteGifs(nullptr);

    currentInstance = nullptr;
}

void MainWindow::switchActiveInstance(Core::ClientInstance *newInstance)
{
    if (currentInstance) {
        currentInstance->forums()->setCurrentForum({});

        auto *msgs = currentInstance->messages();
        disconnect(msgs, nullptr, chatModel, nullptr);
        disconnect(msgs, nullptr, this, nullptr);
        disconnect(msgs, nullptr, chatView, nullptr);
        disconnect(msgs->reactors(), nullptr, chatView, nullptr);
        disconnect(currentInstance->discord(), &Discord::Client::typingStart, this, nullptr);
        disconnect(currentInstance->permissions(), nullptr, this, nullptr);
        disconnect(currentInstance, &Core::ClientInstance::membersUpdated, this, nullptr);
        disconnect(currentInstance->relationships(), nullptr, this, nullptr);
        disconnect(memberListView, nullptr, currentInstance->memberList(), nullptr);
        disconnect(currentInstance->forums(), nullptr, this, nullptr);
    }

    if (expressionPicker)
        expressionPicker->hide();

    currentInstance = newInstance;
    auto *msgs = currentInstance->messages();

    memberListModel->setAccount(currentInstance->accountId());
    memberListModel->setPresenceManager(currentInstance->presences());
    memberListModel->setManager(currentInstance->memberList());

    if (auto *video = chatView->videoController())
        video->setProxy(currentInstance->discord()->getProxy());

    chatModel->setAccount(currentInstance->accountId());
    messageInput->emojiAutocomplete()->cancel();
    messageInput->emojiAutocomplete()->setImageManager(session->getImageManager(), currentInstance->accountId());

    forumModel->setAccount(currentInstance->accountId());
    forumModel->setManager(currentInstance->forums());
    connect(currentInstance->forums(), &Core::ForumManager::loadingChanged, this,
            [this](Core::Snowflake forumId, bool loading) {
                if (forumId == currentForumId)
                    forumBrowser->setLoading(loading);
            });
    connect(memberListView, &MemberListView::visibleRangeChanged,
            currentInstance->memberList(), &Core::MemberListManager::updateSubscriptionRange);

    chatView->setCurrentUserId(currentInstance->accountId());
    chatView->setEmojiManager(currentInstance->emojis(), currentInstance->accountId());
    chatView->setFavoriteGifs(currentInstance->favoriteGifs());
    connect(currentInstance->favoriteGifs(), &Core::FavoriteGifs::limitReached, this, &MainWindow::showFavoriteGifLimit, Qt::UniqueConnection);

    typingTracker->clear();
    typingTracker->setUserManager(currentInstance->users());
    typingTracker->setCurrentUserId(currentInstance->accountId());

    connect(msgs, &MessageManager::messagesReceived, chatModel, &ChatModel::handleIncomingMessages);
    connect(msgs, &MessageManager::messageErrored, chatModel, &ChatModel::handleMessageErrored);
    connect(msgs, &MessageManager::messageDeleted, chatModel, &ChatModel::handleMessageDeleted);
    connect(msgs, &MessageManager::attachmentUploadProgress, chatModel, &ChatModel::handleUploadProgress);
    connect(msgs, &MessageManager::reactionRejected, this, [this](MessageManager::ReactionRejection reason) { showReactionRejection(this, reason); }, Qt::QueuedConnection);
    connect(msgs->reactors(), &Core::MessageReactors::reactorsChanged, chatView, &ChatView::refreshReactionTooltip);
    connect(msgs, &MessageManager::reactionsChanged, chatView, &ChatView::refreshReactionTooltip);
    connect(msgs, &MessageManager::messagesReceived, this,
            [this](const MessageRequestResult &result) {
                if (result.channelId != chatModel->getActiveChannelId())
                    return;
                if (result.type == Discord::Client::MessageLoadType::History) {
                    chatView->onHistoryRequestFinished();
                } else if (result.type == Discord::Client::MessageLoadType::Future) {
                    chatView->onFutureRequestFinished(!result.messages.isEmpty());
                } else if (result.type == Discord::Client::MessageLoadType::Latest) {
                    auto jump = std::exchange(pendingLinkJump, std::nullopt);
                    if (jump && result.success && jump->channelId == result.channelId)
                        chatView->jumpToMessage(jump->messageId);
                }
            });

    connect(currentInstance->discord(), &Discord::Client::typingStart, this,
            &MainWindow::onTypingStart);
    connect(currentInstance->discord(), &Discord::Client::messageCreated, this,
            [this](const Discord::Message &msg) {
                typingTracker->removeTyper(msg.channelId, msg.author->id);
            });

    connect(currentInstance->permissions(), &Core::PermissionManager::channelPermissionsChanged,
            this, &MainWindow::onChannelPermissionsChanged);

    connect(currentInstance, &Core::ClientInstance::membersUpdated, this,
            [this](Snowflake guildId, const QList<Snowflake> &userIds) {
                for (const auto &userId : userIds)
                    userColorCache.remove(userId);

                chatModel->refreshUsersInView(userIds);
                forumModel->refreshAuthors();
            });

    connect(currentInstance->relationships(), &Core::RelationshipManager::relationshipChanged, this,
            [this](Snowflake userId) { chatModel->refreshUsersInView({ userId }); });

    connect(currentInstance, &Core::ClientInstance::detailsUpdated, this,
            [this](const Core::AccountInfo &) { refreshUserPanelIdentity(); }, Qt::UniqueConnection);

    friendsView->setInstance(currentInstance);

    refreshUserPanelIdentity();
#ifndef ACHERON_NO_VOICE
    updateVoicePanel();
#endif
}

void MainWindow::setViewMode(ViewMode mode)
{
    viewMode = mode;

    if (currentInstance) {
        // the friends page owns no forum, so it releases the subscription the
        // same way a plain text channel switch does
        const bool chatFocused = mode == ViewMode::TextChannel || mode == ViewMode::Friends;
        currentInstance->forums()->setCurrentForum(chatFocused ? Core::Snowflake() : currentForumId);
    }

    switch (mode) {
    case ViewMode::TextChannel:
        forumBrowser->setVisible(false);
        threadPane->setVisible(true);
        threadHeader->setVisible(false);
        break;
    case ViewMode::ForumBrowse:
        forumBrowser->setVisible(true);
        threadPane->setVisible(false);
        threadHeader->setVisible(false);
        break;
    case ViewMode::ForumSplit:
        forumBrowser->setVisible(true);
        threadPane->setVisible(true);
        threadHeader->setVisible(true);
        popOutButton->setText(tr("Open as channel"));
        break;
    case ViewMode::ThreadPopout:
        forumBrowser->setVisible(false);
        threadPane->setVisible(true);
        threadHeader->setVisible(true);
        popOutButton->setText(tr("Back to forum"));
        break;
    case ViewMode::Friends:
        forumBrowser->setVisible(false);
        threadPane->setVisible(false);
        threadHeader->setVisible(false);
        channelToolbar->setVisible(false);
        break;
    }

    centerSplitter->setVisible(mode != ViewMode::Friends);
    friendsView->setVisible(mode == ViewMode::Friends);

    updateMemberListVisibility();
}

void MainWindow::setMemberListVisible(bool visible)
{
    memberListWanted = visible;
    updateMemberListVisibility();
}

void MainWindow::updateMemberListVisibility()
{
    const bool applicable = memberListWanted && viewMode == ViewMode::TextChannel;
    memberListView->setVisible(applicable);
    if (memberListToggle)
        memberListToggle->setEnabled(applicable);
    if (applicable)
        applyPaneCollapsed(MemberPaneIndex, memberListHidden, memberListRestoreWidth, DefaultMemberListWidth);
}

void MainWindow::setChannelListHidden(bool hidden)
{
    channelListHidden = hidden;
    applyPaneCollapsed(LeftPaneIndex, hidden, channelListRestoreWidth, DefaultChannelListWidth);
    syncPaneToggleActions();
}

void MainWindow::setMemberListHidden(bool hidden)
{
    memberListHidden = hidden;
    updateMemberListVisibility();
    syncPaneToggleActions();
}

void MainWindow::applyPaneCollapsed(int index, bool collapsed, int &restoreWidth, int defaultWidth)
{
    QList<int> sizes = mainSplitter->sizes();
    if (index >= sizes.size())
        return;

    if (collapsed) {
        if (sizes[index] > 0)
            restoreWidth = sizes[index];
        sizes[CenterPaneIndex] += sizes[index];
        sizes[index] = 0;
    } else {
        if (sizes[index] > 0)
            return;
        const int width = restoreWidth > 0 ? restoreWidth : defaultWidth;
        sizes[CenterPaneIndex] = qMax(0, sizes[CenterPaneIndex] - width);
        sizes[index] = width;
    }
    mainSplitter->setSizes(sizes);
}

void MainWindow::onSplitterMoved()
{
    const QList<int> sizes = mainSplitter->sizes();

    channelListHidden = sizes.value(LeftPaneIndex) == 0;
    if (!channelListHidden)
        channelListRestoreWidth = sizes.value(LeftPaneIndex);

    if (memberListView->isVisible()) {
        memberListHidden = sizes.value(MemberPaneIndex) == 0;
        if (!memberListHidden)
            memberListRestoreWidth = sizes.value(MemberPaneIndex);
    }

    syncPaneToggleActions();
}

void MainWindow::syncPaneToggleActions()
{
    if (channelListToggle) {
        QSignalBlocker blocker(channelListToggle);
        channelListToggle->setChecked(!channelListHidden);
    }
    if (memberListToggle) {
        QSignalBlocker blocker(memberListToggle);
        memberListToggle->setChecked(!memberListHidden);
    }
}

TabEntry MainWindow::makeTabEntry(ChannelNode *node, ChannelNode *accountNode) const
{
    ChannelNode *guildNode = ChannelTreeModel::findGuildNode(node);

    TabEntry entry;
    entry.channelId = node->id;
    entry.guildId = guildNode ? guildNode->id : Snowflake::Invalid;
    entry.accountId = accountNode->id;
    entry.name = node->name;
    entry.isDm = (node->type == ChannelNode::Type::DMChannel);
    entry.isForum = (node->type == ChannelNode::Type::Forum);
    if (guildNode && !guildNode->TEMP_iconHash.isEmpty())
        entry.iconUrl = Discord::Cdn::guildIcon(guildNode->id, guildNode->TEMP_iconHash, 64);
    return entry;
}

void MainWindow::openForumChannel(Core::ClientInstance *instance, Core::Snowflake forumId,
                                  Core::Snowflake guildId)
{
    Snowflake newestPost = instance->readState()->getChannelLastMessageId(forumId);
    if (newestPost.isValid())
        instance->readState()->markChannelAsRead(forumId, newestPost);
    instance->readState()->setActiveChannel(forumId);

    currentForumId = forumId;
    currentForumGuildId = guildId;
    forumBrowser->setSortMode(static_cast<int>(instance->forums()->sortMode(forumId)));
    forumModel->setForum(forumId, guildId);
    setViewMode(ViewMode::ForumBrowse);
    forumBrowser->setLoading(instance->forums()->isLoading(forumId));
}

void MainWindow::openForumPost(Core::Snowflake threadId, Core::Snowflake guildId)
{
    if (!currentInstance)
        return;

    setViewMode(ViewMode::ForumSplit);

    Core::Snowflake userId = currentInstance->accountId();
    bool canSend = currentInstance->permissions()->hasChannelPermission(userId, currentForumId, Discord::Permission::SEND_MESSAGES_IN_THREADS);
    messageInput->setEnabled(canSend);
    messageInput->setSendBlocked(false);
    messageInput->setMaxUploadSize(currentInstance->discord()->getMaxUploadSize(threadId));
    messageInput->setPlaceholder(canSend ? tr("Message this post")
                                         : tr("You do not have permission to send messages"));
    chatView->setCanPinMessages(false);
    chatView->setCanManageMessages(false);

    switchChatChannel(threadId, guildId);
    Snowflake lastMsgId = currentInstance->readState()->getChannelLastMessageId(threadId);
    currentInstance->readState()->markForumPostAsRead(threadId, lastMsgId.isValid() ? lastMsgId : threadId);
    currentInstance->readState()->setActiveChannel(threadId);
    currentInstance->messages()->requestLoadChannel(threadId);
}

void MainWindow::onNewPostRequested()
{
    if (!currentInstance || !currentForumId.isValid())
        return;

    Core::ClientInstance *instance = currentInstance;
    Core::Snowflake forumId = currentForumId;
    Core::Snowflake guildId = currentForumGuildId;

    auto *dialog = new NewPostDialog(instance->forums()->availableTags(forumId), instance->forums()->requiresTag(forumId), this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setMaxUploadSize(instance->discord()->getMaxUploadSize(forumId));

    connect(dialog, &NewPostDialog::submitted, this, [this, dialog, instance, forumId, guildId]() {
        dialog->setBusy(true);

        QPointer<NewPostDialog> guard(dialog);
        instance->discord()->createForumThread(
                forumId, dialog->title(), dialog->selectedTagIds(), dialog->content(),
                Core::Snowflake::generateNonce().toString(), dialog->attachments(),
                [this, guard, instance, forumId,
                 guildId](const Core::Result<Discord::Client::CreatedForumThread> &res) {
                    if (!res.success()) {
                        qCWarning(LogUI) << "Failed to create forum post:" << res.error;
                        if (guard)
                            guard->showError(tr("Could not post: %1").arg(res.error));
                        return;
                    }

                    if (guard)
                        guard->accept();

                    Core::Snowflake threadId = res.value->thread.id.get();
                    if (res.value->starterMessage)
                        instance->forums()->addStarterMessage(threadId, *res.value->starterMessage);
                    if (currentInstance == instance && currentForumId == forumId)
                        openForumPost(threadId, guildId);
                });
    });

    dialog->open();
}

void MainWindow::confirmAndLeaveGuild(Snowflake accountId, Snowflake guildId)
{
    ClientInstance *instance = session->client(accountId);
    if (!instance)
        return;

    auto guild = instance->getGuild(guildId);
    QString guildName = guild.has_value() ? guild->name.get() : QString::number(guildId);

    ConfirmPopup dialog(tr("Leave Server"),
                        tr("Are you sure you want to leave <b>%1</b>?").arg(guildName.toHtmlEscaped()),
                        tr("Leave"), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    instance->discord()->leaveGuild(guildId);
}

bool MainWindow::canManageStage(Snowflake accountId, Snowflake channelId)
{
    ClientInstance *instance = session->client(accountId);
    if (!instance)
        return false;

    const auto channel = instance->getChannel(channelId);
    if (!channel || !channel->type.hasValue() || channel->type.get() != Discord::ChannelType::GUILD_STAGE_VOICE)
        return false;

    return instance->permissions()->hasChannelPermission(accountId, channelId, Discord::Permission::MANAGE_CHANNELS);
}

void MainWindow::manageStage(Snowflake accountId, Snowflake channelId)
{
    ClientInstance *instance = session->client(accountId);
    if (!instance)
        return;

    QPointer<ClientInstance> instanceGuard(instance);
    instance->discord()->fetchStageInstance(channelId, [this, instanceGuard, channelId](const Core::Result<Discord::StageInstance> &result) {
        Discord::Client *client = instanceGuard ? instanceGuard->discord() : nullptr;
        if (!client)
            return;

        if (result.success()) {
            const QString topic = result.value->topic.valueOr();
            ConfirmPopup dialog(tr("End Stage"),
                                tr("Stage <b>%1</b> is live. End it for everyone?").arg(topic.toHtmlEscaped()),
                                tr("End Stage"), this);
            if (dialog.exec() != QDialog::Accepted)
                return;

            client->deleteStageInstance(channelId, [this](const Core::Result<void> &outcome) {
                if (!outcome.success())
                    QMessageBox::warning(this, tr("End Stage"), outcome.error);
            });
            return;
        }

        if (result.code != Discord::ApiError::UnknownStageInstance) {
            QMessageBox::warning(this, tr("Stage"), result.error);
            return;
        }

        bool accepted = false;
        const QString topic = QInputDialog::getText(this, tr("Start Stage"), tr("Stage topic:"),
                                                   QLineEdit::Normal, QString(), &accepted).trimmed();
        if (!accepted || topic.isEmpty())
            return;

        client->createStageInstance(channelId, topic, [this](const Core::Result<void> &outcome) {
            if (!outcome.success())
                QMessageBox::warning(this, tr("Start Stage"), outcome.error);
        });
    });
}

QList<GuildSettingsSection> MainWindow::guildSettingsSections(Snowflake accountId, Snowflake guildId)
{
    return GuildSettingsAccess::visibleSections(session->client(accountId), guildId);
}

void MainWindow::openGuildSettings(Snowflake accountId, Snowflake guildId, GuildSettingsSection section)
{
    ClientInstance *instance = session->client(accountId);
    if (!GuildSettingsAccess::canOpen(instance, guildId))
        return;

    QPointer<GuildSettingsWindow> &window = guildSettingsWindows[qMakePair(accountId, guildId)];
    if (!window) {
        window = new GuildSettingsWindow(session->getImageManager(), instance, guildId);
        connect(window, &GuildSettingsWindow::linkActivated, chatView, &ChatView::openLink);
    }
    window->openSection(section);
    window->setWindowState(window->windowState() & ~Qt::WindowMinimized);
    window->show();
    window->raise();
    window->activateWindow();
}

void MainWindow::showUserProfile(Core::ClientInstance *instance, Snowflake userId, Snowflake guildId, QWidget *aboveModalPopup)
{
    auto *popup = new UserProfilePopup(session->getImageManager(), instance, userId, guildId, aboveModalPopup ? aboveModalPopup : this);
    connect(popup, &UserProfilePopup::linkActivated, chatView, &ChatView::openLink);
    popup->show();
}

void MainWindow::setupPermanentConnections(Core::ClientInstance *instance)
{
    if (!instance)
        return;

    if (instancesSignalsConnected.contains(instance->accountId()))
        return;
    instancesSignalsConnected.insert(instance->accountId());

    connect(instance, &Core::ClientInstance::guildCreated, this,
            [this, instance](const Discord::GatewayGuild &guild) {
                channelTreeModel->addGuild(guild, instance->accountId());
                if (channelListMode == ChannelListMode::Tree)
                    channelTree->performDefaultExpansion();
            });

    connect(instance, &Core::ClientInstance::guildUpdated, this, [this, instance](const Discord::Guild &guild) {
        channelTreeModel->updateGuild(guild, instance->accountId());
        if (railHasSelection && !railSelectedIsHome && railSelectedAccountId == instance->accountId() && railSelectedGuildId == guild.id.get())
            guildHeaderLabel->setText(guild.name);
    });

    connect(instance, &Core::ClientInstance::guildRemoved, this,
            [this, instance](Snowflake guildId) {
                channelTreeModel->removeGuild(instance->accountId(), guildId);
            });

    connect(instance->discord(), &Discord::Client::guildLeaveFailed, this,
            [this, instance](Snowflake guildId, const QString &error) {
                auto guild = instance->getGuild(guildId);
                QString guildName = guild.has_value() ? guild->name.get() : QString::number(guildId);
                QMessageBox::warning(this,
                                     tr("Failed to leave server"),
                                     tr("Could not leave <b>%1</b>.<br>Error: %2")
                                             .arg(guildName.toHtmlEscaped(), error));
            });

    connect(instance, &Core::ClientInstance::channelCreated, this,
            [this, instance](const Discord::ChannelCreate &event) {
                channelTreeModel->addChannel(event, instance->accountId());
            });

    connect(instance, &Core::ClientInstance::channelUpdated, this,
            [this, instance](const Discord::ChannelUpdate &update) {
                channelTreeModel->updateChannel(update, instance->accountId());

                if (!update.channel.hasValue())
                    return;
                const auto &ch = update.channel.get();
                if (!ch.id.hasValue() || ch.id.get() != chatModel->getActiveChannelId())
                    return;
                if (instance != currentInstance)
                    return;

                showChannelTopic(instance, ch.id.get());

                int rateLimit = ch.rateLimitPerUser.hasValue() ? ch.rateLimitPerUser.get() : 0;
                Snowflake userId = instance->accountId();
                bool canBypass = instance->permissions()->hasChannelPermission(
                        userId, ch.id.get(), Discord::Permission::BYPASS_SLOWMODE);
                slowModeIndicator->setSlowMode(ch.id.get(), rateLimit, canBypass);
            });

    connect(instance, &Core::ClientInstance::channelDeleted, this,
            [this, instance](const Discord::ChannelDelete &event) {
                channelTreeModel->deleteChannel(event, instance->accountId());
            });

    connect(instance, &Core::ClientInstance::threadCreated, this,
            [this, instance](const Discord::Channel &thread) {
                channelTreeModel->addThread(thread, instance->accountId());
            });
    connect(instance, &Core::ClientInstance::threadUpdated, this,
            [this, instance](const Discord::Channel &thread) {
                channelTreeModel->updateThread(thread, instance->accountId());
            });
    connect(instance, &Core::ClientInstance::threadDeleted, this,
            [this, instance](Core::Snowflake threadId, Core::Snowflake, Core::Snowflake) {
                channelTreeModel->removeThread(threadId, instance->accountId());
            });
    connect(instance, &Core::ClientInstance::threadListSynced, this,
            [this, instance](Core::Snowflake guildId, const QList<Core::Snowflake> &parentIds,
                             const QList<Discord::Channel> &threads) {
                channelTreeModel->syncThreads(guildId, parentIds, threads, instance->accountId());
            });
    connect(instance, &Core::ClientInstance::threadMembershipChanged, this,
            [this, instance](Core::Snowflake threadId) {
                Core::Snowflake acc = instance->accountId();
                if (instance->isThreadJoined(threadId)) {
                    if (channelTreeModel->findChannelTreeNode(threadId, acc))
                        channelTreeModel->promoteTemporaryThread(threadId);
                    else if (auto ch = instance->getChannel(threadId))
                        channelTreeModel->addThread(*ch, acc);
                    channelTreeModel->updateReadState(threadId, acc);
                } else if (chatModel->getActiveChannelId() == threadId) {
                    if (auto ch = instance->getChannel(threadId))
                        channelTreeModel->showTemporaryThread(*ch, acc);
                } else {
                    channelTreeModel->removeThread(threadId, acc);
                }
            });

    // todo: i dont really like the refresh users logic rn
    connect(instance, &Core::ClientInstance::guildRoleCreated, this,
            [this](const Discord::GuildRoleCreate &event) {
                if (event.guildId.hasValue())
                    refreshGuildRoleData(event.guildId.get());
            });

    connect(instance, &Core::ClientInstance::guildRoleUpdated, this,
            [this](const Discord::GuildRoleUpdate &event) {
                if (event.guildId.hasValue())
                    refreshGuildRoleData(event.guildId.get());
            });

    connect(instance, &Core::ClientInstance::guildRoleDeleted, this,
            [this](const Discord::GuildRoleDelete &event) {
                if (event.guildId.hasValue())
                    refreshGuildRoleData(event.guildId.get());
            });

    connect(instance, &Core::ClientInstance::readStateChanged, this,
            [this, instance](Core::Snowflake channelId) {
                channelTreeModel->updateReadState(channelId, instance->accountId());
                refreshTabReadStates();
                forumModel->refreshPost(channelId);
            });

    connect(instance->presences(), &Core::PresenceManager::presencesChanged, this,
            [this, instance](const QList<Core::Snowflake> &userIds) {
                channelTreeModel->updatePresence(instance->accountId(), userIds);
                if (instance == currentInstance)
                    refreshUserPanelIdentity();
            });

    connect(instance, &Core::ClientInstance::forumBadgeChanged, this,
            [this, instance](Core::Snowflake forumId) {
                channelTreeModel->updateForumBadge(forumId, instance->accountId());
            });

    connect(instance, &Core::ClientInstance::forumJoinedPostsChanged, this,
            [this, instance](Core::Snowflake forumId) {
                channelTreeModel->updateForumThreads(forumId, instance->accountId());
            });

    connect(instance, &Core::ClientInstance::channelLastMessageUpdated, this,
            [this, instance](Core::Snowflake channelId, Core::Snowflake messageId) {
                channelTreeModel->updateChannelLastMessageId(channelId, messageId,
                                                             instance->accountId());
                refreshTabReadStates();
            });

    connect(instance, &Core::ClientInstance::guildSettingsChanged, this,
            [this, instance](Core::Snowflake guildId) {
                channelTreeModel->updateGuildSettings(guildId, instance->accountId());
            });

    connect(instance, &Core::ClientInstance::reconnecting, this,
            [this](int attempt, int maxAttempts) {
                connectionBanner->showReconnecting(attempt, maxAttempts);
            });

    connect(instance, &Core::ClientInstance::stateChanged, this,
            [this](Core::ConnectionState state) {
                if (state == Core::ConnectionState::Connected)
                    connectionBanner->hide();
            });

    connect(instance, &Core::ClientInstance::authenticationFailed, this,
            [this](const Core::AccountInfo &info) {
                connectionBanner->hide();
                const QString accountLabel = info.displayName.isEmpty() ? info.username
                                                                        : info.displayName;
                QWidget *parent = this;
                auto *box = new QMessageBox(parent);
                box->setAttribute(Qt::WA_DeleteOnClose);
                box->setIcon(QMessageBox::Critical);
                box->setWindowTitle(tr("Authentication Failed"));
                box->setText(tr("Discord rejected the token for account \"%1\".").arg(accountLabel));
                box->setInformativeText(tr("Discord's gateway rejected your token. The stored token is invalid. Check or update your token and try again."));
                box->setStandardButtons(QMessageBox::Ok);
                box->setWindowModality(Qt::WindowModal);
                box->show();
            });

    connect(instance, &Core::ClientInstance::voiceStateChanged, this,
            [this, instance](Core::Snowflake channelId, Core::Snowflake) {
                channelTree->setAccountVoiceChannel(instance->accountId(), channelId);
                channelTreeModel->setAccountVoiceChannel(instance->accountId(), channelId);
#ifndef ACHERON_NO_VOICE
                updateVoicePanel();
#endif
            });

#ifndef ACHERON_NO_VOICE
    connect(instance->voice(), &Core::Audio::VoiceManager::voiceStateChanged,
            this, &MainWindow::updateVoicePanel);

    connect(instance->voice(), &Core::Audio::VoiceManager::channelVoiceMemberChanged,
            this, [this, instance](Core::Snowflake channelId, Core::Snowflake userId, bool joined) {
                int count = instance->voice()->channelVoiceUserCount(channelId);
                channelTreeModel->updateVoiceCount(channelId, count, instance->accountId());
                channelTreeModel->updateVoiceParticipant(channelId, userId, joined,
                                                         instance->accountId());
            });

    connect(instance->voice(), &Core::Audio::VoiceManager::participantVoiceStateChanged,
            this, [this, instance](Core::Snowflake channelId, Core::Snowflake userId) {
                channelTreeModel->updateVoiceParticipantState(channelId, userId,
                                                              instance->accountId());
            });
#endif
}

void MainWindow::setupUi()
{
    auto *central = new QWidget(this);
    auto *layout = new QHBoxLayout(central);

    channelTree = new ChannelTreeView(this);
    channelTree->setObjectName(QStringLiteral("ChannelTree"));

    userPanel = new UserPanel(this);
    userPanel->setImageManager(session->getImageManager());
    connect(userPanel, &UserPanel::settingsRequested, this, &MainWindow::openSettingsWindow);
#ifndef ACHERON_NO_VOICE
    connect(userPanel, &UserPanel::disconnectRequested, this, [this]() {
        for (const auto &inst : session->getClients()) {
            if (inst && inst->isInVoice()) {
                inst->discord()->sendVoiceStateUpdate(inst->voiceGuildId(), Snowflake::Invalid, false, false);
                break;
            }
        }
    });
    connect(userPanel, &UserPanel::voiceFlagsRequested, this, [this](bool mute, bool deaf) {
        for (const auto &inst : session->getClients()) {
            if (inst && inst->isInVoice()) {
                inst->discord()->sendVoiceStateUpdate(inst->voiceGuildId(), inst->voiceChannelId(), mute,
                                                      deaf);
                break;
            }
        }
    });
    connect(userPanel, &UserPanel::handToggleRequested, this, [this](bool raise) {
        for (const auto &inst : session->getClients()) {
            if (!inst || !inst->isInVoice())
                continue;
            if (raise)
                inst->discord()->raiseHand(inst->voiceGuildId());
            else
                inst->discord()->lowerHand(inst->voiceGuildId());
            break;
        }
    });
#endif

    serverRail = new ServerRailView(this);
    serverRail->setObjectName(QStringLiteral("ServerRail"));
    serverRail->setModel(serverRailModel);
    serverRail->setItemDelegate(new ServerRailDelegate(serverRail));
    serverRail->setFixedWidth(ServerRailDelegate::RailWidth);
    serverRail->hide();
    connect(serverRail, &ServerRailView::accountHomeClicked, this, &MainWindow::onRailAccountHomeClicked);
    connect(serverRail, &ServerRailView::guildClicked, this, &MainWindow::onRailGuildClicked);
    connect(serverRail, &ServerRailView::folderToggleClicked, this,
            [this](Snowflake accountId, Snowflake folderId) {
                serverRailModel->toggleFolder(accountId, folderId);
            });
    connect(serverRail, &ServerRailView::markAsReadRequested, this,
            [this](Snowflake accountId, Snowflake id, bool isFolder) {
                markIndexAsRead(accountId, isFolder ? channelTreeModel->folderIndex(accountId, id)
                                                    : channelTreeModel->serverIndex(accountId, id));
            });

    connect(serverRail, &ServerRailView::leaveGuildRequested, this, &MainWindow::confirmAndLeaveGuild);
    serverRail->setGuildSettingsProvider(std::bind_front(&MainWindow::guildSettingsSections, this));
    connect(serverRail, &ServerRailView::guildSettingsRequested, this, &MainWindow::openGuildSettings);

    guildHeaderLabel = new QLabel(this);
    guildHeaderLabel->setObjectName(QStringLiteral("GuildHeader"));
    guildHeaderLabel->setContentsMargins(12, 8, 12, 8);
    {
        QFont headerFont = guildHeaderLabel->font();
        headerFont.setBold(true);
        guildHeaderLabel->setFont(headerFont);
    }
    guildHeaderLabel->hide();

    auto *rightSideWidget = new QWidget(central);
    rightSideWidget->setObjectName(QStringLiteral("ChatPane"));
    rightSideWidget->setAttribute(Qt::WA_StyledBackground, true);
    auto *rightLayout = new QVBoxLayout(rightSideWidget);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    connectionBanner = new ConnectionBanner(rightSideWidget);
    tabBar = new TabBar(session->getImageManager(), rightSideWidget);

    threadBrowserButton = new QToolButton(rightSideWidget);
    threadBrowserButton->setText(tr("Threads"));
    threadBrowserButton->setIcon(Core::Theme::Icons::icon(Core::Theme::Icons::Name::Spool, Core::Theme::Token::PrimaryText));
    threadBrowserButton->setIconSize(QSize(16, 16));
    threadBrowserButton->setToolTip(tr("Browse threads"));
    threadBrowserButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    threadBrowserButton->setAutoRaise(true);
    threadBrowserButton->setCursor(Qt::PointingHandCursor);
    connect(threadBrowserButton, &QToolButton::clicked, this, &MainWindow::openThreadBrowser);

    channelToolbar = new QWidget(rightSideWidget);
    channelToolbar->setObjectName("channelToolbar");
    channelToolbar->setAttribute(Qt::WA_StyledBackground, true);
    auto *channelToolbarLayout = new QHBoxLayout(channelToolbar);
    channelToolbarLayout->setContentsMargins(8, 3, 8, 3);
    channelToolbarLayout->setSpacing(4);
    channelTopicLine = new ChannelTopicLine(session->getImageManager(), channelToolbar);
    connect(channelTopicLine, &ChannelTopicLine::clicked, this, &MainWindow::openChannelTopicPopup);
    channelToolbarLayout->addWidget(channelTopicLine, 1);
    channelToolbarLayout->addWidget(threadBrowserButton, 0);
    channelToolbar->hide();

    chatView = new ChatView(rightSideWidget);
    chatView->setFont(Core::Theme::Manager::instance().font(Core::Theme::FontRole::Message));
    messageInput = new MessageInput(rightSideWidget);
    messageInput->emojiAutocomplete()->setProvider([this](const QString &query) -> QList<Core::EmojiMatch> {
        if (!currentInstance)
            return {};
        Snowflake channelId = chatModel->getActiveChannelId();
        if (!channelId.isValid())
            return {};
        return currentInstance->emojis()->search(query, channelId);
    });
    typingIndicator = new TypingIndicator(rightSideWidget);
    slowModeIndicator = new SlowModeIndicator(rightSideWidget);

    auto *statusRow = new QWidget(rightSideWidget);
    auto *statusRowLayout = new QHBoxLayout(statusRow);
    statusRowLayout->setContentsMargins(0, 0, 0, 0);
    statusRowLayout->setSpacing(0);
    statusRowLayout->addWidget(typingIndicator, 1);
    statusRowLayout->addWidget(slowModeIndicator, 0);
    statusRow->setFixedHeight(typingIndicator->minimumHeight());

    connect(slowModeIndicator, &SlowModeIndicator::cooldownChanged, this,
            [this](bool onCooldown) {
                if (!currentInstance)
                    return;
                Snowflake channelId = chatModel->getActiveChannelId();
                if (!channelId.isValid())
                    return;
                messageInput->setSendBlocked(onCooldown);
                if (!onCooldown) {
                    QModelIndex current = channelTree->currentIndex();
                    if (current.isValid()) {
                        auto *node = channelTreeModel->nodeFromIndex(
                                channelFilterProxy->mapToSource(current));
                        if (node)
                            messageInput->setPlaceholder("Message #" + node->name);
                    }
                }
            });

    threadHeader = new QWidget(rightSideWidget);
    auto *threadHeaderLayout = new QHBoxLayout(threadHeader);
    threadHeaderLayout->setContentsMargins(8, 4, 8, 4);
    popOutButton = new QPushButton(tr("Open as channel"), threadHeader);
    closeThreadButton = new QPushButton(tr("Close"), threadHeader);
    threadHeaderLayout->addStretch(1);
    threadHeaderLayout->addWidget(popOutButton, 0);
    threadHeaderLayout->addWidget(closeThreadButton, 0);
    threadHeader->setVisible(false);

    connect(popOutButton, &QPushButton::clicked, this, [this]() {
        setViewMode(viewMode == ViewMode::ThreadPopout ? ViewMode::ForumSplit
                                                       : ViewMode::ThreadPopout);
    });
    connect(closeThreadButton, &QPushButton::clicked, this,
            [this]() { setViewMode(ViewMode::ForumBrowse); });

    threadPane = new QWidget(rightSideWidget);
    auto *threadPaneLayout = new QVBoxLayout(threadPane);
    threadPaneLayout->setContentsMargins(0, 0, 0, 0);
    threadPaneLayout->setSpacing(0);
    threadPaneLayout->addWidget(threadHeader, 0);
    threadPaneLayout->addWidget(chatView, 1);
    threadPaneLayout->addWidget(statusRow, 0);
    threadPaneLayout->addWidget(messageInput, 0);

    forumModel = new ForumPostModel(session->getImageManager(), this);
    forumModel->setDisplayNameResolver([this](const Discord::User &author, Snowflake guildId) -> QString {
        if (!currentInstance)
            return QString();
        return currentInstance->users()->getAuthorDisplayName(author, guildId);
    });
    forumModel->setRoleColorResolver([this](Snowflake userId, Snowflake guildId) { return resolveRoleColor(userId, guildId); });
    forumBrowser = new ForumBrowser(rightSideWidget);
    forumBrowser->setModel(forumModel);
    forumBrowser->setVisible(false);

    connect(forumBrowser, &ForumBrowser::postActivated, this, &MainWindow::openForumPost);
    connect(forumBrowser, &ForumBrowser::newPostRequested, this, &MainWindow::onNewPostRequested);
    connect(forumBrowser, &ForumBrowser::sortModeChanged, this, [this](int mode) {
        if (currentInstance && currentForumId.isValid())
            currentInstance->forums()->setSortMode(currentForumId, static_cast<Core::ForumSortMode>(mode));
    });

    centerSplitter = new QSplitter(Qt::Horizontal, rightSideWidget);
    centerSplitter->addWidget(forumBrowser);
    centerSplitter->addWidget(threadPane);
    centerSplitter->setStretchFactor(0, 1);
    centerSplitter->setStretchFactor(1, 1);
    centerSplitter->setSizes({ 350, 550 });

    friendsView = new FriendsView(session, rightSideWidget);
    friendsView->setVisible(false);
    connect(friendsView, &FriendsView::directMessageRequested, this, [this](Snowflake userId) {
        ClientInstance *instance = currentInstance;
        if (!instance)
            return;
        Snowflake accountId = instance->accountId();
        instance->openDmChannel(userId, [this, accountId](const Core::Result<Snowflake> &res) {
            if (res.success())
                selectChannelInTree(accountId, *res.value);
        });
    });
    connect(friendsView, &FriendsView::profileRequested, this, [this](Snowflake userId) {
        if (currentInstance)
            showUserProfile(currentInstance, userId, Snowflake::Invalid);
    });

    rightLayout->addWidget(connectionBanner, 0);
    rightLayout->addWidget(tabBar, 0);
    rightLayout->addWidget(channelToolbar, 0);
    rightLayout->addWidget(centerSplitter, 1);
    rightLayout->addWidget(friendsView, 1);

    memberListView = new MemberListView(central);
    memberListView->setModel(memberListModel);
    memberListView->setItemDelegate(new MemberListDelegate(memberListView));

    connect(&Core::Theme::Manager::instance(), &Core::Theme::Manager::themeChanged, this, [this]() {
        chatModel->invalidateDocCache();
        chatView->viewport()->update();
        channelTree->viewport()->update();
        memberListView->viewport()->update();
        refreshChannelTopic();
    });

    connect(&Core::Theme::Manager::instance(), &Core::Theme::Manager::metricsChanged, this, [this]() {
        chatView->setFont(Core::Theme::Manager::instance().font(Core::Theme::FontRole::Message));
        chatModel->invalidateLayout();
        chatView->doItemsLayout();
        channelTree->viewport()->update();
        memberListView->viewport()->update();
        refreshChannelTopic();
    });

    connect(memberListView, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) {
                QModelIndex idx = memberListView->indexAt(pos);
                if (!idx.isValid())
                    return;
                if (idx.data(MemberListModel::ItemTypeRole).toInt() != static_cast<int>(Core::MemberListItem::Type::Member))
                    return;
                Snowflake userId = idx.data(MemberListModel::UserIdRole).toULongLong();
                Snowflake guildId = currentInstance
                                            ? currentInstance->memberList()->currentGuildId()
                                            : Snowflake::Invalid;
                showUserContextMenu(currentInstance, userId, guildId,
                                    memberListView->viewport()->mapToGlobal(pos));
            });

    leftSideWidget = buildLeftSide();

    mainSplitter = new QSplitter(this);
    mainSplitter->addWidget(leftSideWidget);
    mainSplitter->addWidget(rightSideWidget);
    mainSplitter->addWidget(memberListView);

    mainSplitter->setCollapsible(LeftPaneIndex, true);
    mainSplitter->setCollapsible(MemberPaneIndex, true);
    mainSplitter->setStretchFactor(0, 0);
    mainSplitter->setStretchFactor(1, 1);
    mainSplitter->setStretchFactor(2, 0);
    memberListView->setMinimumWidth(140);
    memberListView->setMaximumWidth(400);
    connect(mainSplitter, &QSplitter::splitterMoved, this, &MainWindow::onSplitterMoved);

    memberListView->hide();

    channelTree->setModel(channelFilterProxy);
    channelTree->setMentionJumpEnabled(channelListMode == ChannelListMode::Tree);
    channelTree->setHeaderHidden(true);
    channelTree->setIndentation(0);
    channelDelegate = new ChannelDelegate(channelFilterProxy, channelTree);
    channelTree->setItemDelegate(channelDelegate);
    applyChannelIndent();
    channelTree->setIconSize(QSize(24, 24));
    channelTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    channelTree->setFrameShape(QFrame::NoFrame);
    channelTree->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    channelTree->setExpandsOnDoubleClick(false);

    connect(channelTree, &ChannelTreeView::markAsReadRequested, this,
            [this](const QModelIndex &proxyIndex) {
                QModelIndex sourceIndex = channelFilterProxy->mapToSource(proxyIndex);
                auto *node = channelTreeModel->nodeFromIndex(sourceIndex);
                if (!node)
                    return;

                ChannelNode *accountNode = channelTreeModel->getAccountNodeFor(node);
                if (!accountNode)
                    return;

                markIndexAsRead(accountNode->id, sourceIndex);
            });

    chatView->videoController()->setUrlRefresher(
            [this](const QUrl &stale, std::function<void(const QUrl &)> done) {
                if (!currentInstance) {
                    done(QUrl());
                    return;
                }
                currentInstance->discord()->refreshAttachmentUrls(
                        { stale }, [stale, done](const Core::Result<QHash<QUrl, QUrl>> &result) {
                            done(result.success() ? result.value->value(stale) : QUrl());
                        });
            });

    chatView->setModel(chatModel);
    chatView->setImageManager(session->getImageManager());
    Core::AnimatedImageCache *animatedImages = session->getAnimatedImageCache();
    animatedImages->setCacheLimitMiB(QSettings().value("chat/animation_cache_mb", Core::AnimatedImageCache::CacheLimit.fallback).toInt());
    animatedImages->setAnimationLimitMiB(QSettings().value("chat/animation_max_mb", Core::AnimatedImageCache::AnimationLimit.fallback).toInt());
    chatView->frameAnimator()->setCache(animatedImages);
    chatView->frameAnimator()->setEmojiEnabled(QSettings().value("chat/animate_emoji", true).toBool());
    chatView->frameAnimator()->setStickersEnabled(QSettings().value("chat/animate_stickers", true).toBool());
    applyGifSettings(chatView->gifPlayback());
    chatView->setItemDelegate(new ChatDelegate(session->getImageManager(), chatView));
    chatView->setIconSize(QSize(24, 24));
    chatView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    chatView->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    chatView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    chatView->setUniformItemSizes(false);
    chatView->setWordWrap(true);
    chatView->setResizeMode(QListView::Adjust);

    connect(messageInput, &MessageInput::sendMessage, this, &MainWindow::sendFromChatInput);

    connect(messageInput, &MessageInput::editLastMessageRequested, chatView, &ChatView::editLastOwnMessage);
    connect(chatView, &ChatView::inlineEditFinished, messageInput, qOverload<>(&QWidget::setFocus));

    connect(chatView, &ChatView::historyRequested, this, [this]() {
        Snowflake oldestId = chatModel->getOldestMessageId();

        if (currentInstance && oldestId.isValid())
            currentInstance->messages()->requestLoadHistory(chatModel->getActiveChannelId(),
                                                            oldestId);
    });

    connect(chatView, &ChatView::futureRequested, this, [this]() {
        Snowflake newestId = chatModel->getNewestMessageId();

        if (currentInstance && newestId.isValid())
            currentInstance->messages()->requestLoadFuture(chatModel->getActiveChannelId(), newestId);
    });

    connect(chatView, &ChatView::jumpRequested, this, [this](Snowflake messageId) {
        if (currentInstance)
            currentInstance->messages()->requestLoadAround(chatModel->getActiveChannelId(), messageId);
    });

    connect(chatView, &ChatView::presentRequested, this, [this]() {
        if (currentInstance)
            currentInstance->messages()->requestLoadChannel(chatModel->getActiveChannelId());
    });

    connect(chatView, &ChatView::filesDropped, this, [this](const QList<QUrl> &urls) {
        messageInput->queueAttachments(urls);
    });

    connect(chatView, &ChatView::cancelUploadRequested, this,
            [this](Snowflake channelId, Snowflake messageId) {
                if (currentInstance)
                    currentInstance->messages()->cancelSend(channelId, QString::number(messageId));
            });

    connect(chatView, &ChatView::deleteMessageRequested, this,
            [this](Snowflake channelId, Snowflake messageId) {
                if (!currentInstance)
                    return;

                if (QApplication::keyboardModifiers() & Qt::ShiftModifier) {
                    currentInstance->discord()->deleteMessage(channelId, messageId);
                    return;
                }

                ConfirmPopup dialog(tr("Delete Message"),
                                    tr("Are you sure you want to delete this message?"),
                                    tr("Delete"), this);
                if (dialog.exec() == QDialog::Accepted)
                    currentInstance->discord()->deleteMessage(channelId, messageId);
            });

    connect(chatView, &ChatView::pinMessageRequested, this,
            [this](Snowflake channelId, Snowflake messageId) {
                if (currentInstance)
                    currentInstance->discord()->pinMessage(channelId, messageId);
            });

    connect(chatView, &ChatView::editMessageRequested, this,
            [this](Snowflake channelId, Snowflake messageId, const QString &content) {
                if (currentInstance)
                    currentInstance->discord()->editMessage(channelId, messageId, content);
            });

    connect(chatView, &ChatView::replyToMessageRequested, this,
            [this](Snowflake channelId, Snowflake messageId) {
                // Find the message in the model to get author name and content
                for (int row = 0; row < chatModel->rowCount(); ++row) {
                    QModelIndex idx = chatModel->index(row, 0);
                    Snowflake msgId = idx.data(ChatModel::MessageIdRole).toULongLong();
                    if (msgId == messageId) {
                        QString authorName = idx.data(ChatModel::UsernameRole).toString();
                        QString content = idx.data(ChatModel::ContentRole).toString();
                        messageInput->setReplyTarget(messageId, authorName, content);
                        return;
                    }
                }
                // Fallback if message not found in model
                messageInput->setReplyTarget(messageId, tr("Unknown"), QString());
            });

    connect(chatView, &ChatView::reactionPickerRequested, this, &MainWindow::openReactionPicker);
    connect(messageInput, &MessageInput::emojiPickerRequested, this, [this](const QRect &globalAnchor) { openChatPicker(ExpressionPicker::Tab::Emoji, globalAnchor); });
    connect(messageInput, &MessageInput::gifPickerRequested, this, [this](const QRect &globalAnchor) { openChatPicker(ExpressionPicker::Tab::Gifs, globalAnchor); });

    connect(chatView, &ChatView::reactionToggleRequested, this,
            [this](Snowflake channelId, Snowflake messageId, const Discord::Emoji &emoji, bool currentlyReacted, bool isBurst, Discord::Client::ReactionLocation location) {
                if (!currentInstance)
                    return;
                if (currentlyReacted)
                    currentInstance->messages()->removeReaction(channelId, messageId, emoji, isBurst, location);
                else
                    currentInstance->messages()->addReaction(channelId, messageId, emoji, isBurst, location);
            });

    connect(chatView, &ChatView::reactorsRequested, this, &MainWindow::openReactors);
    chatView->setReactionTooltip([this](const Core::ReactionRef &reaction, int reactionCount) {
        return currentInstance ? reactionTooltip(currentInstance, chatModel->getActiveChannelId(), chatModel->getActiveGuildId(), reaction, reactionCount) : QString();
    });

    connect(chatView, &ChatView::userContextMenuRequested, this,
            [this](Snowflake userId, QPoint globalPos) {
                showUserContextMenu(currentInstance, userId, cachedGuildId, globalPos);
            });

    connect(chatView, &ChatView::channelMentionClicked, this, &MainWindow::navigateToChannel);
    connect(chatView, &ChatView::messageLinkClicked, this, &MainWindow::jumpToMessageLink);
    connect(chatView, &ChatView::atBottomChanged, this, [this](bool atBottom) {
        if (currentInstance)
            currentInstance->readState()->setActiveChannelAtBottom(atBottom);
    });

    connect(channelTree->selectionModel(), &QItemSelectionModel::currentChanged, this,
            &MainWindow::onChannelSelectionChanged);

    connect(tabBar, &TabBar::tabChanged, this, &MainWindow::switchToTabEntry);

    connect(channelTree, &ChannelTreeView::voiceParticipantContextMenuRequested, this,
            [this](const QModelIndex &proxyIndex, QPoint globalPos) {
                QModelIndex sourceIndex = channelFilterProxy->mapToSource(proxyIndex);
                auto *node = channelTreeModel->nodeFromIndex(sourceIndex);
                if (!node || node->type != ChannelNode::Type::VoiceParticipant)
                    return;

                ChannelNode *accountNode = channelTreeModel->getAccountNodeFor(node);
                if (!accountNode)
                    return;

                ChannelNode *guildNode = ChannelTreeModel::findGuildNode(node);
                Snowflake guildId = guildNode ? guildNode->id : Snowflake::Invalid;
                showUserContextMenu(session->client(accountNode->id), node->id, guildId, globalPos);
            });

    connect(channelTree, &ChannelTreeView::openInNewTabRequested, this,
            [this](const QModelIndex &proxyIndex) {
                QModelIndex sourceIndex = channelFilterProxy->mapToSource(proxyIndex);
                auto *node = channelTreeModel->nodeFromIndex(sourceIndex);
                if (!node)
                    return;

                ChannelNode *accountNode = channelTreeModel->getAccountNodeFor(node);
                if (!accountNode)
                    return;

                tabBar->openNewTab(makeTabEntry(node, accountNode));
            });

    auto threadMembership = [this](const QModelIndex &proxyIndex, bool join) {
        QModelIndex sourceIndex = channelFilterProxy->mapToSource(proxyIndex);
        auto *node = channelTreeModel->nodeFromIndex(sourceIndex);
        if (!node || node->type != ChannelNode::Type::Thread)
            return;
        ChannelNode *accountNode = channelTreeModel->getAccountNodeFor(node);
        if (!accountNode)
            return;
        auto *instance = session->client(accountNode->id);
        if (!instance)
            return;
        if (join)
            instance->discord()->joinThread(node->id);
        else
            instance->discord()->leaveThread(node->id);
    };
    connect(channelTree, &ChannelTreeView::joinThreadRequested, this, [threadMembership](const QModelIndex &proxyIndex) { threadMembership(proxyIndex, true); });
    connect(channelTree, &ChannelTreeView::leaveThreadRequested, this, [threadMembership](const QModelIndex &proxyIndex) { threadMembership(proxyIndex, false); });

    connect(channelTree, &ChannelTreeView::leaveGuildRequested, this, &MainWindow::confirmAndLeaveGuild);
    channelTree->setGuildSettingsProvider(std::bind_front(&MainWindow::guildSettingsSections, this));
    connect(channelTree, &ChannelTreeView::guildSettingsRequested, this, &MainWindow::openGuildSettings);

    channelTree->setStageAccessProvider([this](Snowflake accountId, Snowflake channelId) {
        return canManageStage(accountId, channelId);
    });
    connect(channelTree, &ChannelTreeView::manageStageRequested, this, [this](const QModelIndex &proxyIndex) {
        QModelIndex sourceIndex = channelFilterProxy->mapToSource(proxyIndex);
        auto *node = channelTreeModel->nodeFromIndex(sourceIndex);
        if (!node)
            return;

        ChannelNode *accountNode = channelTreeModel->getAccountNodeFor(node);
        if (!accountNode)
            return;

        manageStage(accountNode->id, node->id);
    });

#ifndef ACHERON_NO_VOICE
    connect(channelTree, &ChannelTreeView::joinVoiceChannelRequested, this,
            [this](const QModelIndex &proxyIndex) {
                QModelIndex sourceIndex = channelFilterProxy->mapToSource(proxyIndex);
                auto *node = channelTreeModel->nodeFromIndex(sourceIndex);
                if (!node)
                    return;

                bool isDM = (node->type == ChannelNode::Type::DMChannel);
                bool isVoice = (node->type == ChannelNode::Type::VoiceChannel);
                if (!isDM && !isVoice)
                    return;

                ChannelNode *accountNode = channelTreeModel->getAccountNodeFor(node);
                if (!accountNode)
                    return;

                ClientInstance *instance = session->client(accountNode->id);
                if (!instance)
                    return;

                if (!instance->discord()->getProxy().canRelayUdp()) {
                    QMessageBox::warning(this,
                                         tr("Voice unavailable"),
                                         tr("This account uses an HTTP proxy, which cannot carry "
                                            "voice traffic.<br>Switch it to a SOCKS5 proxy to use voice."));
                    return;
                }

                if (isDM) {
                    qCInfo(LogVoice) << "Joining DM call" << node->name << node->id;
                    instance->discord()->sendVoiceStateUpdate(Snowflake::Invalid, node->id, false, false);
                } else {
                    ChannelNode *guildNode = ChannelTreeModel::findGuildNode(node);
                    if (!guildNode)
                        return;

                    qCInfo(LogVoice) << "Joining voice channel" << node->name << node->id
                                     << "in guild" << guildNode->id;
                    instance->discord()->sendVoiceStateUpdate(guildNode->id, node->id, false, false);
                }
            });

    connect(channelTree, &ChannelTreeView::disconnectVoiceRequested, this,
            [this](const QModelIndex &proxyIndex) {
                QModelIndex sourceIndex = channelFilterProxy->mapToSource(proxyIndex);
                auto *node = channelTreeModel->nodeFromIndex(sourceIndex);
                if (!node)
                    return;

                ChannelNode *accountNode = channelTreeModel->getAccountNodeFor(node);
                if (!accountNode)
                    return;

                ClientInstance *instance = session->client(accountNode->id);
                if (!instance || !instance->isInVoice())
                    return;

                qCInfo(LogVoice) << "Disconnecting from voice";
                instance->discord()->sendVoiceStateUpdate(instance->voiceGuildId(), Snowflake::Invalid, false, false);
            });
#endif

    layout->addWidget(mainSplitter);
    layout->setContentsMargins(0, 0, 4, 0);

    auto *root = new QWidget(this);
    shellPages = new QStackedLayout(root);
    shellPages->setContentsMargins(0, 0, 0, 0);
    shellPages->addWidget(central);

    loginView = new LoginView(session, accountsModel, root);
    shellPages->addWidget(loginView);
    connect(loginView, &LoginView::loggedIn, this, &MainWindow::onAccountLoggedIn);

    // User Settings is a page of the same window, like Discord: it slides over
    // the shell and closes with the X or Escape instead of opening a dialog.
    settingsWindow = new SettingsWindow(root);
    settingsWindow->setAccountsModel(accountsModel);
    shellPages->addWidget(settingsWindow);
    connect(settingsWindow, &SettingsWindow::closeRequested, this, &MainWindow::hideOverlayScreen);
    connect(settingsWindow, &SettingsWindow::addAccountRequested, this,
            &MainWindow::showAddAccountScreen);
    connect(settingsWindow, &SettingsWindow::logOutRequested, this, &MainWindow::logOutAccount);
    connect(settingsWindow, &SettingsWindow::channelListModeChanged, this,
            [this](bool classic) { setChannelListMode(classic ? ChannelListMode::Classic : ChannelListMode::Tree); });
    connect(settingsWindow, &SettingsWindow::channelListIndentChanged, this,
            &MainWindow::applyChannelIndent);
    connect(settingsWindow, &SettingsWindow::closeToTrayChanged, this, &MainWindow::applyCloseToTray);
    connect(settingsWindow, &SettingsWindow::animateEmojiChanged, this, [this](bool enabled) {
        chatView->frameAnimator()->setEmojiEnabled(enabled);
        if (expressionPicker)
            expressionPicker->setAnimationEnabled(enabled);
    });
    connect(settingsWindow, &SettingsWindow::animateStickersChanged, this, [this](bool enabled) {
        chatView->frameAnimator()->setStickersEnabled(enabled);
    });
    connect(settingsWindow, &SettingsWindow::gifSettingsChanged, this, [this]() {
        applyGifSettings(chatView->gifPlayback());
        if (expressionPicker)
            applyGifSettings(expressionPicker->gifPlayback());
    });
    connect(settingsWindow, &SettingsWindow::animationCacheLimitChanged,
            session->getAnimatedImageCache(), &Core::AnimatedImageCache::setCacheLimitMiB);
    connect(settingsWindow, &SettingsWindow::animationSizeLimitChanged,
            session->getAnimatedImageCache(), &Core::AnimatedImageCache::setAnimationLimitMiB);

    setCentralWidget(root);
}

QWidget *MainWindow::buildLeftSide()
{
    auto *container = new QWidget;
    container->setObjectName(QStringLiteral("GuildSidebar"));
    container->setAttribute(Qt::WA_StyledBackground, true);

    if (channelListMode == ChannelListMode::Classic) {
        auto *h = new QHBoxLayout(container);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(0);

        serverRail->show();
        h->addWidget(serverRail, 0);

        auto *secondary = new QWidget(container);
        auto *v = new QVBoxLayout(secondary);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);
        v->addWidget(guildHeaderLabel, 0);
        guildHeaderLabel->setVisible(!guildHeaderLabel->text().isEmpty());
        channelTree->show();
        v->addWidget(channelTree, 1);
        v->addWidget(userPanel, 0);
        h->addWidget(secondary, 1);
    } else {
        auto *v = new QVBoxLayout(container);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);

        // The tree layout has no rail home button, so the friends page needs its
        // own entry point at the top of the sidebar.
        auto *friendsEntry = new QPushButton(tr("Friends"), container);
        friendsEntry->setObjectName(QStringLiteral("SidebarFriendsEntry"));
        friendsEntry->setCursor(Qt::PointingHandCursor);
        connect(friendsEntry, &QPushButton::clicked, this, [this] {
            if (currentInstance)
                showFriendsPage(currentInstance->accountId());
        });
        v->addWidget(friendsEntry, 0);

        channelTree->show();
        v->addWidget(channelTree, 1);
        v->addWidget(userPanel, 0);
    }

    container->setMinimumWidth(200);
    return container;
}

void MainWindow::showLoginScreen()
{
    shellPages->setCurrentWidget(loginView);
    loginView->showQrPage();
}

void MainWindow::showAddAccountScreen()
{
    // Discord adds accounts in the window it already runs in, so the login
    // screen takes the shell over instead of opening a side dialog.
    shellPages->setCurrentWidget(loginView);
    loginView->showTokenPage();
}

void MainWindow::hideLoginScreen()
{
    hideOverlayScreen();
}

void MainWindow::hideOverlayScreen()
{
    shellPages->setCurrentIndex(0);
}

void MainWindow::showSettingsPage(const QString &page)
{
#ifndef ACHERON_NO_VOICE
    settingsWindow->setVoiceManager(currentInstance ? currentInstance->voice() : nullptr);
#endif

    shellPages->setCurrentWidget(settingsWindow);
    settingsWindow->openPage(page.isEmpty() ? tr("My Account") : page);
}

void MainWindow::logOutAccount(Core::Snowflake accountId)
{
    session->disconnectAccount(accountId);

    for (int row = 0; row < accountsModel->rowCount(); ++row) {
        QModelIndex idx = accountsModel->index(row, 0);
        auto *info = static_cast<Core::AccountInfo *>(
                idx.data(AccountsModel::AccountObjectRole).value<void *>());
        if (info && info->id == accountId) {
            accountsModel->removeAccount(row);
            return;
        }
    }
}

void MainWindow::onAccountLoggedIn(Snowflake accountId)
{
    hideLoginScreen();
    session->connectAccount(accountId);
}

void MainWindow::applyChannelIndent()
{
    channelDelegate->setIndentScopes(channelListMode == ChannelListMode::Tree ? ChannelIndent::activeScopes() : ChannelIndent::Scopes());
    channelTree->viewport()->update();
}

void MainWindow::setChannelListMode(ChannelListMode mode)
{
    if (channelListMode == mode)
        return;
    channelListMode = mode;
    QSettings().setValue("ui/channelListMode", mode == ChannelListMode::Classic ? "classic" : "tree");
    channelTree->setMentionJumpEnabled(mode == ChannelListMode::Tree);
    applyChannelIndent();

    QList<int> splitterSizes = mainSplitter ? mainSplitter->sizes() : QList<int>();

    channelTree->setParent(this);
    channelTree->hide();
    serverRail->setParent(this);
    serverRail->hide();
    guildHeaderLabel->setParent(this);
    guildHeaderLabel->hide();
    userPanel->setParent(this);
    userPanel->hide();

    QWidget *newLeft = buildLeftSide();
    QWidget *old = mainSplitter->replaceWidget(0, newLeft);
    leftSideWidget = newLeft;
    if (old)
        old->deleteLater();

    mainSplitter->setCollapsible(LeftPaneIndex, true);
    mainSplitter->setStretchFactor(0, 0);
    if (splitterSizes.size() == mainSplitter->count()) {
        const int delta = (mode == ChannelListMode::Classic) ? ServerRailDelegate::RailWidth : -ServerRailDelegate::RailWidth;
        if (channelListHidden) {
            if (channelListRestoreWidth > 0)
                channelListRestoreWidth = qMax(0, channelListRestoreWidth + delta);
        } else {
            splitterSizes[0] = qMax(0, splitterSizes[0] + delta);
            if (splitterSizes.size() > 1)
                splitterSizes[1] = qMax(0, splitterSizes[1] - delta);
        }
        mainSplitter->setSizes(splitterSizes);
    }

    userPanel->show();

    if (mode == ChannelListMode::Tree) {
        channelTree->setRootIndex({});
        channelTree->performDefaultExpansion();
        applyTreeState();
    } else {
        serverRailModel->rebuild();
        railHasSelection = false;
        selectInitialRailItem();
    }
}

void MainWindow::onRailGuildSelected(Snowflake accountId, Snowflake guildId)
{
    QModelIndex src = channelTreeModel->serverIndex(accountId, guildId);
    if (!src.isValid())
        return;
    QModelIndex proxy = channelFilterProxy->mapFromSource(src);
    if (!proxy.isValid())
        return;

    channelTree->setRootIndex(proxy);
    channelTree->performDefaultExpansion();

    serverRailModel->setSelected(ServerRailModel::Kind::Server, accountId, guildId);
    QString guildName = src.data(Qt::DisplayRole).toString();
    guildHeaderLabel->setText(guildName);
    guildHeaderLabel->setVisible(!guildName.isEmpty());
    railHasSelection = true;
    railSelectedIsHome = false;
    railSelectedAccountId = accountId;
    railSelectedGuildId = guildId;

    QSettings s;
    s.setValue("ui/rail/lastAccountId", static_cast<qulonglong>(accountId));
    s.setValue("ui/rail/lastGuildId", static_cast<qulonglong>(guildId));
    s.setValue("ui/rail/lastIsHome", false);
}

void MainWindow::onRailAccountHomeSelected(Snowflake accountId)
{
    QModelIndex src = channelTreeModel->dmHeaderIndex(accountId);
    if (!src.isValid())
        return;
    QModelIndex proxy = channelFilterProxy->mapFromSource(src);
    if (!proxy.isValid())
        return;

    channelTree->setRootIndex(proxy);

    serverRailModel->setSelected(ServerRailModel::Kind::AccountHome, accountId, accountId);
    guildHeaderLabel->setText(tr("Direct Messages"));
    guildHeaderLabel->show();
    railHasSelection = true;
    railSelectedIsHome = true;
    railSelectedAccountId = accountId;
    railSelectedGuildId = Snowflake();

    QSettings s;
    s.setValue("ui/rail/lastAccountId", static_cast<qulonglong>(accountId));
    s.setValue("ui/rail/lastIsHome", true);
}

void MainWindow::showFriendsPage(Snowflake accountId)
{
    // Home is Discord's friends page. The rail layout re-roots the channel pane at
    // the account's DM header; the tree layout keeps the full account list visible.
    if (channelListMode == ChannelListMode::Classic)
        onRailAccountHomeSelected(accountId);
    else
        channelTree->setRootIndex({});

    ClientInstance *instance = session->client(accountId);
    if (!instance)
        return;

    if (instance != currentInstance)
        switchActiveInstance(instance);

    friendsView->setInstance(instance);
    setViewMode(ViewMode::Friends);
}

void MainWindow::onRailAccountHomeClicked(Snowflake accountId)
{
    showFriendsPage(accountId);
}

void MainWindow::selectInitialRailItem()
{
    if (currentInstance) {
        Snowflake channelId = chatModel->getActiveChannelId();
        if (channelId.isValid()) {
            Snowflake accountId = currentInstance->accountId();
            if (ChannelNode *node = channelTreeModel->findChannelTreeNode(channelId, accountId)) {
                if (node->type == ChannelNode::Type::DMChannel) {
                    onRailAccountHomeSelected(accountId);
                    return;
                }
                ChannelNode *guild = ChannelTreeModel::findGuildNode(node);
                if (guild) {
                    onRailGuildSelected(accountId, guild->id);
                    return;
                }
            }
        }
    }

    if (hasSavedRailSelection) {
        if (!savedRailIsHome &&
            channelTreeModel->serverIndex(savedRailAccountId, savedRailGuildId).isValid()) {
            onRailGuildSelected(savedRailAccountId, savedRailGuildId);
            return;
        }
        if (channelTreeModel->dmHeaderIndex(savedRailAccountId).isValid()) {
            showFriendsPage(savedRailAccountId);
            return;
        }
    }

    for (int i = 0; i < channelTreeModel->rowCount({}); ++i) {
        QModelIndex accIdx = channelTreeModel->index(i, 0, {});
        if (static_cast<ChannelNode::Type>(accIdx.data(ChannelTreeModel::TypeRole).toInt()) == ChannelNode::Type::Account) {
            showFriendsPage(Snowflake(accIdx.data(ChannelTreeModel::IdRole).toULongLong()));
            return;
        }
    }

    guildHeaderLabel->clear();
    guildHeaderLabel->hide();
}

void MainWindow::applyPendingRailSelection(Snowflake accountId)
{
    if (channelListMode != ChannelListMode::Classic)
        return;

    if (railHasSelection) {
        hasSavedRailSelection = false;
        return;
    }

    if (hasSavedRailSelection) {
        if (savedRailAccountId != accountId)
            return;
        hasSavedRailSelection = false;
        if (!savedRailIsHome &&
            channelTreeModel->serverIndex(accountId, savedRailGuildId).isValid())
            onRailGuildSelected(accountId, savedRailGuildId);
        else
            showFriendsPage(accountId);
        return;
    }

    showFriendsPage(accountId);
}

void MainWindow::onRailGuildClicked(Snowflake accountId, Snowflake guildId)
{
    onRailGuildSelected(accountId, guildId);

    Snowflake channelId = resolveRailChannel(accountId, guildId);
    if (!channelId.isValid())
        return;

    ChannelNode *node = channelTreeModel->findChannelTreeNode(channelId, accountId);
    if (!node)
        return;

    channelFilterProxy->setSelectedChannel(channelId, accountId);
    QModelIndex proxy = channelFilterProxy->mapFromSource(channelTreeModel->indexForNode(node));
    if (proxy.isValid())
        channelTree->setCurrentIndex(proxy);
}

Core::Snowflake MainWindow::resolveRailChannel(Snowflake accountId, Snowflake guildId)
{
    auto it = lastViewedChannel.constFind(guildChannelKey(accountId, guildId));
    if (it != lastViewedChannel.cend() && channelReadable(accountId, guildId, it.value()))
        return it.value();

    QModelIndex guildSrc = channelTreeModel->serverIndex(accountId, guildId);
    ChannelNode *guildNode = guildSrc.isValid() ? channelTreeModel->nodeFromIndex(guildSrc) : nullptr;
    if (guildNode && channelReadable(accountId, guildId, guildNode->rulesChannelId))
        return guildNode->rulesChannelId;

    return firstReadableChannel(accountId, guildId);
}

bool MainWindow::channelReadable(Snowflake accountId, Snowflake guildId, Snowflake channelId)
{
    if (!channelId.isValid())
        return false;
    ChannelNode *node = channelTreeModel->findChannelTreeNode(channelId, accountId);
    if (!node || (node->type != ChannelNode::Type::Channel && node->type != ChannelNode::Type::VoiceChannel))
        return false;
    ChannelNode *guild = ChannelTreeModel::findGuildNode(node);
    if (!guild || guild->id != guildId)
        return false;
    ClientInstance *instance = session->client(accountId);
    if (instance && instance->permissions())
        return instance->permissions()->hasChannelPermission(accountId, channelId, Discord::Permission::VIEW_CHANNEL);
    return true;
}

Core::Snowflake MainWindow::firstReadableChannel(Snowflake accountId, Snowflake guildId)
{
    QModelIndex guildSrc = channelTreeModel->serverIndex(accountId, guildId);
    if (!guildSrc.isValid())
        return {};
    ChannelNode *guildNode = channelTreeModel->nodeFromIndex(guildSrc);
    if (!guildNode)
        return {};

    ClientInstance *instance = session->client(accountId);
    auto *perms = instance ? instance->permissions() : nullptr;
    auto readable = [&](Snowflake ch) {
        return !perms || perms->hasChannelPermission(accountId, ch, Discord::Permission::VIEW_CHANNEL);
    };

    for (const auto &child : guildNode->children) {
        if (child->type == ChannelNode::Type::Category)
            break;
        if (child->type == ChannelNode::Type::Channel && readable(child->id))
            return child->id;
    }

    for (const auto &child : guildNode->children) {
        if (child->type != ChannelNode::Type::Category)
            continue;
        for (const auto &cc : child->children)
            if (cc->type == ChannelNode::Type::Channel && readable(cc->id))
                return cc->id;
    }
    return {};
}

void MainWindow::markIndexAsRead(Snowflake accountId, const QModelIndex &sourceIndex)
{
    ClientInstance *instance = session->client(accountId);
    if (!instance || !sourceIndex.isValid())
        return;

    ChannelNode *node = channelTreeModel->nodeFromIndex(sourceIndex);
    if (!node)
        return;

    switch (node->type) {
    case ChannelNode::Type::Server:
        instance->markGuildsAsRead({ node->id });
        break;
    case ChannelNode::Type::Folder: {
        QList<Snowflake> guildIds;
        for (const auto &child : node->children)
            if (child->type == ChannelNode::Type::Server)
                guildIds.append(child->id);
        instance->markGuildsAsRead(guildIds);
        break;
    }
    case ChannelNode::Type::Category:
        if (ChannelNode *guildNode = ChannelTreeModel::findGuildNode(node))
            instance->markCategoryAsRead(guildNode->id, node->id);
        break;
    default:
        instance->readState()->markChannelsAsRead(channelTreeModel->getMarkableChannelIds(sourceIndex));
        break;
    }
}

void MainWindow::recordLastViewedChannel(Snowflake accountId, Snowflake guildId, Snowflake channelId)
{
    lastViewedChannel.insert(guildChannelKey(accountId, guildId), channelId);
}

void MainWindow::refreshUserPanelIdentity()
{
    if (!userPanel)
        return;

    if (!currentInstance) {
        userPanel->setSelf(QString(), QString(), QUrl(), Core::Snowflake(), Core::PresenceBadge());
        return;
    }

    const Core::Snowflake selfId = currentInstance->accountId();
    const Core::AccountInfo &info = currentInstance->accountInfo();

    QString name = info.displayName.isEmpty() ? info.username : info.displayName;
    QUrl avatar;

    const std::optional<Discord::User> self = currentInstance->users()->getUser(selfId);
    if (self) {
        const QString global = self->getDisplayName();
        if (!global.isEmpty())
            name = global;
        avatar = currentInstance->users()->getAvatarUrl(*self, Core::Snowflake::Invalid, 64);
    }

    const Core::PresenceBadge badge = currentInstance->presences()->badge(selfId);

    QString status;
    switch (badge.status) {
    case Discord::StatusType::ONLINE:
        status = tr("Online");
        break;
    case Discord::StatusType::IDLE:
        status = tr("Idle");
        break;
    case Discord::StatusType::DND:
        status = tr("Do Not Disturb");
        break;
    case Discord::StatusType::INVISIBLE:
        status = tr("Invisible");
        break;
    default:
        status = currentInstance->state() == Core::ConnectionState::Connecting
                         ? tr("Connecting...")
                         : tr("Offline");
        break;
    }

    userPanel->setSelf(name, status, avatar, selfId, badge);
}

namespace {

// "12", "2,5", "45x", "100%" all mean the same thing to a person typing fast.
double parseGainArgument(const QString &input, bool *ok)
{
    *ok = false;

    QString text = input.trimmed().toLower();
    text.remove(QLatin1Char(' '));
    if (text.isEmpty())
        return 0.0;

    const bool percent = text.endsWith(QLatin1Char('%'));
    if (percent || text.endsWith(QLatin1Char('x')) || text.endsWith(QChar(0x00D7)))
        text.chop(1);

    text.replace(QLatin1Char(','), QLatin1Char('.'));

    bool numeric = false;
    const double value = text.toDouble(&numeric);
    if (!numeric)
        return 0.0;

    *ok = true;
    return percent ? value / 100.0 : value;
}

// A bare number up to 2000 is how people quote kilobits; anything above is bits.
double parseBitrateArgument(const QString &input, bool *ok)
{
    *ok = false;

    QString text = input.trimmed().toLower();
    text.remove(QLatin1Char(' '));
    if (text.endsWith(QStringLiteral("bps")))
        text.chop(3);
    else if (text.endsWith(QLatin1Char('b')))
        text.chop(1);
    if (text.isEmpty())
        return 0.0;

    double scale = 1.0;
    if (text.endsWith(QLatin1Char('k'))) {
        scale = 1000.0;
        text.chop(1);
    } else if (text.endsWith(QLatin1Char('m'))) {
        scale = 1000000.0;
        text.chop(1);
    }

    text.replace(QLatin1Char(','), QLatin1Char('.'));

    bool numeric = false;
    const double value = text.toDouble(&numeric);
    if (!numeric || value <= 0.0)
        return 0.0;

    *ok = true;
    if (scale == 1.0 && value <= 2000.0)
        return value * 1000.0;
    return value * scale;
}

} // namespace

void MainWindow::openGainConsole()
{
    const double current = Core::Audio::AudioSettings::inputGain();

    commandBar->open(tr("INPUT GAIN"),
                     tr("0 – 100 ×  ·  F3 sets the bitrate"),
                     QStringLiteral("100"),
                     QString::number(current, 'f', 2),
                     [this](const QString &argument) {
                         bool ok = false;
                         const double value = parseGainArgument(argument, &ok);
                         if (!ok || value < 0.0)
                             return CommandBar::Outcome { false, tr("Type a number from 0 to 100.") };

                         const double gain = Core::Audio::AudioSettings::clampGain(value);
                         Core::Audio::AudioSettings::setInputGain(gain);
                         applyAudioSettingsLive();

                         QString message = tr("input gain  %1 ×").arg(gain, 0, 'f', 2);
                         if (gain != value)
                             message += tr("   (100 is the ceiling)");
                         return CommandBar::Outcome { true, message };
                     });
}

void MainWindow::openBitrateConsole()
{
    const int current = Core::Audio::AudioSettings::bitrate();

    commandBar->open(tr("BITRATE"),
                     tr("8000 – 1000000 bps  ·  96k and 1m work too"),
                     QStringLiteral("1000000"),
                     QString::number(current),
                     [this](const QString &argument) {
                         bool ok = false;
                         const double value = parseBitrateArgument(argument, &ok);
                         if (!ok)
                             return CommandBar::Outcome {
                                 false, tr("Try 96k, 256000 or 1m.")
                             };

                         const double capped =
                                 qBound(static_cast<double>(Core::Audio::AudioSettings::MinBitrate),
                                        value,
                                        static_cast<double>(Core::Audio::AudioSettings::MaxBitrate));
                         const int bitrate = static_cast<int>(capped);
                         Core::Audio::AudioSettings::setBitrate(bitrate);
                         applyAudioSettingsLive();

                         QString message = tr("bitrate  %1 bps").arg(bitrate);
                         if (capped != value)
                             message += tr("   (Opus tops out at 1000000)");
                         return CommandBar::Outcome { true, message };
                     });
}

void MainWindow::applyAudioSettingsLive()
{
#ifndef ACHERON_NO_VOICE
    for (const auto &instance : session->getClients()) {
        if (instance)
            Core::Audio::AudioSettings::applyTo(instance->voice());
    }
#endif
}

#ifndef ACHERON_NO_VOICE
void MainWindow::updateVoicePanel()
{
    using VState = Discord::Voice::VoiceClient::State;

    if (!userPanel)
        return;

    // Find any account that is in voice or has an active voice client
    Core::ClientInstance *voiceInstance = nullptr;
    for (const auto &inst : session->getClients()) {
        if (inst && (inst->isInVoice() || inst->voice()->clientState() != VState::Disconnected)) {
            voiceInstance = inst;
            break;
        }
    }

    userPanel->setVoiceManager(voiceInstance ? voiceInstance->voice() : nullptr);
    QString label;
    bool isStage = false;

    if (voiceInstance) {
        const Core::Snowflake vcId = voiceInstance->voiceChannelId();
        QString channelName;
        if (vcId.isValid()) {
            ChannelNode *node = channelTreeModel->findChannelTreeNode(vcId);
            if (node)
                channelName = node->type == ChannelNode::Type::DMChannel ? node->name
                                                                        : ("#" + node->name);
            else
                channelName = QString::number(vcId);

            const auto channel = voiceInstance->getChannel(vcId);
            isStage = channel && channel->type.hasValue() &&
                      channel->type.get() == Discord::ChannelType::GUILD_STAGE_VOICE;
        }

        QString guildName;
        const auto guild = voiceInstance->getGuild(voiceInstance->voiceGuildId());
        if (guild)
            guildName = guild->name.get();

        if (!channelName.isEmpty() && !guildName.isEmpty())
            label = channelName + QStringLiteral(" / ") + guildName;
        else
            label = channelName;
    }

    userPanel->setChannelLabel(label);
    userPanel->setStageMode(isStage);
    userPanel->updateConnectionState();
}
#endif

void MainWindow::switchToTabEntry(const TabEntry &entry)
{
    if (!entry.channelId.isValid())
        return;

    activateChannel(entry);
}

void MainWindow::activateChannel(const TabEntry &entry)
{
    channelTreeModel->clearTemporaryThread(entry.channelId);

    // update the proxy selected channel so the delegate highlights correctly,
    // and clear the trees own selection so no stale highlight remains
    channelFilterProxy->setSelectedChannel(entry.channelId, entry.accountId);
    {
        QSignalBlocker blocker(channelTree->selectionModel());
        channelTree->selectionModel()->clearSelection();
        channelTree->selectionModel()->clearCurrentIndex();
    }
    channelTree->viewport()->update();

    if (channelListMode == ChannelListMode::Classic) {
        if (entry.isDm) {
            if (!(railSelectedIsHome && railSelectedAccountId == entry.accountId))
                onRailAccountHomeSelected(entry.accountId);
        } else if (entry.guildId.isValid()) {
            if (railSelectedIsHome || railSelectedAccountId != entry.accountId ||
                railSelectedGuildId != entry.guildId)
                onRailGuildSelected(entry.accountId, entry.guildId);
        }
    }

    ClientInstance *instance = session->client(entry.accountId);
    if (!instance) {
        messageInput->setEnabled(false);
        return;
    }

    if (instance != currentInstance)
        switchActiveInstance(instance);

    if (entry.isForum) {
        setThreadBrowserTarget(Snowflake::Invalid);
        openForumChannel(instance, entry.channelId, entry.guildId);
        if (entry.guildId.isValid())
            recordLastViewedChannel(entry.accountId, entry.guildId, entry.channelId);
        refreshTabReadStates();
        return;
    }

    instance->readState()->setActiveChannel(entry.channelId);
    setViewMode(ViewMode::TextChannel);

    if (!entry.isDm && entry.guildId.isValid())
        recordLastViewedChannel(entry.accountId, entry.guildId, entry.channelId);
    else if (entry.isDm)
        recordLastViewedChannel(entry.accountId, Snowflake::Invalid, entry.channelId);

    applyChannelChrome(instance, entry.channelId, entry.name, entry.isDm, entry.guildId);
    switchChatChannel(entry.channelId, entry.guildId);
    instance->messages()->requestLoadChannel(entry.channelId);

    refreshTabReadStates();
}

void MainWindow::refreshTabReadStates()
{
    for (int i = 0; i < tabBar->tabCount(); ++i) {
        const TabEntry &entry = tabBar->tabEntry(i);
        if (!entry.channelId.isValid())
            continue;

        ClientInstance *inst = session->client(entry.accountId);
        if (!inst)
            continue;

        auto *readState = inst->readState();
        auto state = entry.isDm ? readState->computeDMReadState(entry.channelId)
                                : readState->computeChannelReadState(entry.channelId, entry.guildId,
                                                                     Snowflake::Invalid);
        if (entry.isForum) {
            auto posts = inst->forums()->joinedPostsContribution(entry.channelId);
            state.isUnread = state.isUnread || posts.unread;
            state.mentionCount += posts.mentions;
        }
        tabBar->updateChannelReadState(entry.channelId, state.isUnread, state.mentionCount);
    }
}

void MainWindow::onTotalMentionCountChanged(int count, int previousCount)
{
    const QString appName = QGuiApplication::applicationDisplayName();
    setWindowTitle(count > 0 ? QStringLiteral("(%1) %2").arg(count).arg(appName) : appName);

    if (previousCount == 0 && count > 0)
        QApplication::alert(this);
}

namespace {

bool isNativeExpandable(ChannelNode::Type type)
{
    return type == ChannelNode::Type::Account ||
           type == ChannelNode::Type::DMHeader ||
           type == ChannelNode::Type::Folder ||
           type == ChannelNode::Type::Server;
}

// <accountId>:<type>:<nodeId>
QStringList mergeTreeKeys(const QStringList &previous, const QStringList &captured,
                          const QSet<QString> &presentAccounts)
{
    QStringList result;
    for (const QString &key : previous) {
        if (!presentAccounts.contains(key.section(':', 0, 0)))
            result.append(key);
    }
    result.append(captured);
    return result;
}

} // namespace

void MainWindow::forEachSourceNode(const std::function<void(const QModelIndex &, ChannelNode *)> &fn) const
{
    std::function<void(const QModelIndex &)> walk = [&](const QModelIndex &parent) {
        int rows = channelTreeModel->rowCount(parent);
        for (int i = 0; i < rows; ++i) {
            QModelIndex idx = channelTreeModel->index(i, 0, parent);
            if (ChannelNode *node = channelTreeModel->nodeFromIndex(idx))
                fn(idx, node);
            walk(idx);
        }
    };
    walk({});
}

QString MainWindow::treeNodeKey(const ChannelNode *node) const
{
    const ChannelNode *acct = node;
    while (acct && acct->type != ChannelNode::Type::Account)
        acct = acct->parent;

    quint64 acctId = acct ? static_cast<quint64>(acct->id) : 0;
    return QStringLiteral("%1:%2:%3")
            .arg(acctId)
            .arg(static_cast<int>(node->type))
            .arg(static_cast<quint64>(node->id));
}

void MainWindow::captureTreeState(QStringList &expanded,
                                  QStringList &collapsed,
                                  QStringList &collapsedCategories,
                                  QSet<QString> &presentAccounts) const
{
    forEachSourceNode([&](const QModelIndex &sourceIndex, ChannelNode *node) {
        if (node->type == ChannelNode::Type::Account)
            presentAccounts.insert(QString::number(static_cast<quint64>(node->id)));

        if (node->type == ChannelNode::Type::Category) {
            if (node->collapsed)
                collapsedCategories.append(treeNodeKey(node));
            return;
        }

        if (!isNativeExpandable(node->type))
            return;

        QModelIndex proxyIndex = channelFilterProxy->mapFromSource(sourceIndex);
        if (!proxyIndex.isValid())
            return;

        if (channelTree->isExpanded(proxyIndex))
            expanded.append(treeNodeKey(node));
        else
            collapsed.append(treeNodeKey(node));
    });
}

void MainWindow::applyTreeState()
{
    if (!hasSavedTreeState)
        return;

    // first restore the pseudo-collapsed stuff for categories
    forEachSourceNode([this](const QModelIndex &sourceIndex, ChannelNode *node) {
        if (node->type == ChannelNode::Type::Category)
            channelTreeModel->setCollapsed(sourceIndex, savedCollapsedCategories.contains(treeNodeKey(node)));
    });
    channelFilterProxy->invalidateFilter();

    // the rest
    forEachSourceNode([this](const QModelIndex &sourceIndex, ChannelNode *node) {
        if (!isNativeExpandable(node->type))
            return;

        QString key = treeNodeKey(node);
        QModelIndex proxyIndex = channelFilterProxy->mapFromSource(sourceIndex);
        if (!proxyIndex.isValid())
            return;

        if (savedExpandedNodes.contains(key))
            channelTree->expand(proxyIndex);
        else if (savedCollapsedNodes.contains(key))
            channelTree->collapse(proxyIndex);
    });
}

void MainWindow::maybeActivatePendingChannel(Core::Snowflake accountId)
{
    if (!pendingActiveEntry.has_value() || pendingActiveEntry->accountId != accountId)
        return;

    TabEntry entry = *pendingActiveEntry;
    pendingActiveEntry.reset();
    activateChannel(entry);
}

void MainWindow::saveWindowState()
{
    QSettings settings;

    settings.setValue("layout/geometry", saveGeometry());
    if (mainSplitter)
        settings.setValue("layout/splitter", mainSplitter->saveState());
    settings.setValue("layout/channelListHidden", channelListHidden);
    settings.setValue("layout/memberListHidden", memberListHidden);
    settings.setValue("layout/channelListWidth", channelListRestoreWidth);
    settings.setValue("layout/memberListWidth", memberListRestoreWidth);

    const QList<TabEntry> all = tabBar->tabEntries();
    int activeIndex = tabBar->activeTabIndex();
    QList<TabEntry> valid;
    int newActive = 0;
    for (int i = 0; i < all.size(); ++i) {
        if (!all[i].channelId.isValid())
            continue;
        if (i == activeIndex)
            newActive = valid.size();
        valid.append(all[i]);
    }

    settings.remove("layout/tabs");
    settings.beginWriteArray("layout/tabs");
    for (int i = 0; i < valid.size(); ++i) {
        settings.setArrayIndex(i);
        const TabEntry &entry = valid[i];
        settings.setValue("channelId", static_cast<quint64>(entry.channelId));
        settings.setValue("guildId", static_cast<quint64>(entry.guildId));
        settings.setValue("accountId", static_cast<quint64>(entry.accountId));
        settings.setValue("name", entry.name);
        settings.setValue("iconUrl", entry.iconUrl);
        settings.setValue("isDm", entry.isDm);
        settings.setValue("isForum", entry.isForum);
    }
    settings.endArray();
    settings.setValue("layout/activeTab", newActive);

    QStringList expanded, collapsed, collapsedCategories;
    QSet<QString> presentAccounts;
    captureTreeState(expanded, collapsed, collapsedCategories, presentAccounts);

    if (channelListMode == ChannelListMode::Tree) {
        settings.setValue("layout/tree/expanded",
                          mergeTreeKeys(settings.value("layout/tree/expanded").toStringList(),
                                        expanded,
                                        presentAccounts));
        settings.setValue("layout/tree/collapsed",
                          mergeTreeKeys(settings.value("layout/tree/collapsed").toStringList(),
                                        collapsed,
                                        presentAccounts));
    }
    settings.setValue("layout/tree/collapsedCategories",
                      mergeTreeKeys(settings.value("layout/tree/collapsedCategories").toStringList(),
                                    collapsedCategories,
                                    presentAccounts));

    settings.setValue("ui/channelListMode", channelListMode == ChannelListMode::Classic ? "classic" : "tree");
    if (serverRailModel)
        settings.setValue("ui/rail/expandedFolders", serverRailModel->expandedFolderKeys());

    settings.remove("ui/rail/lastChannels");
    settings.beginWriteArray("ui/rail/lastChannels");
    int lastChannelIdx = 0;
    for (auto it = lastViewedChannel.cbegin(); it != lastViewedChannel.cend(); ++it) {
        settings.setArrayIndex(lastChannelIdx++);
        settings.setValue("key", it.key());
        settings.setValue("channel", static_cast<qulonglong>(it.value()));
    }
    settings.endArray();
}

void MainWindow::restoreWindowState()
{
    QSettings settings;

    if (settings.contains("layout/geometry"))
        restoreGeometry(settings.value("layout/geometry").toByteArray());
    if (mainSplitter && settings.contains("layout/splitter"))
        mainSplitter->restoreState(settings.value("layout/splitter").toByteArray());

    channelListHidden = settings.value("layout/channelListHidden", false).toBool();
    memberListHidden = settings.value("layout/memberListHidden", false).toBool();
    channelListRestoreWidth = settings.value("layout/channelListWidth", 0).toInt();
    memberListRestoreWidth = settings.value("layout/memberListWidth", 0).toInt();

    if (channelListHidden)
        applyPaneCollapsed(LeftPaneIndex, true, channelListRestoreWidth, DefaultChannelListWidth);
    syncPaneToggleActions();

    const auto toSet = [](const QStringList &list) {
        return QSet<QString>(list.cbegin(), list.cend());
    };
    savedExpandedNodes = toSet(settings.value("layout/tree/expanded").toStringList());
    savedCollapsedNodes = toSet(settings.value("layout/tree/collapsed").toStringList());
    savedCollapsedCategories = toSet(settings.value("layout/tree/collapsedCategories").toStringList());
    hasSavedTreeState = !savedExpandedNodes.isEmpty() ||
                        !savedCollapsedNodes.isEmpty() ||
                        !savedCollapsedCategories.isEmpty();

    savedRailAccountId = Core::Snowflake(settings.value("ui/rail/lastAccountId").toULongLong());
    savedRailGuildId = Core::Snowflake(settings.value("ui/rail/lastGuildId").toULongLong());
    savedRailIsHome = settings.value("ui/rail/lastIsHome", true).toBool();
    hasSavedRailSelection = settings.contains("ui/rail/lastAccountId");
    if (serverRailModel)
        serverRailModel->setExpandedFolderKeys(settings.value("ui/rail/expandedFolders").toStringList());

    int lastChannelCount = settings.beginReadArray("ui/rail/lastChannels");
    for (int i = 0; i < lastChannelCount; ++i) {
        settings.setArrayIndex(i);
        QString key = settings.value("key").toString();
        if (!key.isEmpty())
            lastViewedChannel.insert(key, Core::Snowflake(settings.value("channel").toULongLong()));
    }
    settings.endArray();

    QList<TabEntry> entries;
    int count = settings.beginReadArray("layout/tabs");
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        TabEntry entry;
        entry.channelId = Core::Snowflake(settings.value("channelId").toULongLong());
        entry.guildId = Core::Snowflake(settings.value("guildId").toULongLong());
        entry.accountId = Core::Snowflake(settings.value("accountId").toULongLong());
        entry.name = settings.value("name").toString();
        entry.iconUrl = settings.value("iconUrl").toUrl();
        entry.isDm = settings.value("isDm").toBool();
        entry.isForum = settings.value("isForum").toBool();
        if (entry.channelId.isValid())
            entries.append(entry);
    }
    settings.endArray();

    if (!entries.isEmpty()) {
        int activeTab = qBound(0, settings.value("layout/activeTab", 0).toInt(), entries.size() - 1);
        tabBar->restoreTabs(entries, activeTab);
        // wait for READY
        pendingActiveEntry = entries[activeTab];
    }
}

void MainWindow::setupMenu()
{
    // Discord has no menu bar at all: the commands people used to find there live
    // in the settings overlay, the bottom-left user panel and the keyboard.
    menuBar()->setNativeMenuBar(false);
    menuBar()->hide();

    channelListToggle = new QAction(tr("Toggle Channel List"), this);
    channelListToggle->setCheckable(true);
    channelListToggle->setChecked(!channelListHidden);
    channelListToggle->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    connect(channelListToggle, &QAction::toggled, this, [this](bool checked) { setChannelListHidden(!checked); });
    addAction(channelListToggle);

    memberListToggle = new QAction(tr("Toggle Member List"), this);
    memberListToggle->setCheckable(true);
    memberListToggle->setChecked(!memberListHidden);
    memberListToggle->setEnabled(false);
    memberListToggle->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_U));
    connect(memberListToggle, &QAction::toggled, this, [this](bool checked) { setMemberListHidden(!checked); });
    addAction(memberListToggle);

    auto *settingsShortcut = new QAction(tr("User Settings"), this);
    settingsShortcut->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma));
    connect(settingsShortcut, &QAction::triggered, this, &MainWindow::openSettingsWindow);
    addAction(settingsShortcut);

    commandBar = new CommandBar(this);

    // F2 and F3 are grabbed application-wide: the typed value has to land even
    // while the caret sits in the message box.
    auto *gainConsole = new QAction(tr("Set Input Gain"), this);
    gainConsole->setShortcut(QKeySequence(Qt::Key_F2));
    gainConsole->setShortcutContext(Qt::ApplicationShortcut);
    connect(gainConsole, &QAction::triggered, this, &MainWindow::openGainConsole);
    addAction(gainConsole);

    auto *bitrateConsole = new QAction(tr("Set Bitrate"), this);
    bitrateConsole->setShortcut(QKeySequence(Qt::Key_F3));
    bitrateConsole->setShortcutContext(Qt::ApplicationShortcut);
    connect(bitrateConsole, &QAction::triggered, this, &MainWindow::openBitrateConsole);
    addAction(bitrateConsole);

    auto *emojiPickerShortcut = new QAction(this);
    emojiPickerShortcut->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    addAction(emojiPickerShortcut);
    connect(emojiPickerShortcut, &QAction::triggered, messageInput, &MessageInput::requestEmojiPicker);

    auto *gifPickerShortcut = new QAction(this);
    gifPickerShortcut->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_G));
    addAction(gifPickerShortcut);
    connect(gifPickerShortcut, &QAction::triggered, messageInput, &MessageInput::requestGifPicker);

    // DEBUG: Ctrl+Shift+R to force a Gateway reconnect
    auto *debugReconnect = new QAction(this);
    debugReconnect->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R));
    addAction(debugReconnect);
    connect(debugReconnect, &QAction::triggered, this, [this]() {
        if (currentInstance)
            currentInstance->discord()->debugForceReconnect();
    });
}

void MainWindow::openSettingsWindow()
{
    showSettingsPage(QString());
}

void MainWindow::applyCloseToTray()
{
    const bool wanted = QSettings().value("general/close_to_tray", false).toBool();
    if (wanted == (trayIcon != nullptr))
        return;

    if (!wanted) {
        if (isHidden())
            showFromTray();
        delete trayIcon;
        trayIcon = nullptr;
        return;
    }

    trayIcon = new TrayIcon(this);
    trayIcon->setToolTip(windowTitle());
    connect(this, &QWidget::windowTitleChanged, trayIcon, &TrayIcon::setToolTip);
    connect(trayIcon, &TrayIcon::showWindowRequested, this, &MainWindow::showFromTray);
    connect(trayIcon, &TrayIcon::quitRequested, this, &MainWindow::quitFromTray);
}

void MainWindow::showFromTray()
{
    setWindowState(windowState() & ~Qt::WindowMinimized);
    show();
    raise();
    activateWindow();
}

void MainWindow::quitFromTray()
{
    saveWindowState();
    qApp->quit();
}

void MainWindow::onTypingStart(const Discord::TypingStart &event)
{
    // todo im not really sure if i like having this handled here but its not that complex so oh well maybe later
    if (!currentInstance)
        return;

    if (event.member.hasValue() && event.guildId.hasValue()) {
        currentInstance->users()->saveMemberWithUser(event.guildId.get(), event.member.get());
    }

    std::optional<Snowflake> guildId =
            event.guildId.hasValue() ? std::optional(event.guildId.get()) : std::nullopt;

    typingTracker->addTyper(event.channelId.get(), event.userId.get(), guildId);
}

void MainWindow::onChannelPermissionsChanged(Core::Snowflake channelId)
{
    if (chatModel->getActiveChannelId() != channelId)
        return;

    if (!currentInstance)
        return;

    Core::Snowflake userId = currentInstance->accountId();
    bool voiceLocked = voiceChatLocked(currentInstance, channelId);
    bool canSend = !voiceLocked && currentInstance->permissions()->hasChannelPermission(userId, channelId, Discord::Permission::SEND_MESSAGES);
    bool canPin = currentInstance->permissions()->hasChannelPermission(
            userId, channelId, Discord::Permission::PIN_MESSAGES);
    bool canManage = currentInstance->permissions()->hasChannelPermission(
            userId, channelId, Discord::Permission::MANAGE_MESSAGES);

    int rateLimit = currentInstance->getChannelRateLimit(channelId);
    bool canBypass = currentInstance->permissions()->hasChannelPermission(
            userId, channelId, Discord::Permission::BYPASS_SLOWMODE);
    slowModeIndicator->setSlowMode(channelId, rateLimit, canBypass);

    bool onCooldown = rateLimit > 0 && !canBypass && slowModeIndicator->isOnCooldown(channelId);
    messageInput->setEnabled(canSend);
    messageInput->setSendBlocked(onCooldown);
    chatView->setCanPinMessages(canPin);
    chatView->setCanManageMessages(canManage);
    chatView->setCanAddReactions(canAddReactionsIn(currentInstance, channelId, chatModel->getActiveGuildId()));

    if (voiceLocked) {
        messageInput->setPlaceholder("You do not have permission to connect to this channel");
    } else if (!canSend) {
        messageInput->setPlaceholder("You do not have permission to send messages");
    } else if (onCooldown) {
        messageInput->setPlaceholder("Slowmode is active");
    } else {
        QString channelName;
        QModelIndex current = channelTree->currentIndex();
        if (current.isValid()) {
            auto *node = channelTreeModel->nodeFromIndex(channelFilterProxy->mapToSource(current));
            if (node)
                channelName = node->name;
        }
        if (channelName.isEmpty())
            channelName = tabBar->activeTabName();
        if (!channelName.isEmpty())
            messageInput->setPlaceholder("Message #" + channelName);
    }
}

QColor MainWindow::resolveRoleColor(Snowflake userId, Snowflake guildId)
{
    if (!currentInstance || guildId == Snowflake::Invalid)
        return QColor();

    if (cachedGuildId == guildId && userColorCache.contains(userId))
        return userColorCache.value(userId);

    QColor result;
    for (const auto &role : currentInstance->getMemberRolesSorted(guildId, userId)) {
        if (role.hasColor()) {
            result = role.getColor();
            break;
        }
    }

    if (cachedGuildId == guildId)
        userColorCache[userId] = result;

    return result;
}

void MainWindow::refreshGuildRoleData(Snowflake guildId)
{
    channelTreeModel->invalidateGuildData(guildId);

    if (cachedGuildId == guildId) {
        userColorCache.clear();
        chatModel->refreshUsersInView({});
        forumModel->refreshAuthors();
    }
}

namespace {

QWidget *buildUserMenuHeader(QMenu *parent, Core::Session *session, Snowflake accountId,
                             const QString &displayName, const QString &username,
                             const QUrl &avatarUrl)
{
    auto *header = new QWidget(parent);
    auto *layout = new QHBoxLayout(header);
    layout->setContentsMargins(10, 10, 16, 10);
    layout->setSpacing(12);

    constexpr QSize avatarSize(64, 64);
    auto *avatar = new QLabel(header);
    avatar->setFixedSize(avatarSize);
    session->getImageManager()->assign(avatar, avatarUrl, avatarSize, accountId);

    layout->addWidget(avatar);

    auto *textLayout = new QVBoxLayout;
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);

    auto *displayLabel = new QLabel(displayName, header);
    QFont displayFont = displayLabel->font();
    displayFont.setBold(true);
    displayFont.setPointSize(displayFont.pointSize() + 2);
    displayLabel->setFont(displayFont);
    textLayout->addWidget(displayLabel);

    if (!username.isEmpty() && username != displayName) {
        auto *usernameLabel = new QLabel(username, header);
        usernameLabel->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
        textLayout->addWidget(usernameLabel);
    }
    textLayout->addStretch();
    layout->addLayout(textLayout, 1);

    return header;
}

QWidget *buildRoleChip(const Discord::Role &role, QWidget *parent)
{
    auto *label = new QLabel(role.name.get(), parent);
    label->setContentsMargins(24, 4, 24, 4);
    if (role.hasColor())
        label->setStyleSheet(QStringLiteral("color: %1;").arg(role.getColor().name()));
    return label;
}

} // namespace

void MainWindow::showUserContextMenu(ClientInstance *instance, Snowflake userId, Snowflake guildId,
                                     QPoint globalPos)
{
    QMenu menu(this);
    QPointer<ClientInstance> instanceGuard(instance);

    if (instance) {
        auto user = instance->users()->getUser(userId);
        QString displayName = instance->users()->getDisplayName(userId, guildId);
        QString username = (user && user->username.hasValue()) ? user->username.get() : QString();
        QUrl avatarUrl = user ? instance->users()->getAvatarUrl(*user, guildId, 128)
                              : Discord::Cdn::defaultUserAvatar(userId, 128);

        auto *header = buildUserMenuHeader(&menu, session, instance->accountId(),
                                           displayName, username, avatarUrl);
        auto *headerAction = new QWidgetAction(&menu);
        headerAction->setDefaultWidget(header);
        menu.addAction(headerAction);
        menu.addSeparator();
    }

    QAction *profileAction = menu.addAction(tr("Profile"));
    connect(profileAction, &QAction::triggered, this, [this, instanceGuard, userId, guildId]() {
        showUserProfile(instanceGuard, userId, guildId);
    });

    QAction *mentionAction = menu.addAction(tr("Mention"));
    mentionAction->setEnabled(instance == currentInstance);
    connect(mentionAction, &QAction::triggered, this, [this, userId]() {
        messageInput->insertText(QStringLiteral("<@%1>").arg(quint64(userId)));
    });

    QAction *openDmAction = menu.addAction(tr("Open DM"));
    openDmAction->setEnabled(instance && userId != instance->accountId());
    connect(openDmAction, &QAction::triggered, this, [this, instanceGuard, userId]() {
        if (!instanceGuard)
            return;
        Snowflake accountId = instanceGuard->accountId();
        instanceGuard->openDmChannel(userId, [this, accountId](const Core::Result<Snowflake> &res) {
            if (res.success())
                selectChannelInTree(accountId, *res.value);
        });
    });

    if (instance && userId != instance->accountId()) {
        const auto cachedUser = instance->users()->getUser(userId);
        const bool botUser = cachedUser && cachedUser->bot.hasValue() && cachedUser->bot.get();
        if (!botUser) {
            const auto rel = instance->relationships()->getRelationship(userId);
            const auto type = (rel.has_value() && rel->type.hasValue())
                                  ? rel->type.get()
                                  : Discord::RelationshipType::NONE;

            // An add needs the handle, which is missing until the user is cached.
            const bool canAct = type != Discord::RelationshipType::NONE ||
                                (cachedUser && cachedUser->username.hasValue());
            if (canAct) {
                QString label;
                switch (type) {
                case Discord::RelationshipType::FRIEND:
                    label = tr("Remove Friend");
                    break;
                case Discord::RelationshipType::INCOMING_REQUEST:
                    label = tr("Accept Request");
                    break;
                case Discord::RelationshipType::OUTGOING_REQUEST:
                    label = tr("Cancel Request");
                    break;
                case Discord::RelationshipType::BLOCKED:
                    label = tr("Unblock");
                    break;
                default:
                    label = tr("Add Friend");
                    break;
                }

                QAction *friendAction = menu.addAction(label);
                connect(friendAction, &QAction::triggered, this,
                        [instanceGuard, userId, type]() {
                            Discord::Client *client = instanceGuard ? instanceGuard->discord() : nullptr;
                            if (!client)
                                return;
                            switch (type) {
                            case Discord::RelationshipType::INCOMING_REQUEST:
                                client->acceptFriendRequest(userId);
                                break;
                            case Discord::RelationshipType::NONE: {
                                const auto user = instanceGuard->users()->getUser(userId);
                                if (user && user->username.hasValue())
                                    client->sendFriendRequest(user->username.get(), user->discriminator.valueOr());
                                break;
                            }
                            default:
                                client->removeRelationship(userId);
                                break;
                            }
                        });

                if (type != Discord::RelationshipType::BLOCKED) {
                    QAction *blockAction = menu.addAction(tr("Block User"));
                    connect(blockAction, &QAction::triggered, this, [instanceGuard, userId]() {
                        Discord::Client *client = instanceGuard ? instanceGuard->discord() : nullptr;
                        if (client)
                            client->blockUser(userId);
                    });
                }
            }
        }
    }

    if (guildId.isValid() && instance) {
        const auto target = MemberModeration::targetFor(instance, guildId, userId);
        const bool moderationAddsRolesMenu = target && MemberModeration::canEditRoles(instance, *target);
        if (!moderationAddsRolesMenu) {
            QMenu *rolesMenu = menu.addMenu(tr("Roles"));
            const auto memberRoles = instance->getMemberRolesSorted(guildId, userId);
            if (memberRoles.isEmpty()) {
                rolesMenu->addAction(tr("No roles"))->setEnabled(false);
            } else {
                for (const auto &role : memberRoles) {
                    auto *action = new QWidgetAction(rolesMenu);
                    action->setDefaultWidget(buildRoleChip(role, rolesMenu));
                    rolesMenu->addAction(action);
                }
            }
        }
        if (target)
            MemberModeration::addModerationActions(&menu, this, instance, *target);
    }

    menu.addSeparator();
    QAction *copyIdAction = menu.addAction(tr("Copy User ID"));
    connect(copyIdAction, &QAction::triggered, this, [userId]() {
        QGuiApplication::clipboard()->setText(QString::number(quint64(userId)));
    });

    menu.exec(globalPos);
}

void MainWindow::selectChannelInTree(Snowflake accountId, Snowflake channelId)
{
    ChannelNode *node = channelTreeModel->findChannelTreeNode(channelId, accountId);
    if (!node)
        return;

    if (channelListMode == ChannelListMode::Classic) {
        if (node->type == ChannelNode::Type::DMChannel) {
            onRailAccountHomeSelected(accountId);
        } else {
            ChannelNode *guild = ChannelTreeModel::findGuildNode(node);
            if (guild)
                onRailGuildSelected(accountId, guild->id);
        }
    }

    QModelIndex sourceIndex = channelTreeModel->indexForNode(node);
    if (!sourceIndex.isValid())
        return;
    QModelIndex proxyIndex = channelFilterProxy->mapFromSource(sourceIndex);
    if (proxyIndex.isValid())
        channelTree->setCurrentIndex(proxyIndex);
}

void MainWindow::showChannelTopic(Core::ClientInstance *instance, Core::Snowflake channelId)
{
    channelTopicChannelId = channelId;
    const auto channel = instance->getChannel(channelId);
    const QString singleLineTopic = channel ? channel->topic.valueOr(QString()).simplified() : QString();
    channelTopicLine->setTopicHtml(singleLineTopic.isEmpty() ? QString() : instance->messages()->channelTopicHtml(singleLineTopic, channelId), instance->accountId());
}

void MainWindow::refreshChannelTopic()
{
    if (currentInstance && channelTopicChannelId.isValid())
        showChannelTopic(currentInstance, channelTopicChannelId);
}

void MainWindow::openReactionPicker(Core::Snowflake channelId, Core::Snowflake messageId, const QRect &globalAnchor)
{
    if (!currentInstance)
        return;

    reactionPickerTarget = ReactionPickerTarget{ channelId, messageId };
    chatView->setReactionPickerOpen(true);
    sharedExpressionPicker()->openForReaction(currentInstance->emojis(), currentInstance->accountId(), channelId, globalAnchor);
}

void MainWindow::openChatPicker(ExpressionPicker::Tab tab, const QRect &globalAnchor)
{
    if (!currentInstance)
        return;
    const Snowflake channelId = chatModel->getActiveChannelId();
    if (!channelId.isValid())
        return;

    reactionPickerTarget.reset();
    messageInput->setFocus();
    sharedExpressionPicker()->openForChat(currentInstance->emojis(), currentInstance->favoriteGifs(), currentInstance->accountId(), channelId, tab, globalAnchor);
}

void MainWindow::onChatPickerTabShown(ExpressionPicker::Tab tab)
{
    messageInput->setEmojiPickerOpen(tab == ExpressionPicker::Tab::Emoji);
    messageInput->setGifPickerOpen(tab == ExpressionPicker::Tab::Gifs);
}

ExpressionPicker *MainWindow::sharedExpressionPicker()
{
    if (expressionPicker)
        return expressionPicker;

    expressionPicker = new ExpressionPicker(session->getImageManager(), session->getAnimatedImageCache(), this);
    expressionPicker->setAnimationEnabled(QSettings().value("chat/animate_emoji", true).toBool());
    applyGifSettings(expressionPicker->gifPlayback());
    connect(expressionPicker, &ExpressionPicker::emojiPicked, this, &MainWindow::onEmojiPicked);
    connect(expressionPicker, &ExpressionPicker::gifPicked, this, &MainWindow::onGifPicked);
    connect(expressionPicker, &ExpressionPicker::chatTabShown, this, &MainWindow::onChatPickerTabShown);
    connect(expressionPicker, &ExpressionPicker::closed, this, &MainWindow::onExpressionPickerClosed);
    return expressionPicker;
}

void MainWindow::onEmojiPicked(const Core::PickerEmoji &emoji, bool pickerStaysOpen)
{
    if (!currentInstance)
        return;

    if (reactionPickerTarget)
        currentInstance->messages()->addReaction(reactionPickerTarget->channelId, reactionPickerTarget->messageId, emoji.toReactionEmoji(), false, Discord::Client::ReactionLocation::ReactionPicker);
    else
        messageInput->insertText(pickerStaysOpen ? emoji.messageText() : emoji.messageText() + " ");
}

void MainWindow::onGifPicked(const QString &url)
{
    if (sendFromChatInput(url, {}))
        messageInput->clearReplyTarget();
    messageInput->setFocus();
}

void MainWindow::onExpressionPickerClosed()
{
    reactionPickerTarget.reset();
    chatView->setReactionPickerOpen(false);
    messageInput->setEmojiPickerOpen(false);
    messageInput->setGifPickerOpen(false);
}

bool MainWindow::sendFromChatInput(const QString &text, const QList<Core::PendingAttachment> &attachments)
{
    if (!currentInstance) {
        qCWarning(LogCore) << "Cannot send message: no active instance";
        return false;
    }

    Snowflake channelId = chatModel->getActiveChannelId();
    if (!channelId.isValid()) {
        qCWarning(LogCore) << "Cannot send message: no active channel";
        return false;
    }

    if (messageInput->isSendBlocked()) {
        qCDebug(LogCore) << "Cannot send message: slowmode cooldown active";
        return false;
    }

    Snowflake replyTo = messageInput->replyTargetMessageId();
    if (!chatModel->isAtLatest())
        chatView->jumpToPresent();
    currentInstance->messages()->sendMessage(channelId, text, replyTo, attachments);

    int rateLimit = currentInstance->getChannelRateLimit(channelId);
    Snowflake userId = currentInstance->accountId();
    bool canBypass = currentInstance->permissions()->hasChannelPermission(
            userId, channelId, Discord::Permission::BYPASS_SLOWMODE);
    if (rateLimit > 0 && !canBypass) {
        slowModeIndicator->startCooldown(channelId, rateLimit);
        messageInput->setSendBlocked(true);
        messageInput->setPlaceholder("Slowmode is active");
    }
    return true;
}

void MainWindow::showFavoriteGifLimit()
{
    auto *box = new QMessageBox(QMessageBox::Information, tr("Oh no!"), tr("You cannot have more favorites."), QMessageBox::Ok, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();
}

void MainWindow::openReactors(Snowflake channelId, Snowflake messageId, const std::optional<Core::ReactionRef> &selected)
{
    if (!currentInstance)
        return;

    const Snowflake guildId = chatModel->getActiveGuildId();
    auto *popup = new ReactionsPopup(session->getImageManager(), currentInstance, channelId, guildId, messageId, selected, removableReactorsIn(currentInstance, channelId, guildId), this);
    connect(popup, &ReactionsPopup::userActivated, this, [this, popup, instance = QPointer<Core::ClientInstance>(currentInstance), guildId](Snowflake userId) {
        if (instance)
            showUserProfile(instance, userId, guildId, popup);
    });
    popup->open();
}

void MainWindow::openChannelTopicPopup()
{
    if (!currentInstance)
        return;
    const auto channel = currentInstance->getChannel(channelTopicChannelId);
    const QString topic = channel ? channel->topic.valueOr(QString()) : QString();
    if (topic.trimmed().isEmpty())
        return;

    auto *popup = new ChannelTopicPopup(channel->name.valueOr(QString()), currentInstance->messages()->channelTopicHtml(topic, channelTopicChannelId), session->getImageManager(), currentInstance->accountId(), this);
    popup->setAttribute(Qt::WA_DeleteOnClose);
    connect(popup, &ChannelTopicPopup::linkActivated, this, [this, popup](const QString &url) {
        popup->close();
        chatView->openLink(url);
    });
    popup->open();
}

void MainWindow::setThreadBrowserTarget(Core::Snowflake channelId)
{
    threadBrowserChannelId = channelId;
    channelToolbar->setVisible(channelId.isValid());
}

void MainWindow::openThreadBrowser()
{
    if (!currentInstance || !threadBrowserChannelId.isValid())
        return;

    if (!threadBrowser) {
        threadBrowser = new ThreadBrowserPopup(this);
        connect(threadBrowser, &ThreadBrowserPopup::threadActivated, this,
                &MainWindow::navigateToChannel);
    }

    QString name;
    if (auto ch = currentInstance->getChannel(threadBrowserChannelId); ch && ch->name.hasValue())
        name = ch->name.get();

    threadBrowser->configure(currentInstance, threadBrowserChannelId, name);
    threadBrowser->show();
    threadBrowser->raise();
}

void MainWindow::navigateToChannel(Core::Snowflake channelId)
{
    if (!currentInstance)
        return;
    Core::Snowflake acc = currentInstance->accountId();

    if (!channelTreeModel->findChannelTreeNode(channelId, acc)) {
        auto chOpt = currentInstance->getChannel(channelId);
        if (chOpt && chOpt->isThread())
            channelTreeModel->showTemporaryThread(*chOpt, acc);
    }
    selectChannelInTree(acc, channelId);
}

void MainWindow::jumpToMessageLink(Core::Snowflake channelId, Core::Snowflake messageId)
{
    if (channelId == chatModel->getActiveChannelId()) {
        chatView->jumpToMessage(messageId);
        return;
    }

    pendingLinkJump = { channelId, messageId };
    navigateToChannel(channelId);
}

} // namespace UI
} // namespace Acheron

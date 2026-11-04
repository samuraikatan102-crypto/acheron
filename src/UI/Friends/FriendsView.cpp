#include "FriendsView.hpp"

#include "FriendRow.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QAction>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSize>
#include <QStackedWidget>
#include <QString>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <optional>

#include "Core/ClientInstance.hpp"
#include "Core/ImageManager.hpp"
#include "Core/Presence/ActivityFormat.hpp"
#include "Core/Presence/PresenceManager.hpp"
#include "Core/RelationshipManager.hpp"
#include "Core/Result.hpp"
#include "Core/Session.hpp"
#include "Core/UserManager.hpp"
#include "Discord/CdnUrls.hpp"
#include "Discord/Client.hpp"

namespace Acheron {
namespace UI {

namespace {

constexpr int AvatarRequestSize = 48;

QString translated(const char *text)
{
    return QCoreApplication::translate("FriendsView", text);
}

QString kindWord(Discord::ActivityType kind)
{
    switch (kind) {
    case Discord::ActivityType::STREAMING:
        return translated("Streaming");
    case Discord::ActivityType::LISTENING:
        return translated("Listening to");
    case Discord::ActivityType::WATCHING:
        return translated("Watching");
    case Discord::ActivityType::COMPETING:
        return translated("Competing in");
    case Discord::ActivityType::CUSTOM:
        return translated("Custom Status");
    default:
        return translated("Playing");
    }
}

// The line under a friend's name reads like Discord's: "Playing Counter-Strike 2".
QString describeActivity(const Discord::Activity &activity)
{
    if (activity.isCustom())
        return activity.stateText();

    if (activity.kind() == Discord::ActivityType::STREAMING) {
        const QString stream = activity.detailsText();
        return stream.isEmpty() ? translated("Streaming") : stream;
    }

    const QString name = activity.nameText();
    const QString word = kindWord(activity.kind());
    if (name.isEmpty())
        return word;
    return word + QStringLiteral(" ") + name;
}

} // namespace

FriendsView::FriendsView(Core::Session *owner, QWidget *parent)
    : QWidget(parent), session(owner)
{
    buildUi();

    presenceRefresh = new QTimer(this);
    presenceRefresh->setSingleShot(true);
    presenceRefresh->setInterval(250);
    connect(presenceRefresh, &QTimer::timeout, this, &FriendsView::refreshPresence);

    selectTab(Tab::All);
}

void FriendsView::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("FriendsHeader"));
    header->setAttribute(Qt::WA_StyledBackground, true);
    header->setFixedHeight(48);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(16, 0, 16, 0);
    headerLayout->setSpacing(4);

    auto *title = new QLabel(translated("Friends"), header);
    title->setObjectName(QStringLiteral("FriendsTitle"));
    headerLayout->addWidget(title, 0);

    auto *dividerLine = new QFrame(header);
    dividerLine->setObjectName(QStringLiteral("FriendsDivider"));
    dividerLine->setFrameShape(QFrame::VLine);
    dividerLine->setFixedHeight(16);
    headerLayout->addWidget(dividerLine, 0);
    headerLayout->addSpacing(8);

    struct Spec
    {
        const char *text;
        Tab tab;
        const char *objectName;
    };

    const Spec specs[] = {
        { "All", Tab::All, "FriendsTab" },
        { "Online", Tab::Online, "FriendsTab" },
        { "Pending", Tab::Pending, "FriendsTab" },
        { "Blocked", Tab::Blocked, "FriendsTab" },
        { "Add Friend", Tab::Add, "FriendsTabAdd" },
    };

    for (const Spec &spec : specs) {
        auto *button = new QPushButton(translated(spec.text), header);
        button->setObjectName(QLatin1String(spec.objectName));
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        tabButtons.append(button);
        tabOrder.append(spec.tab);
        headerLayout->addWidget(button, 0);
        connect(button, &QPushButton::clicked, this, [this, tab = spec.tab]() {
            selectTab(tab);
        });
    }

    headerLayout->addStretch(1);

    root->addWidget(header, 0);

    // Discord keeps search on its own strip under the header so the tabs never
    // have to share a row with the field.
    searchRow = new QWidget(this);
    searchRow->setObjectName(QStringLiteral("FriendsSearchRow"));
    searchRow->setAttribute(Qt::WA_StyledBackground, true);
    searchRow->setFixedHeight(48);
    auto *searchLayout = new QHBoxLayout(searchRow);
    searchLayout->setContentsMargins(16, 0, 16, 0);
    searchLayout->setSpacing(8);

    search = new QLineEdit(searchRow);
    search->setObjectName(QStringLiteral("FriendsSearch"));
    search->setPlaceholderText(translated("Search"));
    search->setClearButtonEnabled(true);
    searchLayout->addWidget(search, 1);
    connect(search, &QLineEdit::textChanged, this, [this]() {
        if (tab != Tab::Add)
            rebuild();
    });

    root->addWidget(searchRow, 0);

    pages = new QStackedWidget(this);

    listPage = new QWidget(pages);
    auto *listRow = new QHBoxLayout(listPage);
    listRow->setContentsMargins(0, 0, 0, 0);
    listRow->setSpacing(0);

    auto *listScroll = new QScrollArea(listPage);
    listScroll->setFrameShape(QFrame::NoFrame);
    listScroll->setWidgetResizable(true);
    listScroll->setBackgroundRole(QPalette::Base);
    listScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *listContainer = new QWidget(listScroll);
    listContainer->setObjectName(QStringLiteral("FriendsList"));
    listContainer->setAttribute(Qt::WA_StyledBackground, true);
    listLayout = new QVBoxLayout(listContainer);
    listLayout->setContentsMargins(16, 0, 16, 12);
    listLayout->setSpacing(0);
    listLayout->addStretch(1);
    listScroll->setWidget(listContainer);
    listRow->addWidget(listScroll, 1);

    activePanel = new QWidget(listPage);
    activePanel->setObjectName(QStringLiteral("ActiveNowPanel"));
    activePanel->setAttribute(Qt::WA_StyledBackground, true);
    activePanel->setFixedWidth(280);
    auto *activePanelLayout = new QVBoxLayout(activePanel);
    activePanelLayout->setContentsMargins(0, 0, 0, 0);
    activePanelLayout->setSpacing(0);

    auto *activeScroll = new QScrollArea(activePanel);
    activeScroll->setFrameShape(QFrame::NoFrame);
    activeScroll->setWidgetResizable(true);
    activeScroll->setBackgroundRole(QPalette::Base);
    activeScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *activeContainer = new QWidget(activeScroll);
    activeContainer->setObjectName(QStringLiteral("FriendsList"));
    activeContainer->setAttribute(Qt::WA_StyledBackground, true);
    activeLayout = new QVBoxLayout(activeContainer);
    activeLayout->setContentsMargins(16, 16, 16, 16);
    activeLayout->setSpacing(12);
    activeScroll->setWidget(activeContainer);
    activePanelLayout->addWidget(activeScroll, 1);

    listRow->addWidget(activePanel, 0);
    pages->addWidget(listPage);

    addPage = new QWidget(pages);
    auto *addLayout = new QVBoxLayout(addPage);
    addLayout->setContentsMargins(16, 16, 16, 16);
    addLayout->setSpacing(6);

    auto *addTitle = new QLabel(translated("Add a friend"), addPage);
    addTitle->setObjectName(QStringLiteral("AddFriendTitle"));
    addLayout->addWidget(addTitle, 0);

    auto *addHint = new QLabel(translated("You can add friends with their Discord username."), addPage);
    addHint->setObjectName(QStringLiteral("AddFriendHint"));
    addLayout->addWidget(addHint, 0);
    addLayout->addSpacing(12);

    auto *addRow = new QHBoxLayout();
    addRow->setContentsMargins(0, 0, 0, 0);
    addRow->setSpacing(8);
    addEdit = new QLineEdit(addPage);
    addEdit->setObjectName(QStringLiteral("LoginInput"));
    addEdit->setPlaceholderText(translated("Enter a username"));
    addRow->addWidget(addEdit, 1);
    auto *sendButton = new QPushButton(translated("Send Friend Request"), addPage);
    sendButton->setObjectName(QStringLiteral("LoginPrimary"));
    addRow->addWidget(sendButton, 0);
    addLayout->addLayout(addRow);

    addStatus = new QLabel(addPage);
    addStatus->setObjectName(QStringLiteral("LoginStatus"));
    addStatus->hide();
    addLayout->addWidget(addStatus, 0);
    addLayout->addStretch(1);

    connect(sendButton, &QPushButton::clicked, this, [this]() {
        sendFriendRequest(addEdit->text());
    });
    connect(addEdit, &QLineEdit::returnPressed, this, [this]() {
        sendFriendRequest(addEdit->text());
    });

    pages->addWidget(addPage);

    root->addWidget(pages, 1);
}

void FriendsView::setInstance(Core::ClientInstance *newInstance)
{
    if (instance == newInstance)
        return;

    if (instance) {
        disconnect(instance->relationships(), nullptr, this, nullptr);
        disconnect(instance->presences(), nullptr, this, nullptr);
    }

    instance = newInstance;

    if (instance) {
        connect(instance->relationships(), &Core::RelationshipManager::relationshipsLoaded,
                this, &FriendsView::rebuild);
        connect(instance->relationships(), &Core::RelationshipManager::relationshipChanged,
                this, &FriendsView::rebuild);
        connect(instance->presences(), &Core::PresenceManager::presencesChanged, this,
                [this](const QList<Core::Snowflake> &) {
                    presenceRefresh->start();
                });
    }

    rebuild();
}

void FriendsView::selectTab(Tab newTab)
{
    tab = newTab;

    for (int i = 0; i < tabButtons.size(); ++i)
        tabButtons[i]->setChecked(tabOrder[i] == tab);

    const bool adding = tab == Tab::Add;
    pages->setCurrentWidget(adding ? addPage : listPage);
    searchRow->setVisible(!adding);
    activePanel->setVisible(!adding);

    if (adding) {
        addStatus->hide();
        addEdit->setFocus();
        return;
    }

    rebuild();
}

void FriendsView::setStatusText(const QString &text)
{
    addStatus->setText(text);
    addStatus->setVisible(!text.isEmpty());
}

void FriendsView::clearLayout(QVBoxLayout *layout)
{
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
}

QString FriendsView::displayNameFor(const Discord::Relationship &rel) const
{
    if (!instance)
        return QStringLiteral("unknown");

    Core::UserManager *users = instance->users();

    const QString nickname = users->getNickname(rel.id, Core::Snowflake::Invalid);
    if (!nickname.isEmpty())
        return nickname;

    // READY only carries the full user object for some relationship types, so the
    // cached user record has to win over whatever the relationship payload holds.
    if (const std::optional<Discord::User> cached = users->getUser(rel.id)) {
        const QString name = cached->getDisplayName();
        if (!name.isEmpty())
            return name;
    }

    if (rel.user.hasValue()) {
        const QString name = rel.user.get().getDisplayName();
        if (!name.isEmpty())
            return name;
    }

    return QStringLiteral("unknown");
}

QString FriendsView::usernameFor(const Discord::Relationship &rel) const
{
    if (!instance)
        return QString();

    QString username;
    if (const std::optional<Discord::User> cached = instance->users()->getUser(rel.id))
        username = cached->username.get();
    else if (rel.user.hasValue())
        username = rel.user.get().username.get();

    return username.isEmpty() ? QString() : QStringLiteral("@") + username;
}

QList<Discord::Relationship> FriendsView::relationshipsForCurrentTab() const
{
    using Discord::RelationshipType;

    QList<Discord::Relationship> list;
    if (!instance)
        return list;

    Core::RelationshipManager *relationships = instance->relationships();

    switch (tab) {
    case Tab::All:
        list = relationships->ofType(RelationshipType::FRIEND);
        list += relationships->ofType(RelationshipType::INCOMING_REQUEST);
        list += relationships->ofType(RelationshipType::OUTGOING_REQUEST);
        break;
    case Tab::Online: {
        const QList<Discord::Relationship> friends = relationships->ofType(RelationshipType::FRIEND);
        for (const Discord::Relationship &rel : friends) {
            const Discord::StatusType status = instance->presences()->badge(rel.id).status;
            if (status == Discord::StatusType::ONLINE || status == Discord::StatusType::IDLE)
                list.append(rel);
        }
        break;
    }
    case Tab::Pending:
        list = relationships->ofType(RelationshipType::INCOMING_REQUEST);
        list += relationships->ofType(RelationshipType::OUTGOING_REQUEST);
        break;
    case Tab::Blocked:
        list = relationships->ofType(RelationshipType::BLOCKED);
        break;
    case Tab::Add:
        break;
    }

    list.erase(std::remove_if(list.begin(), list.end(),
                              [](const Discord::Relationship &rel) {
                                  return rel.isSpamRequest.hasValue() && rel.isSpamRequest.get();
                              }),
               list.end());

    std::sort(list.begin(), list.end(), [this](const Discord::Relationship &left,
                                               const Discord::Relationship &right) {
        return displayNameFor(left).compare(displayNameFor(right), Qt::CaseInsensitive) < 0;
    });

    return list;
}

void FriendsView::rebuild()
{
    clearLayout(listLayout);
    rows.clear();
    names.clear();
    avatars.clear();

    if (!instance) {
        listLayout->addStretch(1);
        refreshActiveNow();
        return;
    }

    QList<Discord::Relationship> list = relationshipsForCurrentTab();

    const QString query = search ? search->text().trimmed() : QString();
    if (!query.isEmpty()) {
        const QString needle = query.toLower();
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [this, needle](const Discord::Relationship &rel) {
                                      return !displayNameFor(rel).toLower().contains(needle);
                                  }),
                   list.end());
    }

    QString heading;
    switch (tab) {
    case Tab::Online:
        heading = translated("Online");
        break;
    case Tab::Pending:
        heading = translated("Pending");
        break;
    case Tab::Blocked:
        heading = translated("Blocked Users");
        break;
    default:
        heading = translated("All Friends");
        break;
    }

    const auto isOnline = [this](const Discord::Relationship &rel) {
        const Discord::StatusType status = instance->presences()->badge(rel.id).status;
        return status == Discord::StatusType::ONLINE || status == Discord::StatusType::IDLE;
    };

    auto appendRow = [this](const Discord::Relationship &rel) {
        auto *row = new FriendRow(listPage);
        const QString display = displayNameFor(rel);
        names.insert(rel.id, display);

        row->setIdentity(rel.id, display, usernameFor(rel));

        std::optional<Discord::User> cached = instance->users()->getUser(rel.id);
        if (!cached && rel.user.hasValue())
            cached = rel.user.get();
        if (cached) {
            const QString hash = (cached->avatar.hasValue() && !cached->avatar.isNull())
                                         ? cached->avatar.get()
                                         : QString();
            const QUrl url = Discord::Cdn::userAvatar(rel.id, hash, AvatarRequestSize);
            avatars.insert(rel.id, url);
            session->getImageManager()->assign(row->avatar(), url,
                                               QSize(AvatarRequestSize, AvatarRequestSize),
                                               instance->accountId());
        }

        rows.insert(rel.id, row);
        listLayout->addWidget(row, 0);

        connect(row, &FriendRow::messageClicked, this, &FriendsView::directMessageRequested);
        connect(row, &FriendRow::menuRequested, this, &FriendsView::showRowMenu);
    };

    // Discord groups the list under section headers that carry their own count.
    auto addSection = [this, appendRow](const QString &title,
                                        const QList<Discord::Relationship> &group) {
        if (group.isEmpty())
            return;

        auto *label = new QLabel(QStringLiteral("%1  %2").arg(title, QString::number(group.size())),
                                 listPage);
        label->setObjectName(QStringLiteral("FriendSection"));
        listLayout->addWidget(label, 0);

        for (const Discord::Relationship &rel : group)
            appendRow(rel);
    };

    if (list.isEmpty()) {
        auto *empty = new QLabel(tab == Tab::Blocked ? translated("You haven't blocked anyone.")
                                                     : translated("No one here yet."),
                                 listPage);
        empty->setObjectName(QStringLiteral("AddFriendHint"));
        empty->setAlignment(Qt::AlignHCenter);
        empty->setContentsMargins(0, 32, 0, 0);
        listLayout->addWidget(empty, 0);
    } else if (tab == Tab::All) {
        QList<Discord::Relationship> online;
        QList<Discord::Relationship> everyone;
        for (const Discord::Relationship &rel : list) {
            if (isOnline(rel))
                online.append(rel);
            else
                everyone.append(rel);
        }

        addSection(translated("Online"), online);
        addSection(translated("Everyone"), everyone);
    } else {
        addSection(heading, list);
    }

    listLayout->addStretch(1);

    refreshPresence();
}

void FriendsView::refreshPresence()
{
    if (!instance)
        return;

    Core::PresenceManager *presences = instance->presences();

    for (auto it = rows.begin(); it != rows.end(); ++it) {
        const Core::PresenceBadge badge = presences->badge(it.key());
        const Discord::Activity *activity = presences->primaryActivity(it.key());

        it.value()->setBadge(badge);
        // Discord only spells out an activity under the name; a bare status lives
        // on the avatar dot, so an idle friend shows just their handle.
        it.value()->setStatusLine(activity ? describeActivity(*activity) : QString());
    }

    refreshActiveNow();
}

void FriendsView::refreshActiveNow()
{
    clearLayout(activeLayout);

    auto *heading = new QLabel(translated("Active Now"));
    heading->setObjectName(QStringLiteral("ActiveNowHeading"));
    activeLayout->addWidget(heading, 0);

    Core::ImageManager *images = session ? session->getImageManager() : nullptr;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    int shown = 0;

    for (auto it = rows.begin(); it != rows.end(); ++it) {
        const Discord::Activity *activity = instance ? instance->presences()->primaryActivity(it.key())
                                                     : nullptr;
        if (!activity)
            continue;

        auto *card = new QWidget();
        card->setObjectName(QStringLiteral("ActiveNowCard"));
        card->setAttribute(Qt::WA_StyledBackground, true);
        auto *cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(12, 12, 12, 12);
        cardLayout->setSpacing(10);

        auto *avatar = new FriendAvatar(card);
        if (instance) {
            avatar->setBadge(instance->presences()->badge(it.key()));
            if (images && avatars.contains(it.key()))
                images->assign(avatar, avatars.value(it.key()),
                               QSize(AvatarRequestSize, AvatarRequestSize), instance->accountId());
        }
        cardLayout->addWidget(avatar, 0);

        auto *text = new QVBoxLayout();
        text->setContentsMargins(0, 0, 0, 0);
        text->setSpacing(2);

        auto *name = new QLabel(names.value(it.key()), card);
        name->setObjectName(QStringLiteral("ActiveNowName"));
        text->addWidget(name, 0);

        auto *kind = new QLabel(kindWord(activity->kind()), card);
        kind->setObjectName(QStringLiteral("ActiveNowKind"));
        text->addWidget(kind, 0);

        const QString detail = activity->isCustom() ? activity->stateText() : activity->nameText();
        if (!detail.isEmpty()) {
            auto *detailLabel = new QLabel(detail, card);
            detailLabel->setObjectName(QStringLiteral("ActiveNowDetail"));
            detailLabel->setWordWrap(true);
            text->addWidget(detailLabel, 0);
        }

        const Core::ActivityFormat::TimerInfo timer = Core::ActivityFormat::timerFor(*activity);
        const QString timerText = Core::ActivityFormat::timerText(timer, nowMs);
        if (!timerText.isEmpty()) {
            auto *elapsed = new QLabel(timerText, card);
            elapsed->setObjectName(QStringLiteral("ActiveNowDetail"));
            text->addWidget(elapsed, 0);
        }

        cardLayout->addLayout(text, 1);
        activeLayout->addWidget(card, 0);
        ++shown;
    }

    if (shown == 0) {
        auto *empty = new QLabel(translated("No one else is active right now."));
        empty->setObjectName(QStringLiteral("AddFriendHint"));
        empty->setWordWrap(true);
        activeLayout->addWidget(empty, 0);
    }

    activeLayout->addStretch(1);
}

void FriendsView::showRowMenu(Core::Snowflake userId, const QPoint &globalPos)
{
    if (!instance)
        return;

    Discord::Client *client = instance->discord();
    const std::optional<Discord::Relationship> rel =
            instance->relationships()->getRelationship(userId);
    const Discord::RelationshipType type = rel && rel->type.hasValue()
                                                   ? rel->type.get()
                                                   : Discord::RelationshipType::NONE;

    QMenu menu(this);

    QAction *profile = menu.addAction(translated("Profile"));
    connect(profile, &QAction::triggered, this, [this, userId]() {
        emit profileRequested(userId);
    });

    QPointer<Core::ClientInstance> guard(instance);

    switch (type) {
    case Discord::RelationshipType::INCOMING_REQUEST: {
        QAction *accept = menu.addAction(translated("Accept Request"));
        connect(accept, &QAction::triggered, this, [guard, userId]() {
            if (guard && guard->discord())
                guard->discord()->acceptFriendRequest(userId);
        });
        QAction *ignore = menu.addAction(translated("Ignore Request"));
        connect(ignore, &QAction::triggered, this, [guard, userId]() {
            if (guard && guard->discord())
                guard->discord()->removeRelationship(userId);
        });
        break;
    }
    case Discord::RelationshipType::OUTGOING_REQUEST: {
        QAction *cancel = menu.addAction(translated("Cancel Request"));
        connect(cancel, &QAction::triggered, this, [guard, userId]() {
            if (guard && guard->discord())
                guard->discord()->removeRelationship(userId);
        });
        break;
    }
    case Discord::RelationshipType::BLOCKED: {
        QAction *unblock = menu.addAction(translated("Unblock"));
        connect(unblock, &QAction::triggered, this, [guard, userId]() {
            if (guard && guard->discord())
                guard->discord()->removeRelationship(userId);
        });
        break;
    }
    case Discord::RelationshipType::FRIEND: {
        QAction *message = menu.addAction(translated("Send Message"));
        connect(message, &QAction::triggered, this, [this, userId]() {
            emit directMessageRequested(userId);
        });
        QAction *unfriend = menu.addAction(translated("Remove Friend"));
        connect(unfriend, &QAction::triggered, this, [guard, userId]() {
            if (guard && guard->discord())
                guard->discord()->removeRelationship(userId);
        });
        break;
    }
    default: {
        QAction *message = menu.addAction(translated("Send Message"));
        connect(message, &QAction::triggered, this, [this, userId]() {
            emit directMessageRequested(userId);
        });
        break;
    }
    }

    if (type != Discord::RelationshipType::BLOCKED && client) {
        QAction *block = menu.addAction(translated("Block User"));
        connect(block, &QAction::triggered, this, [guard, userId]() {
            if (guard && guard->discord())
                guard->discord()->blockUser(userId);
        });
    }

    menu.exec(globalPos);
}

void FriendsView::sendFriendRequest(const QString &input)
{
    if (!instance || !instance->discord()) {
        setStatusText(translated("Connect to Discord first."));
        return;
    }

    QString handle = input.trimmed();
    QString discriminator;

    const int separator = handle.lastIndexOf(QLatin1Char('#'));
    if (separator > 0) {
        discriminator = handle.mid(separator + 1);
        handle = handle.left(separator);
    }

    if (handle.isEmpty()) {
        setStatusText(translated("Enter a username to add."));
        return;
    }

    QPointer<FriendsView> guard(this);
    instance->discord()->sendFriendRequest(handle, discriminator, [guard](const Core::Result<void> &result) {
        if (!guard)
            return;
        if (result.success()) {
            guard->addEdit->clear();
            guard->setStatusText(translated("Friend request sent."));
        } else {
            guard->setStatusText(result.error.isEmpty()
                                         ? translated("That username couldn't be found. Check the spelling and try again.")
                                         : result.error);
        }
    });
}

} // namespace UI
} // namespace Acheron

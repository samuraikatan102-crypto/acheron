#include "FriendRow.hpp"

#include <QContextMenuEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QSize>
#include <QToolButton>
#include <QVBoxLayout>

#include "Core/Theme/Icons.hpp"
#include "Core/Theme/Manager.hpp"
#include "UI/StatusIndicator.hpp"

namespace Acheron {
namespace UI {

namespace {

constexpr int AvatarSize = 32;
constexpr int ButtonSize = 32;
constexpr int RowHeight = 64;

QColor themeColor(Core::Theme::Token token)
{
    return Core::Theme::Manager::instance().color(token);
}

} // namespace

FriendAvatar::FriendAvatar(QWidget *parent) : QLabel(parent)
{
    setFixedSize(AvatarSize, AvatarSize);
    setScaledContents(true);
    ringColor = themeColor(Core::Theme::Token::BaseBg);
}

void FriendAvatar::setBadge(const Core::PresenceBadge &newBadge)
{
    badge = newBadge;
    setToolTip(StatusIndicator::tooltip(newBadge));
    update();
}

void FriendAvatar::setRingColor(const QColor &color)
{
    if (ringColor == color)
        return;
    ringColor = color;
    update();
}

QSize FriendAvatar::sizeHint() const
{
    return QSize(AvatarSize, AvatarSize);
}

void FriendAvatar::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QRectF body = rect();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QPixmap icon = pixmap();
#else
    const QPixmap *stored = pixmap();
    const QPixmap icon = stored ? *stored : QPixmap();
#endif
    if (!icon.isNull()) {
        QPainterPath circle;
        circle.addEllipse(body);
        painter.setClipPath(circle);
        painter.drawPixmap(body.toRect(), icon);
        painter.setClipping(false);
    } else {
        painter.setPen(Qt::NoPen);
        painter.setBrush(themeColor(Core::Theme::Token::ButtonBg));
        painter.drawEllipse(body.adjusted(0.5, 0.5, -0.5, -0.5));
    }

    StatusIndicator::paintOnAvatar(painter, rect().adjusted(0, 0, -1, -1), dotSize, badge, ringColor);
}

FriendRow::FriendRow(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_Hover, true);
    setMinimumHeight(RowHeight);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(12);

    avatarLabel = new FriendAvatar(this);
    layout->addWidget(avatarLabel, 0);

    auto *text = new QVBoxLayout();
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(1);

    nameLabel = new QLabel(this);
    nameLabel->setObjectName(QStringLiteral("FriendName"));
    text->addWidget(nameLabel, 0);

    handleLabel = new QLabel(this);
    handleLabel->setObjectName(QStringLiteral("FriendHandle"));
    text->addWidget(handleLabel, 0);

    statusLabel = new QLabel(this);
    statusLabel->setObjectName(QStringLiteral("FriendStatusLine"));
    statusLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    statusLabel->hide();
    text->addWidget(statusLabel, 0);

    layout->addLayout(text, 1);

    // The buttons sit in a fixed-width box so the name and status columns never
    // shift sideways when a row changes shape.
    auto *actions = new QWidget(this);
    actions->setFixedWidth(ButtonSize * 2 + 8);
    auto *actionsLayout = new QHBoxLayout(actions);
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(8);

    messageButton = new QToolButton(actions);
    messageButton->setObjectName(QStringLiteral("FriendActionButton"));
    messageButton->setAutoRaise(true);
    messageButton->setFixedSize(ButtonSize, ButtonSize);
    messageButton->setIcon(Core::Theme::Icons::icon(Core::Theme::Icons::Name::MessageCircle,
                                                   Core::Theme::Token::PrimaryText));
    messageButton->setIconSize(QSize(18, 18));
    messageButton->setCursor(Qt::PointingHandCursor);
    messageButton->setToolTip(tr("Send Message"));

    menuButton = new QToolButton(actions);
    menuButton->setObjectName(QStringLiteral("FriendActionButton"));
    menuButton->setAutoRaise(true);
    menuButton->setFixedSize(ButtonSize, ButtonSize);
    menuButton->setIcon(Core::Theme::Icons::icon(Core::Theme::Icons::Name::Ellipsis,
                                                Core::Theme::Token::PrimaryText));
    menuButton->setIconSize(QSize(18, 18));
    menuButton->setCursor(Qt::PointingHandCursor);
    menuButton->setToolTip(tr("More"));

    actionsLayout->addWidget(messageButton, 0);
    actionsLayout->addWidget(menuButton, 0);
    layout->addWidget(actions, 0);

    connect(messageButton, &QToolButton::clicked, this, [this]() {
        emit messageClicked(id);
    });
    connect(menuButton, &QToolButton::clicked, this, [this]() {
        emit menuRequested(id, menuButton->mapToGlobal(QPoint(0, menuButton->height())));
    });
}

void FriendRow::setIdentity(Core::Snowflake userId, const QString &name, const QString &handle)
{
    id = userId;
    nameLabel->setText(name);
    nameLabel->setToolTip(name);
    handleLabel->setText(handle);
    handleLabel->setVisible(!handle.isEmpty());
}

void FriendRow::setStatusLine(const QString &text)
{
    statusLabel->setText(text);
    statusLabel->setVisible(!text.isEmpty());
}

void FriendRow::setBadge(const Core::PresenceBadge &newBadge)
{
    avatarLabel->setBadge(newBadge);
}

bool FriendRow::event(QEvent *event)
{
    switch (event->type()) {
    case QEvent::HoverEnter:
        setHovered(true);
        break;
    case QEvent::HoverLeave:
        setHovered(false);
        break;
    default:
        break;
    }
    return QWidget::event(event);
}

void FriendRow::setHovered(bool isHovered)
{
    if (hovered == isHovered)
        return;
    hovered = isHovered;
    avatarLabel->setRingColor(themeColor(Core::Theme::Token::BaseBg).lighter(isHovered ? 112 : 100));
    update();
}

void FriendRow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    const QColor base = themeColor(Core::Theme::Token::BaseBg);
    painter.fillRect(rect(), hovered ? base.lighter(112) : base);

    // the separator has to read against the chat background, so it comes from
    // the button colour the same way the stylesheet builds it
    painter.setPen(themeColor(Core::Theme::Token::ButtonBg).lighter(118));
    painter.drawLine(rect().bottomLeft(), rect().bottomRight());
}

void FriendRow::contextMenuEvent(QContextMenuEvent *event)
{
    emit menuRequested(id, event->globalPos());
}

} // namespace UI
} // namespace Acheron

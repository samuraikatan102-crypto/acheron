#pragma once

#include <QColor>
#include <QLabel>
#include <QPoint>
#include <QWidget>

#include "Core/Presence/PresenceBadge.hpp"
#include "Core/Snowflake.hpp"

class QContextMenuEvent;
class QEvent;
class QPaintEvent;
class QToolButton;

namespace Acheron {
namespace UI {

// Round avatar with the presence badge sitting on its bottom-right corner.
class FriendAvatar : public QLabel
{
    Q_OBJECT
public:
    explicit FriendAvatar(QWidget *parent = nullptr);

    void setBadge(const Core::PresenceBadge &newBadge);
    void setRingColor(const QColor &color);
    void setSize(int side);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Core::PresenceBadge badge;
    QColor ringColor;
    int dotSize = 10;
    int side = 48;
};

// One row of the friends list: avatar, name, status line and the two buttons
// Discord only shows while the pointer is over the row.
class FriendRow : public QWidget
{
    Q_OBJECT
public:
    explicit FriendRow(QWidget *parent = nullptr);

    void setIdentity(Core::Snowflake userId, const QString &name, const QString &handle);
    void setStatusLine(const QString &text);
    void setBadge(const Core::PresenceBadge &newBadge);

    [[nodiscard]] Core::Snowflake userId() const { return id; }
    [[nodiscard]] FriendAvatar *avatar() const { return avatarLabel; }

signals:
    void messageClicked(Core::Snowflake userId);
    void menuRequested(Core::Snowflake userId, const QPoint &globalPos);

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void setHovered(bool hovered);

    Core::Snowflake id;
    bool hovered = false;
    FriendAvatar *avatarLabel = nullptr;
    QLabel *nameLabel = nullptr;
    QLabel *handleLabel = nullptr;
    QLabel *statusLabel = nullptr;
    QToolButton *messageButton = nullptr;
    QToolButton *menuButton = nullptr;
};

} // namespace UI
} // namespace Acheron

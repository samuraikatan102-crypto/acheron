#pragma once

#include <QHash>
#include <QUrl>
#include <QWidget>

#include "Core/Snowflake.hpp"
#include "Discord/Entities.hpp"
#include "Discord/Enums.hpp"

class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTimer;
class QVBoxLayout;

namespace Acheron {
namespace Core {
class ClientInstance;
class Session;
} // namespace Core

namespace UI {

class FriendRow;

// The friends page: Discord's header with tabs and search, the list of
// relationships on the left and the Active Now panel on the right.
class FriendsView : public QWidget
{
    Q_OBJECT
public:
    explicit FriendsView(Core::Session *session, QWidget *parent = nullptr);

    void setInstance(Core::ClientInstance *newInstance);

signals:
    void directMessageRequested(Core::Snowflake userId);
    void profileRequested(Core::Snowflake userId);

private:
    enum class Tab {
        Add,
        All,
        Online,
        Pending,
        Blocked,
    };

    void buildUi();
    void selectTab(Tab newTab);
    void clearLayout(QVBoxLayout *layout);
    void rebuild();
    void refreshPresence();
    void refreshActiveNow();
    void showRowMenu(Core::Snowflake userId, const QPoint &globalPos);
    void sendFriendRequest(const QString &input);

    [[nodiscard]] QList<Discord::Relationship> relationshipsForCurrentTab() const;
    [[nodiscard]] QString displayNameFor(const Discord::Relationship &rel) const;
    void setStatusText(const QString &text);

    Core::Session *session;
    Core::ClientInstance *instance = nullptr;
    Tab tab = Tab::All;

    QStackedWidget *pages = nullptr;
    QWidget *listPage = nullptr;
    QWidget *addPage = nullptr;
    QVBoxLayout *listLayout = nullptr;
    QVBoxLayout *activeLayout = nullptr;
    QWidget *activePanel = nullptr;
    QHash<Core::Snowflake, FriendRow *> rows;
    QHash<Core::Snowflake, QString> names;
    QHash<Core::Snowflake, QUrl> avatars;
    QList<QPushButton *> tabButtons;
    QList<Tab> tabOrder;
    QLineEdit *search = nullptr;
    QLineEdit *addEdit = nullptr;
    QLabel *addStatus = nullptr;
    QTimer *presenceRefresh = nullptr;
};

} // namespace UI
} // namespace Acheron

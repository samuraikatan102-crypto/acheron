#pragma once

#include <QtWidgets>

#include "Core/Snowflake.hpp"

namespace Acheron {
namespace Core {
namespace Audio {
class VoiceManager;
}
} // namespace Core

namespace UI {
class AccountsModel;

// User Settings, opened as a page of the main window the same way Discord does:
// a category list on the left, the selected page on the right, close with the X
// or Escape.
class SettingsWindow : public QWidget
{
    Q_OBJECT
public:
    explicit SettingsWindow(QWidget *parent = nullptr);

    void setAccountsModel(AccountsModel *model);
    void setVoiceManager(Core::Audio::VoiceManager *voiceManager);
    void openPage(const QString &name);

signals:
    void channelListModeChanged(bool classic);
    void channelListIndentChanged();
    void closeToTrayChanged();
    void animateEmojiChanged(bool enabled);
    void animateStickersChanged(bool enabled);
    void gifSettingsChanged();
    void animationCacheLimitChanged(int megabytes);
    void animationSizeLimitChanged(int megabytes);
    void voiceSettingsChanged();
    void closeRequested();
    void addAccountRequested();
    void logOutRequested(Core::Snowflake accountId);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void setupUi();
    QWidget *buildAccountsPage();
    void refreshAccounts();

    QListWidget *categoryList;
    QStackedWidget *pages;
    QWidget *voicePage = nullptr;
    QVBoxLayout *accountsLayout = nullptr;

    AccountsModel *accountsModel = nullptr;
    Core::Audio::VoiceManager *voiceManager = nullptr;
};

} // namespace UI
} // namespace Acheron

#pragma once

#include <QWidget>
#include <QString>

#include "Core/ProxyConfig.hpp"
#include "Core/Snowflake.hpp"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;

namespace Acheron {

namespace Core {
class Session;
}

namespace Discord {
class CaptchaResolver;
class LoginClient;
class RemoteAuthClient;
struct MfaChallenge;
enum class LoginError;
enum class MfaMethod;
enum class RemoteAuthError;
} // namespace Discord

namespace UI {

class AccountsModel;
class ProxyLineEdit;

// Discord's own sign-in screen: a card with a QR code, an email and password
// form, and the two-factor page behind it. It replaces the old token prompt.
class LoginView : public QWidget
{
    Q_OBJECT
public:
    explicit LoginView(Core::Session *session, AccountsModel *model, QWidget *parent = nullptr);

    void showQrPage();
    void showPasswordPage();

signals:
    void loggedIn(Core::Snowflake accountId);

protected:
    void showEvent(QShowEvent *event) override;

private:
    QWidget *buildQrPage();
    QWidget *buildFormPage();
    QWidget *buildMfaPage();

    void startQrLogin();
    void ensureRemoteClient(const Core::ProxyConfig &wanted);
    void ensureLoginClient(const Core::ProxyConfig &wanted);

    void onLogInClicked();
    void onQrProxyClicked();
    void onBackClicked();
    void onVerifyClicked();
    void onSmsClicked();
    void onMfaMethodChanged();

    void onMfaRequired(const Discord::MfaChallenge &challenge);
    void onSmsSent(bool ok);
    void onLoginAuthenticated(const QString &token);
    void onFailed(Discord::LoginError error);

    void renderQr(const QString &fingerprint);
    void onPendingTicket(const QString &userId, const QString &name, const QString &avatarHash);
    void onRemoteAuthenticated(const QString &token);
    void onRemoteFailed(Discord::RemoteAuthError error);
    void onRemoteCanceled();

    [[nodiscard]] Discord::MfaMethod currentMfaMethod() const;
    void storeToken(const QString &token);
    void setStatus(QLabel *label, const QString &text, bool error = true);
    QLabel *statusForCurrentPage() const;
    void setBusy(bool busy);

    Core::Session *session = nullptr;
    AccountsModel *model = nullptr;
    Discord::CaptchaResolver *captchaResolver = nullptr;

    Discord::LoginClient *loginClient = nullptr;
    Core::ProxyConfig proxy;

    Discord::RemoteAuthClient *remoteClient = nullptr;
    Core::ProxyConfig remoteProxy;
    bool remoteStarted = false;
    bool remoteBroken = false;

    QStackedWidget *stack = nullptr;
    QWidget *qrPage = nullptr;
    QWidget *formPage = nullptr;
    QWidget *mfaPage = nullptr;

    QLabel *qrImage = nullptr;
    QLabel *qrStatus = nullptr;
    QPushButton *qrProxyButton = nullptr;

    QLineEdit *loginEdit = nullptr;
    QLineEdit *passwordEdit = nullptr;
    ProxyLineEdit *proxyEdit = nullptr;
    QLabel *formStatus = nullptr;
    QPushButton *loginButton = nullptr;

    QLabel *mfaTitle = nullptr;
    QComboBox *mfaMethodCombo = nullptr;
    QLineEdit *codeEdit = nullptr;
    QPushButton *smsButton = nullptr;
    QLabel *mfaStatus = nullptr;
    QPushButton *backButton = nullptr;
    QPushButton *verifyButton = nullptr;

    QString pendingUsername;
};

} // namespace UI
} // namespace Acheron

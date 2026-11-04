#include "UI/Login/LoginView.hpp"

#include "Core/AccountInfo.hpp"
#include "Core/ImageManager.hpp"
#include "Core/ProxyConfig.hpp"
#include "Core/Session.hpp"
#include "Core/Snowflake.hpp"
#include "Core/Theme/Manager.hpp"
#include "Core/TokenUtils.hpp"
#include "Discord/CaptchaResolver.hpp"
#include "Discord/CdnUrls.hpp"
#include "Discord/LoginClient.hpp"
#include "Discord/RemoteAuthClient.hpp"
#include "UI/Accounts/AccountsModel.hpp"
#include "UI/Dialogs/ProxyInputDialog.hpp"
#include "UI/ProxyLineEdit.hpp"

#include "qrcodegen.hpp"

#include <QComboBox>
#include <QColor>
#include <QDesktopServices>
#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace Acheron {
namespace UI {

namespace {

constexpr int QrSize = 216;

QLabel *makeLabel(const char *objectName, const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QLatin1String(objectName));
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignCenter);
    return label;
}

QLabel *makeFieldLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("LoginFieldLabel"));
    return label;
}

QLabel *makeStatus(QWidget *parent)
{
    QLabel *label = makeLabel("LoginStatus", QString(), parent);
    label->hide();
    return label;
}

QPushButton *makeLinkButton(const QString &text, QWidget *parent)
{
    auto *button = new QPushButton(text, parent);
    button->setObjectName(QStringLiteral("LoginLink"));
    button->setFlat(true);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

QFrame *makeDivider(QWidget *parent)
{
    auto *divider = new QFrame(parent);
    divider->setObjectName(QStringLiteral("LoginDivider"));
    divider->setFrameShape(QFrame::HLine);
    return divider;
}

} // namespace

LoginView::LoginView(Core::Session *session, AccountsModel *model, QWidget *parent)
    : QWidget(parent), session(session), model(model), captchaResolver(session->getCaptchaResolver())
{
    setObjectName(QStringLiteral("LoginView"));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(24, 24, 24, 24);

    auto *card = new QWidget(this);
    card->setObjectName(QStringLiteral("LoginCard"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    card->setFixedWidth(480);
    card->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(32, 32, 32, 24);
    cardLayout->setSpacing(16);

    stack = new QStackedWidget(card);
    stack->setObjectName(QStringLiteral("LoginStack"));
    stack->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    qrPage = buildQrPage();
    formPage = buildFormPage();
    mfaPage = buildMfaPage();
    stack->addWidget(qrPage);
    stack->addWidget(formPage);
    stack->addWidget(mfaPage);
    stack->setCurrentWidget(qrPage);

    cardLayout->addWidget(stack);

    outer->addStretch(1);
    outer->addWidget(card, 0, Qt::AlignHCenter);
    outer->addStretch(1);

    ensureRemoteClient(remoteProxy);
}

QWidget *LoginView::buildQrPage()
{
    auto *page = new QWidget(stack);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    layout->addWidget(makeLabel("LoginTitle", tr("Log in with QR code"), page), 0, Qt::AlignHCenter);
    layout->addWidget(makeLabel("LoginSubtitle",
                               tr("Open Discord on your phone, go to Settings, tap \"Scan QR Code\" "
                                  "and scan this code to log in here."),
                               page));

    qrImage = new QLabel(page);
    qrImage->setObjectName(QStringLiteral("LoginQr"));
    qrImage->setFixedSize(QrSize, QrSize);
    qrImage->setScaledContents(true);
    qrImage->setAlignment(Qt::AlignCenter);
    layout->addWidget(qrImage, 0, Qt::AlignHCenter);

    qrStatus = makeStatus(page);
    layout->addWidget(qrStatus);

    qrProxyButton = makeLinkButton(tr("Connect through a proxy"), page);
    layout->addWidget(qrProxyButton, 0, Qt::AlignHCenter);

    layout->addWidget(makeDivider(page));

    auto *passwordButton = makeLinkButton(tr("Log in with an email and password instead"), page);
    layout->addWidget(passwordButton, 0, Qt::AlignHCenter);

    connect(qrProxyButton, &QPushButton::clicked, this, &LoginView::onQrProxyClicked);
    connect(passwordButton, &QPushButton::clicked, this, &LoginView::showPasswordPage);

    return page;
}

QWidget *LoginView::buildFormPage()
{
    auto *page = new QWidget(stack);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    layout->addWidget(makeLabel("LoginTitle", tr("Welcome back!"), page), 0, Qt::AlignHCenter);
    layout->addWidget(makeLabel("LoginSubtitle", tr("We're so excited to see you again!"), page));

    loginEdit = new QLineEdit(page);
    loginEdit->setObjectName(QStringLiteral("LoginInput"));
    loginEdit->setPlaceholderText(tr("Email or phone number"));
    layout->addWidget(makeFieldLabel(tr("EMAIL OR PHONE NUMBER"), page));
    layout->addWidget(loginEdit);

    passwordEdit = new QLineEdit(page);
    passwordEdit->setObjectName(QStringLiteral("LoginInput"));
    passwordEdit->setEchoMode(QLineEdit::Password);
    passwordEdit->setPlaceholderText(tr("Password"));
    layout->addWidget(makeFieldLabel(tr("PASSWORD"), page));
    layout->addWidget(passwordEdit);

    proxyEdit = new ProxyLineEdit(page);
    proxyEdit->setObjectName(QStringLiteral("LoginInput"));
    proxyEdit->setPlaceholderText(tr("socks5://127.0.0.1:1080"));
    layout->addWidget(makeFieldLabel(tr("PROXY (OPTIONAL)"), page));
    layout->addWidget(proxyEdit);

    auto *forgot = makeLinkButton(tr("Forgot your password?"), page);
    layout->addWidget(forgot, 0, Qt::AlignLeft);

    formStatus = makeStatus(page);
    layout->addWidget(formStatus);

    loginButton = new QPushButton(tr("Log In"), page);
    loginButton->setObjectName(QStringLiteral("LoginPrimary"));
    loginButton->setDefault(true);
    layout->addWidget(loginButton);

    layout->addWidget(makeDivider(page));

    auto *qrButton = makeLinkButton(tr("Log in with a QR code instead"), page);
    layout->addWidget(qrButton, 0, Qt::AlignHCenter);

    connect(loginButton, &QPushButton::clicked, this, &LoginView::onLogInClicked);
    connect(loginEdit, &QLineEdit::returnPressed, this, &LoginView::onLogInClicked);
    connect(passwordEdit, &QLineEdit::returnPressed, this, &LoginView::onLogInClicked);
    connect(qrButton, &QPushButton::clicked, this, &LoginView::showQrPage);
    connect(forgot, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://discord.com/login?forgot_password")));
    });

    return page;
}

QWidget *LoginView::buildMfaPage()
{
    auto *page = new QWidget(stack);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    mfaTitle = makeLabel("LoginTitle", tr("Two-factor authentication"), page);
    layout->addWidget(mfaTitle, 0, Qt::AlignHCenter);

    mfaMethodCombo = new QComboBox(page);
    mfaMethodCombo->setObjectName(QStringLiteral("LoginInput"));
    layout->addWidget(mfaMethodCombo);

    codeEdit = new QLineEdit(page);
    codeEdit->setObjectName(QStringLiteral("LoginInput"));
    codeEdit->setPlaceholderText(tr("6 digit code or backup code"));
    layout->addWidget(makeFieldLabel(tr("AUTHENTICATION CODE"), page));
    layout->addWidget(codeEdit);

    smsButton = new QPushButton(tr("Send code"), page);
    smsButton->setObjectName(QStringLiteral("LoginSecondary"));
    smsButton->setVisible(false);
    layout->addWidget(smsButton, 0, Qt::AlignHCenter);

    mfaStatus = makeStatus(page);
    layout->addWidget(mfaStatus);

    verifyButton = new QPushButton(tr("Submit"), page);
    verifyButton->setObjectName(QStringLiteral("LoginPrimary"));
    verifyButton->setDefault(true);
    layout->addWidget(verifyButton);

    backButton = makeLinkButton(tr("Go back"), page);
    layout->addWidget(backButton, 0, Qt::AlignHCenter);

    connect(verifyButton, &QPushButton::clicked, this, &LoginView::onVerifyClicked);
    connect(codeEdit, &QLineEdit::returnPressed, this, &LoginView::onVerifyClicked);
    connect(smsButton, &QPushButton::clicked, this, &LoginView::onSmsClicked);
    connect(backButton, &QPushButton::clicked, this, &LoginView::onBackClicked);
    connect(mfaMethodCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &LoginView::onMfaMethodChanged);

    return page;
}

void LoginView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);

    if (stack->currentWidget() == qrPage)
        startQrLogin();
    else
        loginEdit->setFocus();
}

void LoginView::showQrPage()
{
    stack->setCurrentWidget(qrPage);
    qrStatus->hide();
    startQrLogin();
}

void LoginView::showPasswordPage()
{
    // Discord spends a code as soon as it is answered or left, so the next visit
    // to the QR page gets a fresh client instead of a restarted one
    if (remoteClient) {
        remoteClient->stop();
        remoteStarted = false;
    }
    remoteBroken = true;

    stack->setCurrentWidget(formPage);
    formStatus->hide();
    loginEdit->setFocus();
}

void LoginView::startQrLogin()
{
    ensureRemoteClient(remoteProxy);

    if (remoteStarted)
        return;

    remoteStarted = true;
    qrImage->clear();
    setStatus(qrStatus, tr("Generating QR code..."), false);
    remoteClient->start();
}

void LoginView::ensureRemoteClient(const Core::ProxyConfig &wanted)
{
    // a polled session can't be restarted once Discord rejected or cancelled it,
    // so a broken client is thrown away instead of being reused
    if (remoteClient && remoteProxy == wanted && !remoteBroken)
        return;

    if (remoteClient) {
        remoteClient->stop();
        delete remoteClient;
    }

    remoteProxy = wanted;
    remoteStarted = false;
    remoteBroken = false;

    remoteClient = new Discord::RemoteAuthClient(remoteProxy, captchaResolver, this);
    connect(remoteClient, &Discord::RemoteAuthClient::fingerprintReady, this, &LoginView::renderQr);
    connect(remoteClient, &Discord::RemoteAuthClient::pendingTicket, this, &LoginView::onPendingTicket);
    connect(remoteClient, &Discord::RemoteAuthClient::authenticated, this, &LoginView::onRemoteAuthenticated);
    connect(remoteClient, &Discord::RemoteAuthClient::failed, this, &LoginView::onRemoteFailed);
    connect(remoteClient, &Discord::RemoteAuthClient::canceled, this, &LoginView::onRemoteCanceled);
}

// a new login client would lose the cookies and fingerprint from the previous attempt
void LoginView::ensureLoginClient(const Core::ProxyConfig &wanted)
{
    if (loginClient && proxy == wanted)
        return;

    delete loginClient;
    proxy = wanted;
    loginClient = new Discord::LoginClient(proxy, captchaResolver, this);
    connect(loginClient, &Discord::LoginClient::mfaRequired, this, &LoginView::onMfaRequired);
    connect(loginClient, &Discord::LoginClient::authenticated, this, &LoginView::onLoginAuthenticated);
    connect(loginClient, &Discord::LoginClient::failed, this, &LoginView::onFailed);
    connect(loginClient, &Discord::LoginClient::smsSent, this, &LoginView::onSmsSent);
}

void LoginView::onQrProxyClicked()
{
    ProxyInputDialog dlg(tr("QR Login"),
                         tr("Proxy for this login (optional). Leave empty to connect directly."),
                         this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    remoteBroken = true;
    ensureRemoteClient(dlg.getProxy());
    startQrLogin();
}

void LoginView::onLogInClicked()
{
    const QString login = loginEdit->text().trimmed();
    if (login.isEmpty() || passwordEdit->text().isEmpty()) {
        setStatus(formStatus, tr("Enter your email or phone number, and your password."));
        return;
    }

    std::optional<Core::ProxyConfig> parsed = proxyEdit->parseOrWarn();
    if (!parsed)
        return;

    ensureLoginClient(*parsed);

    formStatus->hide();
    setBusy(true);
    loginButton->setText(tr("Logging in..."));
    loginClient->login(login, passwordEdit->text());
}

void LoginView::onBackClicked()
{
    stack->setCurrentWidget(formPage);
    mfaStatus->hide();
    loginEdit->setFocus();
}

void LoginView::onMfaRequired(const Discord::MfaChallenge &challenge)
{
    setBusy(false);

    codeEdit->clear();
    mfaStatus->hide();

    if (challenge.sms && !challenge.totp && !challenge.backup)
        mfaTitle->setText(tr("Enter the code sent to your phone."));
    else
        mfaTitle->setText(tr("Two-factor authentication"));

    {
        QSignalBlocker blocker(mfaMethodCombo);
        mfaMethodCombo->clear();
        if (challenge.totp)
            mfaMethodCombo->addItem(tr("Authenticator app"), static_cast<int>(Discord::MfaMethod::Totp));
        if (challenge.backup)
            mfaMethodCombo->addItem(tr("Backup code"), static_cast<int>(Discord::MfaMethod::Backup));
        if (challenge.sms)
            mfaMethodCombo->addItem(tr("Text message"), static_cast<int>(Discord::MfaMethod::Sms));
    }
    mfaMethodCombo->setVisible(mfaMethodCombo->count() > 1);
    onMfaMethodChanged();

    stack->setCurrentWidget(mfaPage);
    codeEdit->setFocus();
}

void LoginView::onMfaMethodChanged()
{
    if (mfaMethodCombo->count() == 0)
        return;

    smsButton->setVisible(currentMfaMethod() == Discord::MfaMethod::Sms);
}

void LoginView::onSmsClicked()
{
    setBusy(true);
    setStatus(mfaStatus, tr("Requesting a code."), false);
    loginClient->requestSmsCode();
}

void LoginView::onSmsSent(bool ok)
{
    setBusy(false);
    if (ok)
        setStatus(mfaStatus, tr("Code sent to your phone. Enter it below; it expires within a few minutes."),
                  false);
    else
        setStatus(mfaStatus, tr("Failed to request a code, check your connection and try again."));
}

void LoginView::onVerifyClicked()
{
    const QString code = codeEdit->text().trimmed();
    if (code.isEmpty()) {
        setStatus(mfaStatus, tr("Enter your verification code."));
        return;
    }

    mfaStatus->hide();
    setBusy(true);
    verifyButton->setText(tr("Verifying..."));
    loginClient->submitMfa(currentMfaMethod(), code);
}

Discord::MfaMethod LoginView::currentMfaMethod() const
{
    return static_cast<Discord::MfaMethod>(mfaMethodCombo->currentData().toInt());
}

void LoginView::renderQr(const QString &fingerprint)
{
    const QString url = QStringLiteral("https://discord.com/ra/") + fingerprint;

    try {
        const qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(url.toUtf8().constData(),
                                                                   qrcodegen::QrCode::Ecc::QUARTILE);
        const int n = qr.getSize();
        const int border = 4;
        const int dim = n + border * 2;

        QImage image(dim, dim, QImage::Format_RGB32);
        image.fill(Qt::white);
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                if (qr.getModule(x, y))
                    image.setPixel(x + border, y + border, qRgb(0, 0, 0));

        qrImage->setPixmap(QPixmap::fromImage(
                image.scaled(QrSize, QrSize, Qt::KeepAspectRatio, Qt::FastTransformation)));
        setStatus(qrStatus, tr("Waiting for the scan..."), false);
    } catch (const std::exception &) {
        setStatus(qrStatus, tr("Failed to generate the QR code."));
    }
}

void LoginView::onPendingTicket(const QString &userId, const QString &name, const QString &avatarHash)
{
    pendingUsername = name;
    setStatus(qrStatus, tr("Logging in as %1...").arg(name), false);

    const QString hash = (avatarHash == QLatin1String("0")) ? QString() : avatarHash;
    const Core::Snowflake pendingAccount(userId.toULongLong());
    const QUrl url = Discord::Cdn::userAvatar(pendingAccount, hash, QrSize);
    if (!url.isEmpty()) {
        session->getImageManager()->setAccountProxy(pendingAccount, remoteProxy);
        session->getImageManager()->assign(qrImage, url, QSize(QrSize, QrSize), pendingAccount);
    }
}

void LoginView::onRemoteAuthenticated(const QString &token)
{
    storeToken(token);
}

void LoginView::onRemoteFailed(Discord::RemoteAuthError error)
{
    remoteStarted = false;
    remoteBroken = true;
    qrImage->clear();
    qrImage->setText(tr("QR unavailable"));

    QString message;
    switch (error) {
    case Discord::RemoteAuthError::KeyGenerationFailed:
        message = tr("Couldn't start a secure session. Please try again.");
        break;
    case Discord::RemoteAuthError::ConnectionFailed:
        message = tr("Couldn't reach Discord. Check your connection and try again.");
        break;
    case Discord::RemoteAuthError::HandshakeFailed:
        message = tr("The secure handshake with Discord failed. Please try again.");
        break;
    case Discord::RemoteAuthError::TimedOut:
        message = tr("The login request timed out. Switch pages to get a new code.");
        break;
    case Discord::RemoteAuthError::CaptchaRequired:
        message = tr("Discord is requiring a captcha. Log in with email and password instead.");
        break;
    case Discord::RemoteAuthError::LoginFailed:
        message = tr("Login failed. Switch pages to get a new code.");
        break;
    }
    setStatus(qrStatus, message);
}

void LoginView::onRemoteCanceled()
{
    remoteStarted = false;
    remoteBroken = true;
    setStatus(qrStatus, tr("Login was canceled on your phone. Switch pages to get a new code."), false);
}

void LoginView::onLoginAuthenticated(const QString &token)
{
    pendingUsername.clear();
    storeToken(token);
}

void LoginView::onFailed(Discord::LoginError error)
{
    setBusy(false);

    QString message;
    switch (error) {
    case Discord::LoginError::Network:
        message = tr("Couldn't reach Discord. Check your connection and try again.");
        break;
    case Discord::LoginError::InvalidCredentials:
        message = tr("The email or password you entered is incorrect.");
        break;
    case Discord::LoginError::CaptchaRequired:
        message = tr("Discord asked for a captcha but it wasn't completed. Try again.");
        break;
    case Discord::LoginError::InvalidMfaCode:
        message = tr("That code didn't work. Double-check it and try again.");
        break;
    case Discord::LoginError::TooManyRequests:
        message = tr("Too many login attempts. Wait a bit and try again.");
        break;
    case Discord::LoginError::AccountLocked:
        message = tr("This account is disabled. Enable it on discord.com and try again.");
        break;
    case Discord::LoginError::Suspended:
        message = tr("This account has been suspended.");
        break;
    case Discord::LoginError::RequiresVerification:
        message = tr("Discord wants to verify your login. Check your email or messages, follow the "
                     "link, and try logging in again.");
        break;
    case Discord::LoginError::PasskeyOnly:
        message = tr("This account only allows passkeys or security keys for two-factor "
                     "authentication, which aren't supported here. Log in with a QR code instead.");
        break;
    case Discord::LoginError::Unknown:
        message = tr("Login failed. Please try again.");
        break;
    }

    if (stack->currentWidget() == mfaPage && loginClient && loginClient->canRetryMfa()) {
        setStatus(mfaStatus, message);
        if (error == Discord::LoginError::InvalidMfaCode) {
            codeEdit->setFocus();
            codeEdit->selectAll();
        }
        return;
    }

    stack->setCurrentWidget(formPage);
    setStatus(formStatus, message);
    if (error == Discord::LoginError::InvalidCredentials) {
        passwordEdit->setFocus();
        passwordEdit->selectAll();
    }
}

void LoginView::storeToken(const QString &token)
{
    QLabel *status = statusForCurrentPage();
    setBusy(false);

    // the polled code is spent once Discord answers, so stop listening for it
    if (remoteClient) {
        remoteClient->stop();
        remoteStarted = false;
    }
    remoteBroken = true;

    const Core::Snowflake userId = Core::TokenUtils::getIdAndCheckToken(token);
    if (!userId.isValid()) {
        setStatus(status, tr("Discord returned a token this client couldn't read. Please try again."));
        return;
    }

    Core::AccountInfo acc;
    acc.id = userId;
    acc.token = token;
    acc.username = pendingUsername.isEmpty() ? QStringLiteral("unknown") : pendingUsername;
    acc.displayName = acc.username;
    acc.proxy = stack->currentWidget() == qrPage ? remoteProxy : proxy;

    Core::Result<void> added = model->addAccount(acc);
    if (!added.success()) {
        setStatus(status, tr("The account was logged in, but its token couldn't be saved.") + "\n" + added.error);
        return;
    }

    setBusy(false);
    emit loggedIn(userId);
}

QLabel *LoginView::statusForCurrentPage() const
{
    if (stack->currentWidget() == qrPage)
        return qrStatus;
    if (stack->currentWidget() == mfaPage)
        return mfaStatus;
    return formStatus;
}

void LoginView::setStatus(QLabel *label, const QString &text, bool error)
{
    if (error) {
        const QColor errorColor = Core::Theme::Manager::instance().color(Core::Theme::Token::ChatError);
        label->setStyleSheet(QStringLiteral("color: %1;").arg(errorColor.name()));
    } else {
        label->setStyleSheet(QString());
    }

    label->setText(text);
    label->show();
}

void LoginView::setBusy(bool busy)
{
    if (!busy) {
        loginButton->setText(tr("Log In"));
        verifyButton->setText(tr("Submit"));
    }

    loginEdit->setEnabled(!busy);
    passwordEdit->setEnabled(!busy);
    proxyEdit->setEnabled(!busy);
    loginButton->setEnabled(!busy);

    mfaMethodCombo->setEnabled(!busy);
    codeEdit->setEnabled(!busy);
    smsButton->setEnabled(!busy);
    backButton->setEnabled(!busy);
    verifyButton->setEnabled(!busy);
}

} // namespace UI
} // namespace Acheron

#include "SettingsWindow.hpp"

#include "AppearancePage.hpp"
#include "GeneralPage.hpp"

#ifdef ACHERON_HAVE_MINIAUDIO
#include "AudioPage.hpp"
#endif

#include "Core/AccountInfo.hpp"
#include "UI/Accounts/AccountsModel.hpp"

namespace Acheron {
namespace UI {

SettingsWindow::SettingsWindow(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("SettingsShell"));
    setAttribute(Qt::WA_StyledBackground, true);

    setupUi();
}

void SettingsWindow::setupUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("SettingsHeader"));
    header->setAttribute(Qt::WA_StyledBackground, true);
    header->setFixedHeight(52);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(20, 0, 12, 0);
    headerLayout->setSpacing(8);

    auto *title = new QLabel(tr("User Settings"), header);
    title->setObjectName(QStringLiteral("SettingsTitle"));
    headerLayout->addWidget(title, 0);
    headerLayout->addStretch(1);

    auto *closeButton = new QPushButton(QStringLiteral("\u2715"), header);
    closeButton->setObjectName(QStringLiteral("SettingsClose"));
    closeButton->setToolTip(tr("Esc"));
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setFixedSize(36, 36);
    headerLayout->addWidget(closeButton, 0);
    connect(closeButton, &QPushButton::clicked, this, &SettingsWindow::closeRequested);

    root->addWidget(header, 0);

    auto *body = new QWidget(this);
    auto *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    categoryList = new QListWidget(body);
    categoryList->setObjectName(QStringLiteral("SettingsNav"));
    categoryList->setFrameShape(QFrame::NoFrame);
    categoryList->setFixedWidth(216);
    categoryList->setFocusPolicy(Qt::NoFocus);

    pages = new QStackedWidget(body);
    pages->setObjectName(QStringLiteral("SettingsPages"));

    auto addPage = [this](const QString &name, QWidget *page) {
        categoryList->addItem(name);
        pages->addWidget(page);
    };

    addPage(tr("My Account"), buildAccountsPage());

    auto *appearance = new AppearancePage(this);
    addPage(tr("Appearance"), appearance);
    connect(appearance, &AppearancePage::channelListModeChanged, this,
            &SettingsWindow::channelListModeChanged);
    connect(appearance, &AppearancePage::channelListIndentChanged, this,
            &SettingsWindow::channelListIndentChanged);

#ifdef ACHERON_HAVE_MINIAUDIO
    voicePage = new AudioPage(this);
    addPage(tr("Voice & Video"), voicePage);
    auto *audio = static_cast<AudioPage *>(voicePage);
    connect(audio, &AudioPage::voiceSettingsChanged, this, &SettingsWindow::voiceSettingsChanged);
    if (voiceManager)
        audio->setVoiceManager(voiceManager);
#endif

    auto *general = new GeneralPage(this);
    addPage(tr("Text & Images"), general);
    connect(general, &GeneralPage::closeToTrayChanged, this, &SettingsWindow::closeToTrayChanged);
    connect(general, &GeneralPage::animateEmojiChanged, this, &SettingsWindow::animateEmojiChanged);
    connect(general, &GeneralPage::animateStickersChanged, this,
            &SettingsWindow::animateStickersChanged);
    connect(general, &GeneralPage::gifSettingsChanged, this, &SettingsWindow::gifSettingsChanged);
    connect(general, &GeneralPage::animationCacheLimitChanged, this,
            &SettingsWindow::animationCacheLimitChanged);
    connect(general, &GeneralPage::animationSizeLimitChanged, this,
            &SettingsWindow::animationSizeLimitChanged);

    categoryList->setCurrentRow(0);

    connect(categoryList, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);
    connect(categoryList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (pages->widget(row) && pages->widget(row)->objectName() ==
                                        QStringLiteral("SettingsAccountsPage"))
            refreshAccounts();
    });

    bodyLayout->addWidget(categoryList, 0);
    bodyLayout->addWidget(pages, 1);
    root->addWidget(body, 1);
}

QWidget *SettingsWindow::buildAccountsPage()
{
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("SettingsAccountsPage"));

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(10);

    auto *heading = new QLabel(tr("Accounts"), page);
    heading->setObjectName(QStringLiteral("SettingsPageTitle"));
    layout->addWidget(heading, 0);

    auto *hint = new QLabel(tr("Signed in with the accounts below."), page);
    hint->setObjectName(QStringLiteral("SettingsPageHint"));
    layout->addWidget(hint, 0);

    accountsLayout = new QVBoxLayout();
    accountsLayout->setSpacing(8);
    layout->addLayout(accountsLayout, 0);

    auto *addButton = new QPushButton(tr("Add an Account"), page);
    addButton->setObjectName(QStringLiteral("SettingsPrimaryButton"));
    addButton->setCursor(Qt::PointingHandCursor);
    addButton->setMaximumWidth(220);
    layout->addWidget(addButton, 0);
    connect(addButton, &QPushButton::clicked, this, [this]() {
        emit addAccountRequested();
    });

    layout->addStretch(1);

    refreshAccounts();
    return page;
}

void SettingsWindow::setAccountsModel(AccountsModel *model)
{
    if (accountsModel == model)
        return;

    if (accountsModel)
        disconnect(accountsModel, nullptr, this, nullptr);

    accountsModel = model;

    if (accountsModel) {
        connect(accountsModel, &QAbstractItemModel::rowsInserted, this,
                &SettingsWindow::refreshAccounts);
        connect(accountsModel, &QAbstractItemModel::rowsRemoved, this,
                &SettingsWindow::refreshAccounts);
        connect(accountsModel, &QAbstractItemModel::modelReset, this,
                &SettingsWindow::refreshAccounts);
        connect(accountsModel, &QAbstractItemModel::dataChanged, this,
                &SettingsWindow::refreshAccounts);
    }

    refreshAccounts();
}

void SettingsWindow::setVoiceManager(Core::Audio::VoiceManager *manager)
{
    voiceManager = manager;

#ifdef ACHERON_HAVE_MINIAUDIO
    if (voicePage)
        static_cast<AudioPage *>(voicePage)->setVoiceManager(voiceManager);
#endif
}

void SettingsWindow::refreshAccounts()
{
    if (!accountsLayout)
        return;

    while (QLayoutItem *item = accountsLayout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }

    if (!accountsModel)
        return;

    for (int row = 0; row < accountsModel->rowCount(); ++row) {
        const QModelIndex idx = accountsModel->index(row, 0);
        auto *info =
                static_cast<Core::AccountInfo *>(idx.data(AccountsModel::AccountObjectRole)
                                                         .value<void *>());
        if (!info)
            continue;

        auto *card = new QWidget(this);
        card->setObjectName(QStringLiteral("SettingsAccountCard"));
        card->setAttribute(Qt::WA_StyledBackground, true);

        auto *cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(12, 10, 12, 10);
        cardLayout->setSpacing(10);

        auto *name = new QLabel(info->getEntryString(), card);
        name->setObjectName(QStringLiteral("SettingsAccountName"));
        cardLayout->addWidget(name, 1);

        auto *logout = new QPushButton(tr("Log Out"), card);
        logout->setObjectName(QStringLiteral("SettingsDangerButton"));
        logout->setCursor(Qt::PointingHandCursor);
        const Core::Snowflake accountId = info->id;
        connect(logout, &QPushButton::clicked, this, [this, accountId]() {
            emit logOutRequested(accountId);
        });
        cardLayout->addWidget(logout, 0);

        accountsLayout->addWidget(card, 0);
    }
}

void SettingsWindow::openPage(const QString &name)
{
    const QList<QListWidgetItem *> matches =
            categoryList->findItems(name, Qt::MatchFixedString | Qt::MatchCaseSensitive);
    if (!matches.isEmpty())
        categoryList->setCurrentItem(matches.first());
}

void SettingsWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        emit closeRequested();
        return;
    }

    QWidget::keyPressEvent(event);
}

void SettingsWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refreshAccounts();
}

} // namespace UI
} // namespace Acheron

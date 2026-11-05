#include "CommandBar.hpp"

#include "Core/Theme/Manager.hpp"
#include "Core/Theme/Tokens.hpp"

#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>
#include <QVBoxLayout>

namespace Acheron {
namespace UI {

namespace {

constexpr int BarWidth = 480;

QFont consoleFont()
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSizeF(11.0);
    return font;
}

} // namespace

CommandBar::CommandBar(QWidget *parent) : QWidget(parent)
{
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setObjectName(QStringLiteral("CommandBar"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(BarWidth);
    setFocusPolicy(Qt::StrongFocus);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 10);
    root->setSpacing(6);

    auto *titleRow = new QHBoxLayout();
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(6);

    titleLabel = new QLabel(this);
    titleLabel->setObjectName(QStringLiteral("CommandBarTitle"));
    titleRow->addWidget(titleLabel, 0);
    titleRow->addStretch(1);

    hintLabel = new QLabel(this);
    hintLabel->setObjectName(QStringLiteral("CommandBarHint"));
    hintLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    titleRow->addWidget(hintLabel, 0);
    root->addLayout(titleRow, 0);

    auto *inputRow = new QHBoxLayout();
    inputRow->setContentsMargins(0, 0, 0, 0);
    inputRow->setSpacing(6);

    auto *caret = new QLabel(QStringLiteral(">"), this);
    caret->setObjectName(QStringLiteral("CommandBarPrompt"));
    inputRow->addWidget(caret, 0);

    input = new QLineEdit(this);
    input->setObjectName(QStringLiteral("CommandBarInput"));
    input->setFrame(false);
    input->installEventFilter(this);
    connect(input, &QLineEdit::returnPressed, this, &CommandBar::submit);
    inputRow->addWidget(input, 1);
    root->addLayout(inputRow, 0);

    resultLabel = new QLabel(this);
    resultLabel->setObjectName(QStringLiteral("CommandBarResult"));
    resultLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    resultLabel->hide();
    root->addWidget(resultLabel, 0);

    closeTimer = new QTimer(this);
    closeTimer->setSingleShot(true);
    closeTimer->setInterval(900);
    connect(closeTimer, &QTimer::timeout, this, &QWidget::close);

    const QFont mono = consoleFont();
    titleLabel->setFont(mono);
    caret->setFont(mono);
    input->setFont(mono);
}

void CommandBar::open(const QString &title, const QString &hint, const QString &placeholder,
                      const QString &currentValue, const Handler &apply)
{
    titleLabel->setText(title);
    hintLabel->setText(hint);
    input->setPlaceholderText(placeholder);
    input->setText(currentValue);
    handler = apply;
    resultLabel->hide();
    resultLabel->setText(QString());
    closeTimer->stop();

    if (QWidget *anchor = parentWidget() ? parentWidget()->window() : nullptr) {
        const QRect area = anchor->geometry();
        move(area.center().x() - BarWidth / 2, area.top() + qMax(60, area.height() / 5));
    }

    show();
    raise();

    input->setFocus();
    input->selectAll();
}

void CommandBar::submit()
{
    if (!handler)
        return;

    const Outcome outcome = handler(input->text().trimmed());
    if (outcome.accepted) {
        showResult(outcome.message, false);
        closeTimer->start();
        return;
    }

    showResult(outcome.message, true);
    input->setFocus();
    input->selectAll();
}

void CommandBar::showResult(const QString &text, bool error)
{
    const Core::Theme::Token token =
            error ? Core::Theme::Token::ChatError : Core::Theme::Token::StatusOnline;
    const QColor color = Core::Theme::Manager::instance().color(token);

    resultLabel->setText(text);
    resultLabel->setStyleSheet(QStringLiteral("color: %1;").arg(color.name()));
    resultLabel->show();
}

bool CommandBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == input && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) {
            close();
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}

} // namespace UI
} // namespace Acheron

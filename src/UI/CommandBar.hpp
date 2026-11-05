#pragma once

#include <QWidget>

#include <functional>

class QLabel;
class QLineEdit;
class QTimer;

namespace Acheron {
namespace UI {

// The strip you type a number into: F2 asks for the microphone gain, F3 for the
// bitrate. Enter applies and keeps the result visible for a moment, Escape bails.
class CommandBar : public QWidget
{
    Q_OBJECT
public:
    struct Outcome
    {
        bool accepted = false;
        QString message;
    };

    using Handler = std::function<Outcome(const QString &)>;

    explicit CommandBar(QWidget *parent = nullptr);

    void open(const QString &title, const QString &hint, const QString &placeholder,
              const QString &currentValue, const Handler &handler);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void submit();
    void showResult(const QString &text, bool error);

    QLabel *titleLabel = nullptr;
    QLabel *hintLabel = nullptr;
    QLabel *resultLabel = nullptr;
    QLineEdit *input = nullptr;
    QTimer *closeTimer = nullptr;
    Handler handler;
};

} // namespace UI
} // namespace Acheron

#include "VolumeMeter.hpp"

#include "Core/Theme/Manager.hpp"
#include "Core/Theme/Tokens.hpp"

#include <QLinearGradient>
#include <QPainter>

namespace Acheron {
namespace UI {

VolumeMeter::VolumeMeter(QWidget *parent) : QWidget(parent)
{
    setFixedHeight(12);
}

void VolumeMeter::setLevel(float rms)
{
    level = rms;
    update();
}

void VolumeMeter::setThreshold(float t)
{
    threshold = t;
    update();
}

void VolumeMeter::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    p.fillRect(rect(), palette().color(QPalette::Base));

    const float fraction = qBound(0.0f, level / MAX_RMS, 1.0f);
    const int barWidth = static_cast<int>(fraction * width());

    if (barWidth > 0) {
        Core::Theme::Manager &theme = Core::Theme::Manager::instance();
        QLinearGradient gradient(0, 0, width(), 0);
        gradient.setColorAt(0.0, theme.color(Core::Theme::Token::StatusOnline));
        gradient.setColorAt(0.5, theme.color(Core::Theme::Token::StatusIdle));
        gradient.setColorAt(1.0, theme.color(Core::Theme::Token::StatusDnd));

        p.fillRect(0, 0, barWidth, height(), gradient);
    }

    if (threshold >= 0.0f) {
        const float threshFraction = qBound(0.0f, threshold / MAX_RMS, 1.0f);
        const int threshX = static_cast<int>(threshFraction * width());
        p.setPen(QPen(palette().color(QPalette::WindowText), 1));
        p.drawLine(threshX, 0, threshX, height());
    }
}

} // namespace UI
} // namespace Acheron

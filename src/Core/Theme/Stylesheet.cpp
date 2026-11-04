#include "Core/Theme/Stylesheet.hpp"

#include "Core/Theme/Fonts.hpp"
#include "Core/Theme/Manager.hpp"
#include "Core/Theme/Tokens.hpp"

#include <QColor>

namespace Acheron {
namespace Core {
namespace Theme {

namespace {
QString hex(const QColor &c)
{
    return c.name(QColor::HexRgb);
}

QString rgba(const QColor &c, int alpha)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha);
}

QString px(int value)
{
    return QStringLiteral("%1px").arg(value);
}

int innerRadius(int radius, int inset)
{
    return qMax(0, radius - inset);
}
} // namespace

QString buildStyleSheet()
{
    const Manager &m = Manager::instance();

    const QColor baseBg = m.color(Token::BaseBg);
    const QColor buttonBg = m.color(Token::ButtonBg);
    const QColor primaryText = m.color(Token::PrimaryText);
    const QColor buttonText = m.color(Token::ButtonText);
    const QColor tooltipBg = m.color(Token::TooltipBg);
    const QColor tooltipText = m.color(Token::TooltipText);
    const QColor divider = m.color(Token::Divider);
    const QColor highlight = m.color(Token::Highlight);
    const QColor highlightedText = m.color(Token::HighlightedText);
    const QColor disabledText = m.color(Token::DisabledText);
    const QColor windowBg = m.color(Token::WindowBg);
    const QColor alternateBaseBg = m.color(Token::AlternateBaseBg);
    const QColor brightText = m.color(Token::BrightText);
    const QColor linkText = m.color(Token::LinkText);
    const QColor placeholderText = m.color(Token::PlaceholderText);

    const int radius = Metrics::cornerRadius();
    const QString radiusPx = px(radius);
    const QString innerRadiusPx = px(innerRadius(radius, 2));
    const int indicatorSize = Metrics::spacing(14);

    QString qss;

    constexpr qreal tooltipFontScale = 0.9;
    QString tooltipFontSize;
    const qreal uiPointSize = m.font(FontRole::Ui).pointSizeF();
    if (uiPointSize > 0)
        tooltipFontSize = QStringLiteral("  font-size: %1pt;").arg(uiPointSize * tooltipFontScale);

    qss += QStringLiteral("QToolTip {"
                          "  background-color: %1;"
                          "  color: %2;"
                          "  border: 1px solid %3;"
                          "  padding: %4 %5;"
                          "  border-radius: %6;"
                          "%7"
                          "}")
                   .arg(hex(tooltipBg), hex(tooltipText), hex(divider), px(Metrics::spacing(2)),
                        px(Metrics::spacing(6)), px(innerRadius(radius, 1)), tooltipFontSize);

    qss += "#MemberList QScrollBar::handle:vertical { min-height: 40px; }";

    // Discord shades each pane differently, which a single palette role can't
    // express, so the panes name their own backgrounds here.
    qss += QStringLiteral("QListView#ServerRail { background-color: %1; }"
                          "QWidget#GuildSidebar { background-color: %2; }"
                          "QWidget#ChatPane { background-color: %3; }")
                   .arg(hex(windowBg), hex(alternateBaseBg), hex(baseBg));

    qss += QStringLiteral("QTreeView#ChannelTree { background-color: %1; border: none; }"
                          "QListView#MemberList { background-color: %1; border: none; }"
                          "QLabel#GuildHeader { background-color: %1; border-bottom: 1px solid %2; }"
                          "QWidget#channelToolbar { background-color: %3; border-bottom: 1px solid %2; }")
                   .arg(hex(alternateBaseBg), hex(divider), hex(baseBg));

    qss += QStringLiteral("#MessageInput {"
                          "  background-color: %1;"
                          "  border: none;"
                          "  border-radius: %2;"
                          "  padding: %3 %4; }")
                   .arg(hex(buttonBg), px(qMax(4, radius + 2)), px(Metrics::spacing(6)),
                        px(Metrics::spacing(10)));

    qss += QStringLiteral("#MessageInput QToolButton {"
                          "  background: transparent;"
                          "  border: none;"
                          "  border-radius: %1; }"
                          "#MessageInput QToolButton:hover { background-color: %2; }"
                          "#MessageInput QToolButton:pressed { background-color: %3; }")
                   .arg(innerRadiusPx, rgba(primaryText, 24), rgba(primaryText, 40));

    qss += QStringLiteral("#EmojiAutocompletePopup QListView {"
                          "  background: transparent;"
                          "  border: none; }");

    qss += QStringLiteral("#MessageActionBar {"
                          "  background-color: %1;"
                          "  border: 1px solid %2;"
                          "  border-radius: %3;"
                          "  padding: %4; }"
                          "#MessageActionBar QToolButton {"
                          "  background: transparent;"
                          "  border: none;"
                          "  border-radius: %5; }"
                          "#MessageActionBar QToolButton:hover { background-color: %6; }"
                          "#MessageActionBar QToolButton:pressed { background-color: %7; }")
                   .arg(hex(buttonBg), hex(divider), radiusPx, px(Metrics::spacing(2)), innerRadiusPx,
                        rgba(primaryText, 24), rgba(primaryText, 40));

    // QString::arg only reads one digit per marker, so no block may go past %9.
    qss += QStringLiteral("QPushButton {"
                          "  background-color: %1;"
                          "  color: %2;"
                          "  border: 1px solid %3;"
                          "  border-radius: %4;"
                          "  padding: %5 %6;"
                          "  outline: none; }"
                          "QPushButton:hover { background-color: %7; border-color: %8; }"
                          "QPushButton:pressed { background-color: %9; }")
                   .arg(hex(buttonBg), hex(buttonText), hex(divider), radiusPx,
                        px(Metrics::spacing(4)), px(Metrics::spacing(10)),
                        buttonBg.lighter(118).name(QColor::HexRgb), hex(highlight),
                        buttonBg.darker(112).name(QColor::HexRgb));

    qss += QStringLiteral("QPushButton:checked { background-color: %1; color: %2; border-color: %1; }"
                          "QPushButton:default { border-color: %1; }"
                          "QPushButton:disabled { color: %3; }")
                   .arg(hex(highlight), hex(highlightedText), hex(disabledText));

    qss += QStringLiteral("QMenu {"
                          "  background-color: %1;"
                          "  color: %2;"
                          "  border: 1px solid %3;"
                          "  border-radius: %4;"
                          "  padding: %5; }"
                          "QMenu::item {"
                          "  background: transparent;"
                          "  border-radius: %6;"
                          "  padding: %7 %8 %7 %9; }")
                   .arg(hex(buttonBg), hex(buttonText), hex(divider), radiusPx, px(Metrics::spacing(4)),
                        innerRadiusPx, px(Metrics::spacing(5)), px(Metrics::spacing(20)),
                        px(Metrics::spacing(10)));

    qss += QStringLiteral("QMenu::item:selected { background-color: %1; color: %2; }"
                          "QMenu::item:disabled { color: %3; }"
                          "QMenu::separator {"
                          "  height: 1px;"
                          "  background-color: %4;"
                          "  margin: %5 %6; }"
                          "QMenu::indicator { width: %7; height: %7; }")
                   .arg(hex(highlight), hex(highlightedText), hex(disabledText), hex(divider),
                        px(Metrics::spacing(4)), px(Metrics::spacing(10)), px(indicatorSize));

    qss += QStringLiteral("QMenuBar {"
                          "  background-color: %1;"
                          "  color: %2;"
                          "  border-bottom: 1px solid %3; }"
                          "QMenuBar::item {"
                          "  background: transparent;"
                          "  border-radius: %4;"
                          "  padding: %5 %6; }"
                          "QMenuBar::item:selected { background-color: %7; }")
                   .arg(hex(windowBg), hex(buttonText), hex(divider), innerRadiusPx,
                        px(Metrics::spacing(4)), px(Metrics::spacing(8)), rgba(primaryText, 24));

    qss += QStringLiteral("QLineEdit, QTextEdit, QPlainTextEdit, QSpinBox, QDoubleSpinBox {"
                          "  background-color: %1;"
                          "  color: %2;"
                          "  border: 1px solid %3;"
                          "  border-radius: %4;"
                          "  padding: %5 %6;"
                          "  selection-background-color: %7;"
                          "  selection-color: %8; }"
                          "QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus,"
                          "QSpinBox:focus, QDoubleSpinBox:focus { border: 1px solid %7; }")
                   .arg(hex(baseBg), hex(primaryText), hex(divider), radiusPx, px(Metrics::spacing(4)),
                        px(Metrics::spacing(8)), hex(highlight), hex(highlightedText));

    qss += QStringLiteral("QComboBox {"
                          "  background-color: %1;"
                          "  color: %2;"
                          "  border: 1px solid %3;"
                          "  border-radius: %4;"
                          "  padding: %5 %6; }"
                          "QComboBox::drop-down { border: none; width: %7; }"
                          "QComboBox QAbstractItemView { background-color: %8; color: %9; }")
                   .arg(hex(buttonBg), hex(primaryText), hex(divider), radiusPx,
                        px(Metrics::spacing(4)), px(Metrics::spacing(8)), px(Metrics::spacing(18)),
                        hex(buttonBg), hex(primaryText));

    qss += QStringLiteral("QCheckBox::indicator, QRadioButton::indicator {"
                          "  width: %1;"
                          "  height: %1;"
                          "  border: 1px solid %2;"
                          "  background-color: %3; }"
                          "QCheckBox::indicator { border-radius: %4; }"
                          "QRadioButton::indicator { border-radius: %5; }"
                          "QCheckBox::indicator:checked, QRadioButton::indicator:checked {"
                          "  background-color: %6; border-color: %6; }"
                          "QCheckBox::indicator:disabled, QRadioButton::indicator:disabled {"
                          "  background-color: %3; border-color: %3; }")
                   .arg(px(indicatorSize), hex(divider), hex(baseBg),
                        px(qMax(2, innerRadius(radius, 4))), px(indicatorSize / 2), hex(highlight));

    // The sign-in card copies Discord's own layout: a shaded card, small
    // uppercase field labels, and one blurple action.
    qss += QStringLiteral("QWidget#LoginView { background-color: %1; }"
                          "QWidget#LoginCard { background-color: %2; border-radius: %3; }"
                          "QStackedWidget#LoginStack { background: transparent; }")
                   .arg(hex(windowBg), hex(baseBg), px(radius + 4));

    qss += QStringLiteral("QLabel#LoginTitle { font-size: 22px; font-weight: 700; color: %1; }"
                          "QLabel#LoginSubtitle { font-size: 14px; color: %2; }"
                          "QLabel#LoginFieldLabel { font-size: 11px; font-weight: 700; color: %3; }"
                          "QLabel#LoginStatus { font-size: 13px; color: %4; }")
                   .arg(hex(brightText), hex(placeholderText), hex(placeholderText),
                        hex(placeholderText));

    qss += QStringLiteral("QLineEdit#LoginInput, QComboBox#LoginInput {"
                          "  background-color: %1;"
                          "  color: %2;"
                          "  border: 1px solid %3;"
                          "  border-radius: %4;"
                          "  padding: %5 %6;"
                          "  font-size: 16px; }"
                          "QLineEdit#LoginInput:focus, QComboBox#LoginInput:focus {"
                          "  border: 1px solid %7; }")
                   .arg(hex(windowBg), hex(primaryText), hex(divider), radiusPx,
                        px(Metrics::spacing(8)), px(Metrics::spacing(12)), hex(highlight));

    qss += QStringLiteral("QPushButton#LoginPrimary {"
                          "  background-color: %1;"
                          "  color: %2;"
                          "  border: none;"
                          "  border-radius: %3;"
                          "  padding: %4 %5;"
                          "  font-size: 16px;"
                          "  font-weight: 600; }"
                          "QPushButton#LoginPrimary:hover { background-color: %6; }"
                          "QPushButton#LoginPrimary:pressed { background-color: %7; }"
                          "QPushButton#LoginPrimary:disabled { background-color: %8; color: %9; }")
                   .arg(hex(highlight), hex(highlightedText), radiusPx, px(Metrics::spacing(10)),
                        px(Metrics::spacing(16)), highlight.darker(115).name(QColor::HexRgb),
                        highlight.darker(125).name(QColor::HexRgb), hex(buttonBg),
                        hex(disabledText));

    qss += QStringLiteral("QPushButton#LoginSecondary {"
                          "  background-color: %1;"
                          "  color: %2;"
                          "  border: none;"
                          "  border-radius: %3;"
                          "  padding: %4 %5; }"
                          "QPushButton#LoginSecondary:hover { background-color: %6; }"
                          "QPushButton#LoginLink { background: transparent; border: none;"
                          "  color: %7; font-size: 13px; }"
                          "QPushButton#LoginLink:hover { text-decoration: underline; }"
                          "QPushButton#LoginLink:disabled { color: %8; }"
                          "QLabel#LoginQr { background-color: white; border-radius: %9; }")
                   .arg(hex(buttonBg), hex(buttonText), radiusPx, px(Metrics::spacing(8)),
                        px(Metrics::spacing(14)), buttonBg.lighter(118).name(QColor::HexRgb),
                        hex(linkText), hex(disabledText), radiusPx);

    qss += QStringLiteral("QFrame#LoginDivider { color: %1; background-color: %2; }")
                   .arg(hex(divider), hex(divider));

    return qss;
}

} // namespace Theme
} // namespace Core
} // namespace Acheron

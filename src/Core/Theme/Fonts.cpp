#include "Fonts.hpp"

#include <QApplication>
#include <QFontDatabase>

namespace Acheron {
namespace Core {
namespace Theme {

namespace {

// Discord's interface runs a step above the platform default, so the roles
// carry their own sizes instead of inheriting the application font.
QFont sized(QFont font, int points)
{
    font.setPointSize(points);
    return font;
}

} // namespace

const std::vector<FontDescriptor> &fontRegistry()
{
    static const std::vector<FontDescriptor> reg = {
        { FontRole::Ui, "font.ui", "Interface", sized(QApplication::font(), 11) },
        { FontRole::Message, "font.message", "Messages", sized(QApplication::font(), 12) },
        { FontRole::Code, "font.code", "Code",
          QFontDatabase::systemFont(QFontDatabase::FixedFont) },
    };
    return reg;
}

const FontDescriptor *findFontById(const QString &id)
{
    for (const FontDescriptor &d : fontRegistry()) {
        if (id == QLatin1String(d.id))
            return &d;
    }
    return nullptr;
}

const FontDescriptor &fontDescriptor(FontRole role)
{
    for (const FontDescriptor &d : fontRegistry()) {
        if (d.role == role)
            return d;
    }
    return fontRegistry().front(); // just in case
}

} // namespace Theme
} // namespace Core
} // namespace Acheron

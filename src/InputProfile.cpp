#include "InputProfile.h"
#include <QRegularExpression>

QString guidString(const GUID &guid)
{
    wchar_t buffer[40]{};
    StringFromGUID2(guid, buffer, 40);
    return QString::fromWCharArray(buffer).toUpper();
}

QString layoutString(HKL layout)
{
    return QString::number(reinterpret_cast<quintptr>(layout) & 0xffffffffu, 16).rightJustified(8, u'0').toUpper();
}

InputProfile InputProfile::parse(const QString &raw, const QString &name)
{
    InputProfile result;
    result.id = raw.trimmed().toUpper().replace(QStringLiteral("0X"), QString());
    result.name = name;
    static const QRegularExpression pattern(QStringLiteral("^([0-9A-F]{4}):([0-9A-F]{8}|\\{[0-9A-F-]{36}\\}\\{[0-9A-F-]{36}\\})$"));
    const auto match = pattern.match(result.id);
    if (!match.hasMatch()) { result.id.clear(); return result; }
    result.language = match.captured(1).toUShort(nullptr, 16);
    result.keyboard = match.captured(2).size() == 8;
    if (result.keyboard) {
        result.klid = match.captured(2);
    } else {
        const auto value = match.captured(2);
        if (FAILED(CLSIDFromString(reinterpret_cast<LPCOLESTR>(value.left(38).utf16()), &result.clsid))
            || FAILED(CLSIDFromString(reinterpret_cast<LPCOLESTR>(value.mid(38).utf16()), &result.profile))) {
            result.id.clear();
        }
    }
    wchar_t language[160]{};
    GetLocaleInfoW(MAKELCID(result.language, SORT_DEFAULT), LOCALE_SLOCALIZEDDISPLAYNAME, language, 160);
    result.languageName = QString::fromWCharArray(language);
    if (result.isUS()) result.name = QStringLiteral("美式键盘");
    return result;
}

bool InputProfile::isUS() const { return keyboard && language == 0x0409 && klid == QStringLiteral("00000409"); }
bool InputProfile::isChinese() const { return PRIMARYLANGID(language) == LANG_CHINESE; }

QVariantMap InputProfile::toVariant() const
{
    return {{"profileId", id}, {"displayName", name}, {"languageName", languageName},
            {"enabled", enabled}, {"systemDefault", systemDefault}, {"isUS", isUS()},
            {"isChinese", isChinese()}, {"layout", layoutString(layout)},
            {"kind", keyboard ? QStringLiteral("键盘布局") : QStringLiteral("输入法")}};
}

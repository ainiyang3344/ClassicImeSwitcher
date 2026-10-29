#include "AppController.h"
#include "StartupTask.h"
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUrl>
#include <algorithm>

AppController::AppController(QString stateDirectory, bool safeMode, QObject *parent)
    : QObject(parent), m_stateDirectory(std::move(stateDirectory)),
      m_settings(m_stateDirectory + QStringLiteral("/settings.ini"), QSettings::IniFormat),
      m_engine(this), m_safeMode(safeMode)
{
    QDir().mkpath(m_stateDirectory);
    m_chineseId = m_settings.value(QStringLiteral("switch/chineseId")).toString();
    m_shortcut = std::clamp(m_settings.value(QStringLiteral("switch/shortcut"), 0).toInt(), 0, 2);
    connect(&m_engine, &SwitchEngine::result, this, [this](bool ok, const QString &message) { announce(message, !ok); updateCurrentInput(); }, Qt::QueuedConnection);
    refresh();
    if (!m_noticeError) m_notice = ready() ? QStringLiteral("切换目标已就绪，可以开启经典切换。") : preparationHint();
    if (!m_safeMode) {
        QString error;
        m_engineStarted = m_engine.startEngine(&error);
        if (!m_engineStarted) announce(error, true);
        else if (m_settings.value(QStringLiteral("switch/enabled"), false).toBool()) setEnabled(true);
    }
    connect(&m_poll, &QTimer::timeout, this, &AppController::updateCurrentInput);
    m_poll.start(500);
    updateCurrentInput();
}

QVariantList AppController::enabledProfiles() const
{
    QVariantList result;
    for (const auto &profile : m_profiles) if (profile.enabled) result.append(profile.toVariant());
    return result;
}
QVariantList AppController::disabledProfiles() const
{
    QVariantList result;
    for (const auto &profile : m_profiles) if (!profile.enabled) result.append(profile.toVariant());
    return result;
}
QVariantList AppController::chineseChoices() const
{
    QVariantList result;
    for (const auto &profile : m_profiles) if (profile.enabled && profile.isChinese() && profile.layout) result.append(profile.toVariant());
    return result;
}
const InputProfile *AppController::find(const QString &id) const
{
    for (const auto &profile : m_profiles) if (profile.id == id) return &profile;
    return nullptr;
}
InputProfile AppController::selectedChinese() const { const auto *profile = find(m_chineseId); return profile ? *profile : InputProfile{}; }
bool AppController::hasUS() const
{
    for (const auto &profile : m_profiles) if (profile.isUS() && profile.enabled && profile.layout) return true;
    return false;
}
int AppController::enabledChineseCount() const
{
    return static_cast<int>(std::count_if(m_profiles.cbegin(), m_profiles.cend(), [](const InputProfile &profile) { return profile.enabled && profile.isChinese(); }));
}
bool AppController::ready() const
{
    const auto selected = selectedChinese();
    return hasUS() && enabledChineseCount() == 1 && selected.enabled && selected.isChinese() && selected.layout;
}
QString AppController::preparationHint() const
{
    if (!hasUS()) return QStringLiteral("请先添加美式键盘，再开启经典切换。");
    if (enabledChineseCount() > 1) return QStringLiteral("首版请只启用一个中文输入法。请在输入法管理页禁用其他中文输入法，再刷新列表。");
    return QStringLiteral("请选择已启用的中文输入法，再开启经典切换。");
}
QString AppController::shortcutText() const
{
    return QStringList{QStringLiteral("Ctrl + Space"), QStringLiteral("Ctrl + Shift + Space"), QStringLiteral("Alt + Space")}.at(m_shortcut);
}
void AppController::refresh()
{
    QString error;
    auto profiles = m_windows.profiles(&error);
    if (!error.isEmpty()) { announce(error, true); return; }
    m_profiles = std::move(profiles);
    const auto previous = selectedChinese();
    if (!previous.enabled || !previous.layout) {
        m_chineseId.clear();
        QList<InputProfile> candidates;
        for (const auto &profile : m_profiles) if (profile.enabled && profile.isChinese() && profile.layout) candidates.append(profile);
        if (candidates.size() == 1) m_chineseId = candidates.front().id;
        else for (const auto &profile : candidates) if (profile.systemDefault) { m_chineseId = profile.id; break; }
        m_settings.setValue(QStringLiteral("switch/chineseId"), m_chineseId);
    }
    if (m_enabled && !ready()) {
        m_enabled = false;
        m_settings.setValue(QStringLiteral("switch/enabled"), false);
        announce(QStringLiteral("输入法配置已变化，经典切换已暂停。请选择可用的切换目标。"), true);
    }
    updateEngine(); emit profilesChanged(); emit settingsChanged();
}
void AppController::setChineseId(const QString &id)
{
    const auto *profile = find(id);
    if (!profile || !profile->enabled || !profile->isChinese() || !profile->layout) return;
    m_chineseId = id; m_settings.setValue(QStringLiteral("switch/chineseId"), id);
    updateEngine(); emit settingsChanged(); emit profilesChanged();
}
void AppController::setEnabled(bool enabled)
{
    if (enabled && (m_safeMode || !m_engineStarted || !ready())) {
        announce(!ready() ? preparationHint() : QStringLiteral("键盘监听尚未就绪，无法开启经典切换。"), true);
        emit settingsChanged(); return;
    }
    m_enabled = enabled;
    m_settings.setValue(QStringLiteral("switch/enabled"), enabled);
    m_settings.sync(); updateEngine(); emit settingsChanged();
    announce(enabled ? QStringLiteral("经典切换已开启，按 %1 切换中文与美式键盘。").arg(shortcutText()) : QStringLiteral("经典切换已暂停。"));
}
void AppController::setShortcut(int shortcut)
{
    if (shortcut < 0 || shortcut > 2 || m_shortcut == shortcut) return;
    m_shortcut = shortcut; m_settings.setValue(QStringLiteral("switch/shortcut"), shortcut);
    updateEngine(); emit settingsChanged();
}
void AppController::updateEngine() { m_engine.configure(m_enabled && ready(), m_shortcut, selectedChinese(), enabledChineseCount()); }
bool AppController::autoStart() const
{
    return !m_safeMode && StartupTask::enabled();
}
void AppController::migrateAutoStart()
{
    if (m_safeMode) return;
    QSettings run(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"), QSettings::NativeFormat);
    if (run.contains(QStringLiteral("ClassicImeSwitcher"))) setAutoStart(true);
}
void AppController::setAutoStart(bool enabled)
{
    if (m_safeMode) return;
    QString error;
    if (!StartupTask::setEnabled(enabled, &error)) {
        announce(error, true);
    } else {
        // Preserve the old entry if registering the elevated task fails.
        QSettings run(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"), QSettings::NativeFormat);
        run.remove(QStringLiteral("ClassicImeSwitcher"));
        run.sync();
        if (run.status() != QSettings::NoError) announce(QStringLiteral("旧开机启动项清理失败，请重新设置开机启动。"), true);
        else announce(enabled ? QStringLiteral("已设置登录后以管理员权限启动。") : QStringLiteral("已关闭开机启动。"));
    }
    emit settingsChanged();
}
bool AppController::closeToTray() const { return m_settings.value(QStringLiteral("window/closeToTray"), true).toBool(); }
void AppController::setCloseToTray(bool enabled) { m_settings.setValue(QStringLiteral("window/closeToTray"), enabled); emit settingsChanged(); }
void AppController::announce(const QString &message, bool error)
{
    m_notice = message; m_noticeError = error; emit noticeChanged();
    QFile log(m_stateDirectory + QStringLiteral("/activity.log"));
    if (log.size() > 512 * 1024) { QFile::remove(log.fileName() + QStringLiteral(".old")); QFile::rename(log.fileName(), log.fileName() + QStringLiteral(".old")); }
    if (log.open(QIODevice::WriteOnly | QIODevice::Append)) log.write((QDateTime::currentDateTime().toString(Qt::ISODateWithMs) + (error ? QStringLiteral(" ERROR ") : QStringLiteral(" INFO ")) + message + u'\n').toUtf8());
}
bool AppController::backup()
{
    QJsonArray profiles;
    for (const auto &profile : m_profiles) profiles.append(QJsonObject::fromVariantMap(profile.toVariant()));
    const QString folder = m_stateDirectory + QStringLiteral("/backups");
    if (!QDir().mkpath(folder)) { announce(QStringLiteral("无法创建配置快照目录。"), true); return false; }
    QSaveFile file(folder + u'/' + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")) + QStringLiteral(".json"));
    if (!file.open(QIODevice::WriteOnly)) { announce(QStringLiteral("无法保存修改前的配置快照。"), true); return false; }
    file.write(QJsonDocument(QJsonObject{{"userSid", WindowsInput::userSid()}, {"profiles", profiles}}).toJson());
    if (!file.commit()) { announce(QStringLiteral("配置快照保存失败。"), true); return false; }
    return true;
}
void AppController::addUS()
{
    if (m_safeMode) return;
    refresh();
    if (hasUS()) { announce(QStringLiteral("美式键盘已经可用，无需重复添加。")); return; }
    if (!backup()) return;
    QString error;
    if (!m_windows.setEnabled(InputProfile::parse(QStringLiteral("0409:00000409")), true, &error)) { announce(error, true); return; }
    refresh();
    announce(hasUS() ? QStringLiteral("美式键盘已添加，原有输入法保留。") : QStringLiteral("Windows 尚未确认美式键盘，请刷新后检查。"), !hasUS());
}
void AppController::setProfileEnabled(const QString &id, bool enabled)
{
    if (m_safeMode) return;
    refresh(); const auto *found = find(id);
    if (!found || found->enabled == enabled) return;
    const InputProfile profile = *found;
    if (!enabled && (enabledProfiles().size() <= 1 || (m_enabled && (profile.isUS() || profile.id == m_chineseId)))) {
        announce(QStringLiteral("请保留至少一个可用输入法；禁用切换目标前，请先暂停经典切换。"), true); return;
    }
    if (!backup()) return;
    QString error;
    if (!m_windows.setEnabled(profile, enabled, &error)) { announce(error, true); return; }
    refresh(); const auto *updated = find(id);
    const bool confirmed = updated && updated->enabled == enabled;
    announce(confirmed ? QStringLiteral("%1已%2。").arg(profile.name, enabled ? QStringLiteral("启用") : QStringLiteral("禁用"))
                       : QStringLiteral("Windows 尚未确认变更，请刷新列表。"), !confirmed);
}
void AppController::setDefaultProfile(const QString &id)
{
    if (m_safeMode) return;
    const auto *profile = find(id);
    if (!profile || !profile->enabled || !backup()) return;
    QString error;
    if (!m_windows.setDefault(*profile, &error)) { announce(error, true); return; }
    refresh();
    const auto *updated = find(id);
    const bool confirmed = updated && updated->systemDefault;
    announce(confirmed ? QStringLiteral("已设置 Windows 默认输入法。") : QStringLiteral("Windows 尚未确认默认输入法变更，请刷新后检查。"), !confirmed);
}
void AppController::updateCurrentInput()
{
    const HWND window = GetForegroundWindow();
    const DWORD thread = GetWindowThreadProcessId(WindowsInput::focusedWindow(window), nullptr);
    const HKL layout = thread ? GetKeyboardLayout(thread) : nullptr;
    const LANGID language = LOWORD(reinterpret_cast<quintptr>(layout));
    const QString current = !layout ? QStringLiteral("暂无前台窗口") : layoutString(layout) == QStringLiteral("04090409")
        ? QStringLiteral("美式键盘") : PRIMARYLANGID(language) == LANG_CHINESE ? QStringLiteral("中文输入法") : QStringLiteral("其他输入布局");
    if (m_currentInput != current) { m_currentInput = current; emit currentInputChanged(); }
}
void AppController::openDataDirectory() { QDesktopServices::openUrl(QUrl::fromLocalFile(m_stateDirectory)); }
void AppController::openWindowsLanguageSettings() { QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:regionlanguage"))); }
void AppController::quit() { QCoreApplication::quit(); }

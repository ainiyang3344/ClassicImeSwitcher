#include "WindowsInput.h"
#include <msctf.h>
#include <sddl.h>
#include <wrl/client.h>
#include <QSet>
#include <QSettings>
#include <algorithm>
#include <vector>

using Microsoft::WRL::ComPtr;
bool OwnInputLanguageFilter::nativeEventFilter(const QByteArray &, void *message, qintptr *)
{
    const auto *event = static_cast<MSG *>(message);
    if (!m_synchronizing && event->message == WM_INPUTLANGCHANGE) {
        m_synchronizing = true;
        ComPtr<ITfInputProcessorProfiles> profiles;
        if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&profiles)))) {
            const LANGID language = LOWORD(static_cast<quintptr>(event->lParam));
            LANGID current = 0;
            if (SUCCEEDED(profiles->GetCurrentLanguage(&current)) && current != language) profiles->ChangeCurrentLanguage(language);
        }
        m_synchronizing = false;
    }
    return false;
}
namespace {
struct LayoutOrTip { DWORD flags; WCHAR id[MAX_PATH]; WCHAR name[MAX_PATH]; };
struct EnabledProfile {
    DWORD type; LANGID language; CLSID clsid; GUID profile; GUID category;
    DWORD substituteLayout; DWORD flags; WCHAR id[MAX_PATH];
};
using EnumSetup = UINT(WINAPI *)(LANGID, LayoutOrTip *, UINT, DWORD);
using EnumEnabled = UINT(WINAPI *)(LPCWSTR, LPCWSTR, LPCWSTR, EnabledProfile *, UINT);
using Install = BOOL(WINAPI *)(LPCWSTR, DWORD);
QString failure(const QString &operation) { return operation + QStringLiteral("失败，Windows 错误码 %1").arg(GetLastError()); }
}

WindowsInput::WindowsInput() : m_module(LoadLibraryExW(L"input.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {}
WindowsInput::~WindowsInput() { if (m_module) FreeLibrary(m_module); }

QList<HKL> WindowsInput::loadedLayouts()
{
    const int count = GetKeyboardLayoutList(0, nullptr);
    if (count <= 0) return {};
    std::vector<HKL> buffer(static_cast<size_t>(count));
    const int actual = GetKeyboardLayoutList(count, buffer.data());
    QList<HKL> result;
    for (int i = 0; i < actual && i < count; ++i) result.append(buffer[static_cast<size_t>(i)]);
    return result;
}

QList<InputProfile> WindowsInput::profiles(QString *error) const
{
    QList<InputProfile> result;
    const auto enumerate = m_module ? reinterpret_cast<EnumSetup>(GetProcAddress(m_module, "EnumLayoutOrTipForSetup")) : nullptr;
    const auto enumerateEnabled = m_module ? reinterpret_cast<EnumEnabled>(GetProcAddress(m_module, "EnumEnabledLayoutOrTip")) : nullptr;
    if (!enumerate || !enumerateEnabled) { if (error) *error = failure(QStringLiteral("读取输入法列表")); return result; }
    const auto layouts = loadedLayouts();
    QSet<QString> enabledIds;
    const UINT enabledCount = enumerateEnabled(nullptr, nullptr, nullptr, nullptr, 0);
    if (enabledCount > 4096) { if (error) *error = QStringLiteral("Windows 输入法列表数量异常。"); return result; }
    std::vector<EnabledProfile> enabledEntries(enabledCount);
    const UINT enabledActual = enabledCount ? enumerateEnabled(nullptr, nullptr, nullptr, enabledEntries.data(), enabledCount) : 0;
    for (UINT index = 0; index < std::min(enabledCount, enabledActual); ++index) {
        const auto profile = InputProfile::parse(QString::fromWCharArray(enabledEntries[index].id));
        if (!profile.id.isEmpty()) enabledIds.insert(profile.id);
    }
    ComPtr<ITfInputProcessorProfiles> tsf;
    CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&tsf));
    ComPtr<ITfInputProcessorProfileMgr> manager;
    if (tsf) {
        tsf.As(&manager);
    }
    // Setup enumeration is an OS-wide catalog, not the user's enabled list.
    QSet<LANGID> catalogLanguages{0x0409, 0x0804};
    for (HKL layout : layouts) catalogLanguages.insert(LOWORD(reinterpret_cast<quintptr>(layout)));
    QSet<QString> seen;
    for (LANGID language : catalogLanguages) {
        const UINT count = enumerate(language, nullptr, 0, 0);
        if (!count || count > 4096) continue;
        std::vector<LayoutOrTip> entries(count);
        const UINT actual = enumerate(language, entries.data(), count, 0);
        for (UINT index = 0; index < std::min(actual, count); ++index) {
            const auto &entry = entries[index];
            auto profile = InputProfile::parse(QString::fromWCharArray(entry.id), QString::fromWCharArray(entry.name));
            if (profile.id.isEmpty() || seen.contains(profile.id)) continue;
            if (profile.keyboard && !profile.isUS() && !enabledIds.contains(profile.id) && !profile.klid.startsWith(u'E')) continue;
            seen.insert(profile.id);
            profile.enabled = enabledIds.contains(profile.id);
            profile.systemDefault = (entry.flags & 0x1) != 0;
            if (!profile.keyboard && tsf) {
                BOOL enabled = FALSE;
                if (SUCCEEDED(tsf->IsEnabledLanguageProfile(profile.clsid, profile.language, profile.profile, &enabled))) profile.enabled = profile.enabled && enabled != FALSE;
                BSTR description = nullptr;
                if (SUCCEEDED(tsf->GetLanguageProfileDescription(profile.clsid, profile.language, profile.profile, &description))) {
                    profile.name = QString::fromWCharArray(description);
                    SysFreeString(description);
                }
            }
            if (manager) {
                ComPtr<IEnumTfInputProcessorProfiles> profiles;
                if (SUCCEEDED(manager->EnumProfiles(language, &profiles))) {
                    TF_INPUTPROCESSORPROFILE native{}; ULONG fetched = 0;
                    while (profiles->Next(1, &native, &fetched) == S_OK) {
                        if (!profile.keyboard && IsEqualGUID(native.clsid, profile.clsid) && IsEqualGUID(native.guidProfile, profile.profile)) {
                            profile.layout = native.hklSubstitute;
                            break;
                        }
                    }
                }
            }
            if (profile.isUS()) {
                for (HKL layout : layouts) if (layoutString(layout) == QStringLiteral("04090409")) profile.layout = layout;
            }
            if (!profile.layout) {
                QList<HKL> matching;
                for (HKL layout : layouts) if (LOWORD(reinterpret_cast<quintptr>(layout)) == profile.language) matching.append(layout);
                if (matching.size() == 1) profile.layout = matching.front();
            }
            if (profile.name.isEmpty()) profile.name = profile.id;
            result.append(profile);
        }
        // Setup/OOBE omits some third-party IMEs. Enumerate registered TSF
        // profiles as well, retaining the provider GUID rather than only an HKL.
        if (manager && tsf) {
            ComPtr<IEnumTfInputProcessorProfiles> registered;
            if (SUCCEEDED(manager->EnumProfiles(language, &registered))) {
                TF_INPUTPROCESSORPROFILE native{}; ULONG fetched = 0;
                while (registered->Next(1, &native, &fetched) == S_OK) {
                    if (native.dwProfileType != TF_PROFILETYPE_INPUTPROCESSOR || !IsEqualGUID(native.catid, GUID_TFCAT_TIP_KEYBOARD)) continue;
                    const QString id = QStringLiteral("%1:%2%3").arg(native.langid, 4, 16, u'0').arg(guidString(native.clsid), guidString(native.guidProfile)).toUpper();
                    auto entry = InputProfile::parse(id);
                    BSTR description = nullptr;
                    if (SUCCEEDED(tsf->GetLanguageProfileDescription(native.clsid, native.langid, native.guidProfile, &description))) {
                        entry.name = QString::fromWCharArray(description); SysFreeString(description);
                    }
                    if (entry.name.isEmpty()) entry.name = id;
                    BOOL enabled = FALSE;
                    entry.enabled = SUCCEEDED(tsf->IsEnabledLanguageProfile(native.clsid, native.langid, native.guidProfile, &enabled)) && enabled;
                    entry.layout = native.hklSubstitute;
                    if (!entry.layout) {
                        QList<HKL> matching;
                        for (HKL layout : layouts) if (LOWORD(reinterpret_cast<quintptr>(layout)) == language) matching.append(layout);
                        if (matching.size() == 1) entry.layout = matching.front();
                    }
                    auto existing = std::find_if(result.begin(), result.end(), [&id](const InputProfile &profile) { return profile.id == id; });
                    if (existing != result.end()) {
                        entry.systemDefault = existing->systemDefault;
                        *existing = entry;
                    } else result.append(entry);
                }
            }
        }
    }
    if (std::none_of(result.begin(), result.end(), [](const InputProfile &item) { return item.isUS(); })) {
        auto us = InputProfile::parse(QStringLiteral("0409:00000409"));
        for (HKL layout : layouts) if (layoutString(layout) == QStringLiteral("04090409")) { us.layout = layout; us.enabled = true; }
        result.append(us);
    }
    QSet<LANGID> enabledTextLanguages;
    for (const auto &profile : result) if (profile.enabled && !profile.keyboard) enabledTextLanguages.insert(profile.language);
    result.removeIf([&enabledTextLanguages](const InputProfile &profile) {
        return profile.keyboard && profile.isChinese() && enabledTextLanguages.contains(profile.language)
            && profile.klid == QString::number(profile.language, 16).rightJustified(8, u'0').toUpper();
    });
    HKL defaultLayout = nullptr;
    if (SystemParametersInfoW(SPI_GETDEFAULTINPUTLANG, 0, &defaultLayout, 0)) {
        const LANGID defaultLanguage = LOWORD(reinterpret_cast<quintptr>(defaultLayout));
        CLSID defaultClsid{}; GUID defaultProfile{};
        const bool hasDefaultTip = tsf && SUCCEEDED(tsf->GetDefaultLanguageProfile(defaultLanguage, GUID_TFCAT_TIP_KEYBOARD, &defaultClsid, &defaultProfile));
        for (auto &profile : result) {
            profile.systemDefault = profile.enabled && profile.language == defaultLanguage && (profile.keyboard
                ? profile.layout == defaultLayout
                : hasDefaultTip && IsEqualGUID(profile.clsid, defaultClsid) && IsEqualGUID(profile.profile, defaultProfile));
        }
    }
    std::stable_sort(result.begin(), result.end(), [](const InputProfile &left, const InputProfile &right) {
        if (left.enabled != right.enabled) return left.enabled;
        if (left.isUS() != right.isUS()) return left.isUS();
        return left.name.localeAwareCompare(right.name) < 0;
    });
    return result;
}

bool WindowsInput::setEnabled(const InputProfile &profile, bool enabled, QString *error) const
{
    const auto install = m_module ? reinterpret_cast<Install>(GetProcAddress(m_module, "InstallLayoutOrTip")) : nullptr;
    if (!install) { *error = failure(QStringLiteral("加载输入法管理接口")); return false; }
    if (!install(reinterpret_cast<LPCWSTR>(profile.id.utf16()), enabled ? 0u : 1u)) {
        *error = QStringLiteral("Windows 未能%1此输入法。").arg(enabled ? QStringLiteral("启用") : QStringLiteral("禁用"));
        return false;
    }
    return true;
}

bool WindowsInput::setDefault(const InputProfile &profile, QString *error) const
{
    const auto set = m_module ? reinterpret_cast<Install>(GetProcAddress(m_module, "SetDefaultLayoutOrTip")) : nullptr;
    if (!set || !set(reinterpret_cast<LPCWSTR>(profile.id.utf16()), 0)) {
        *error = QStringLiteral("Windows 未能设置默认输入法。"); return false;
    }
    return true;
}

HWND WindowsInput::focusedWindow(HWND foreground)
{
    GUITHREADINFO info{sizeof(info)};
    const DWORD thread = GetWindowThreadProcessId(foreground, nullptr);
    return thread && GetGUIThreadInfo(thread, &info) && info.hwndFocus ? info.hwndFocus : foreground;
}

QString WindowsInput::userSid()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
    DWORD size = 0; GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<BYTE> data(size);
    QString result;
    if (GetTokenInformation(token, TokenUser, data.data(), size, &size)) {
        LPWSTR sid = nullptr;
        if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(data.data())->User.Sid, &sid)) {
            result = QString::fromWCharArray(sid); LocalFree(sid);
        }
    }
    CloseHandle(token);
    return result;
}

bool WindowsInput::requestChinese(const InputProfile &profile, HWND focus, QString *error)
{
    if (!profile.isChinese() || !profile.enabled || !profile.layout || !IsWindow(focus)) {
        *error = QStringLiteral("中文切换目标不可用。"); return false;
    }
    ComPtr<ITfInputProcessorProfileMgr> manager;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&manager));
    if (SUCCEEDED(hr)) hr = manager->ActivateProfile(profile.keyboard ? TF_PROFILETYPE_KEYBOARDLAYOUT : TF_PROFILETYPE_INPUTPROCESSOR,
        profile.language, profile.clsid, profile.profile, profile.keyboard ? profile.layout : nullptr, TF_IPPMF_FORSESSION);
    if (FAILED(hr)) { *error = QStringLiteral("激活中文输入法失败（0x%1）。").arg(static_cast<quint32>(hr), 8, 16, u'0'); return false; }
    if (!PostMessageW(focus, WM_INPUTLANGCHANGEREQUEST, INPUTLANGCHANGE_SYSCHARSET, reinterpret_cast<LPARAM>(profile.layout))) {
        *error = failure(QStringLiteral("请求中文输入布局")); return false;
    }
    return true;
}


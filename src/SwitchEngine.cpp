#include "SwitchEngine.h"
#include "WindowsInput.h"
#include <objbase.h>
#include <msctf.h>
#include <wrl/client.h>
#include <algorithm>
#include <vector>

namespace {
constexpr UINT ToggleMessage = WM_APP + 83;
constexpr ULONG_PTR SwitchInputTag = 0x4353494d;
}
SwitchEngine *SwitchEngine::s_instance = nullptr;

KeyTracker::Decision KeyTracker::process(unsigned key, bool down, bool enabled, int shortcut)
{
    if (key >= m_down.size()) return Pass;
    const bool repeat = m_down[key];
    m_down[key] = down;
    if (!down) {
        if (m_swallowed[key]) { m_swallowed[key] = false; return Suppress; }
        return Pass;
    }
    if (m_swallowed[key]) return Suppress;
    if (repeat || !enabled || key != VK_SPACE) return Pass;
    const bool ctrl = m_down[VK_LCONTROL] || m_down[VK_RCONTROL] || m_down[VK_CONTROL];
    const bool alt = m_down[VK_LMENU] || m_down[VK_RMENU] || m_down[VK_MENU];
    const bool shift = m_down[VK_LSHIFT] || m_down[VK_RSHIFT] || m_down[VK_SHIFT];
    const bool win = m_down[VK_LWIN] || m_down[VK_RWIN];
    const bool match = !win && ((shortcut == 0 && ctrl && !alt && !shift)
        || (shortcut == 1 && ctrl && shift && !alt) || (shortcut == 2 && alt && !ctrl && !shift));
    if (!match) return Pass;
    m_swallowed[key] = true;
    return Toggle;
}
void KeyTracker::prime(unsigned key, bool down) { if (key < m_down.size()) m_down[key] = down; }
void KeyTracker::cancelSwallow(unsigned key) { if (key < m_swallowed.size()) m_swallowed[key] = false; }
bool KeyTracker::isDown(unsigned key) const { return key < m_down.size() && m_down[key]; }
void KeyTracker::recoverReleased(const std::vector<WORD> &temporarilyReleased)
{
    for (unsigned key = 0; key < m_down.size(); ++key)
        if (m_down[key] && !m_swallowed[key]
            && std::find(temporarilyReleased.cbegin(), temporarilyReleased.cend(), key) == temporarilyReleased.cend()
            && !(GetAsyncKeyState(static_cast<int>(key)) & 0x8000)) m_down[key] = false;
}

SwitchEngine::SwitchEngine(QObject *parent, bool testInjectedKeys) : QThread(parent), m_testInjectedKeys(testInjectedKeys) {}
SwitchEngine::~SwitchEngine() { stopEngine(); }
bool SwitchEngine::startEngine(QString *error)
{
    if (isRunning()) return m_installed;
    start(); m_ready.acquire();
    if (!m_installed) {
        wait(); *error = QStringLiteral("无法安装键盘监听，Windows 错误码 %1。").arg(m_startError);
    }
    return m_installed;
}
void SwitchEngine::stopEngine()
{
    const DWORD thread = m_threadId.load();
    if (thread && isRunning()) { PostThreadMessageW(thread, WM_QUIT, 0, 0); wait(); }
}
void SwitchEngine::configure(bool enabled, int shortcut, const InputProfile &chinese, int chineseCount, HWND testWindow)
{
    { std::lock_guard guard(m_configMutex); m_chinese = chinese; m_chineseCount = chineseCount; }
    m_testWindow.store(testWindow);
    m_shortcut.store(shortcut);
    m_enabled.store(enabled);
}
void SwitchEngine::request(HWND foreground)
{
    if (m_enabled.load()) PostThreadMessageW(m_threadId.load(), ToggleMessage, reinterpret_cast<WPARAM>(foreground), 0);
}
LRESULT CALLBACK SwitchEngine::hookProc(int code, WPARAM message, LPARAM data)
{
    auto *self = s_instance;
    if (!self || code != HC_ACTION) return CallNextHookEx(nullptr, code, message, data);
    const auto *event = reinterpret_cast<KBDLLHOOKSTRUCT *>(data);
    if (event->dwExtraInfo == SwitchInputTag) return CallNextHookEx(self->m_hook, code, message, data);
    const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    if (self->m_testInjectedKeys && (!(event->flags & LLKHF_INJECTED) || (down && GetForegroundWindow() != self->m_testWindow.load())))
        return CallNextHookEx(self->m_hook, code, message, data);
    if ((event->flags & LLKHF_INJECTED) && !self->m_testInjectedKeys) return CallNextHookEx(self->m_hook, code, message, data);
    const auto decision = self->m_keys.process(event->vkCode, down, self->m_enabled.load(), self->m_shortcut.load());
    if (self->m_testInjectedKeys) emit self->testTrace(QStringLiteral("%1 key=%2 down=%3 decision=%4").arg(GetTickCount64()).arg(event->vkCode).arg(down).arg(decision));
    if (decision == KeyTracker::Toggle) {
        if (PostThreadMessageW(GetCurrentThreadId(), ToggleMessage, reinterpret_cast<WPARAM>(GetForegroundWindow()), 0)) return 1;
        self->m_keys.cancelSwallow(event->vkCode);
    } else if (decision == KeyTracker::Suppress) return 1;
    return CallNextHookEx(self->m_hook, code, message, data);
}
void SwitchEngine::run()
{
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Microsoft::WRL::ComPtr<ITfThreadMgr> textServices;
    TfClientId textClient = TF_CLIENTID_NULL;
    const HRESULT textCreated = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&textServices));
    const HRESULT textActivated = SUCCEEDED(textCreated) ? textServices->Activate(&textClient) : textCreated;
    MSG message{};
    PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    m_threadId.store(GetCurrentThreadId());
    m_keys = {};
    for (unsigned key : {VK_LCONTROL, VK_RCONTROL, VK_LMENU, VK_RMENU, VK_LSHIFT, VK_RSHIFT, VK_LWIN, VK_RWIN})
        m_keys.prime(key, (GetAsyncKeyState(static_cast<int>(key)) & 0x8000) != 0);
    s_instance = this;
    m_hook = SetWindowsHookExW(WH_KEYBOARD_LL, hookProc, GetModuleHandleW(nullptr), 0);
    if (!m_hook) m_startError = GetLastError();
    const UINT_PTR timer = SetTimer(nullptr, 0, 40, nullptr);
    if (!timer && !m_startError) m_startError = GetLastError();
    m_installed = m_hook && timer && SUCCEEDED(com);
    if (FAILED(com)) m_startError = static_cast<DWORD>(com);
    m_ready.release();
    if (m_installed) {
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (message.message == ToggleMessage) toggle(reinterpret_cast<HWND>(message.wParam));
            else if (message.message == WM_TIMER && message.wParam == timer) { m_keys.recoverReleased(m_heldModifiers); poll(); }
            else { TranslateMessage(&message); DispatchMessageW(&message); }
        }
    }
    m_focus = nullptr;
    restoreModifiers();
    if (timer) KillTimer(nullptr, timer);
    if (m_hook) UnhookWindowsHookEx(m_hook);
    m_hook = nullptr; s_instance = nullptr; m_threadId.store(0);
    if (SUCCEEDED(textActivated)) textServices->Deactivate();
    textServices.Reset();
    if (SUCCEEDED(com)) CoUninitialize();
}
bool SwitchEngine::sendCycle()
{
    m_cycleBefore = layoutString(GetKeyboardLayout(m_targetThread));
    m_waitForChange = true;
    if (!m_cyclesSent) m_started = GetTickCount64();
    ++m_cyclesSent;
    if (m_testInjectedKeys) emit testTrace(QStringLiteral("%1 cycle=%2 before=%3 target=%4 ctrl=%5").arg(GetTickCount64()).arg(m_cyclesSent).arg(m_cycleBefore, layoutString(m_target)).arg((GetAsyncKeyState(VK_LCONTROL) & 0x8000) != 0));
    // Use the foreground window's system switcher in both directions. Session
    // profile activation would also change other windows and their initial IME.
    std::vector<INPUT> inputs;
    auto append = [&inputs](WORD key, bool up) {
        INPUT input{}; input.type = INPUT_KEYBOARD; input.ki.wVk = key;
        input.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0; input.ki.dwExtraInfo = SwitchInputTag;
        if (key == VK_RCONTROL || key == VK_RMENU || key == VK_LWIN) input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        inputs.push_back(input);
    };
    for (WORD key : {VK_LCONTROL, VK_RCONTROL, VK_LSHIFT, VK_RSHIFT}) {
        if (GetAsyncKeyState(key) & 0x8000) { m_heldModifiers.push_back(key); append(key, true); }
    }
    append(VK_LWIN, false);
    for (WORD key : {VK_LMENU, VK_RMENU}) {
        if (GetAsyncKeyState(key) & 0x8000) { m_heldModifiers.push_back(key); append(key, true); }
    }
    append(VK_SPACE, false); append(VK_SPACE, true); append(VK_LWIN, true);
    const UINT inserted = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    if (inserted == inputs.size()) return true;
    const DWORD error = GetLastError();
    if (inserted) {
        inputs.clear(); append(VK_SPACE, true); append(VK_LWIN, true);
        SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    }
    restoreModifiers(); m_focus = nullptr;
    emit result(false, QStringLiteral("切换请求未送达（%1）。请检查目标窗口权限。").arg(error));
    return false;
}
void SwitchEngine::restoreModifiers()
{
    std::vector<INPUT> inputs;
    for (WORD key : m_heldModifiers) {
        if (!m_keys.isDown(key) || (GetAsyncKeyState(key) & 0x8000)) continue;
        INPUT input{}; input.type = INPUT_KEYBOARD; input.ki.wVk = key; input.ki.dwExtraInfo = SwitchInputTag;
        if (key == VK_RCONTROL || key == VK_RMENU) input.ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
        inputs.push_back(input);
    }
    m_heldModifiers.clear();
    if (!inputs.empty() && SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT)) != inputs.size())
        emit result(false, QStringLiteral("修饰键状态未能恢复，请松开快捷键后重试。"));
}
void SwitchEngine::toggle(HWND foreground)
{
    if (!m_enabled.load() || !foreground || foreground != GetForegroundWindow() || !IsWindow(foreground)) return;
    const HWND focus = WindowsInput::focusedWindow(foreground);
    const DWORD thread = GetWindowThreadProcessId(focus, nullptr);
    if (!thread) return;
    InputProfile chinese;
    int chineseCount = 1;
    { std::lock_guard guard(m_configMutex); chinese = m_chinese; chineseCount = m_chineseCount; }
    HKL english = nullptr;
    const auto layouts = WindowsInput::loadedLayouts();
    for (HKL layout : layouts) if (layoutString(layout) == QStringLiteral("04090409")) english = layout;
    if (!english || !chinese.enabled || chineseCount != 1 || !layouts.contains(chinese.layout)) {
        restoreModifiers(); m_focus = nullptr; emit result(false, QStringLiteral("切换目标已变化，请刷新输入法列表。")); return;
    }
    const bool pending = m_focus == focus && m_foreground == foreground;
    const HKL current = pending ? m_target : GetKeyboardLayout(thread);
    const bool toChinese = current == english;
    m_target = toChinese ? chinese.layout : english;
    m_focus = focus; m_foreground = foreground; m_targetThread = thread;
    m_started = GetTickCount64();
    if (!pending) {
        restoreModifiers(); m_cyclesSent = 0; m_waitForChange = false;
        m_cycleBefore = layoutString(GetKeyboardLayout(thread));
    }
}
void SwitchEngine::poll()
{
    if (!m_focus) return;
    if (!m_enabled.load() || GetForegroundWindow() != m_foreground || !IsWindow(m_focus)
        || WindowsInput::focusedWindow(m_foreground) != m_focus || GetWindowThreadProcessId(m_focus, nullptr) != m_targetThread) {
        restoreModifiers(); m_focus = nullptr; return;
    }
    if (!m_cyclesSent && m_keys.isDown(VK_SPACE)) return;
    if (!m_cyclesSent) m_started = GetTickCount64();
    const HKL currentLayout = GetKeyboardLayout(m_targetThread);
    const QString signature = layoutString(currentLayout);
    if (signature != m_cycleBefore) { m_waitForChange = false; restoreModifiers(); }
    if (!m_waitForChange && currentLayout == m_target) {
        emit result(true, QStringLiteral("已切换到%1 · %2 ms").arg(LOWORD(reinterpret_cast<quintptr>(m_target)) == 0x0409
            ? QStringLiteral("美式键盘") : QStringLiteral("中文输入法")).arg(GetTickCount64() - m_started));
        m_focus = nullptr; return;
    }
    const auto elapsed = GetTickCount64() - m_started;
    if (elapsed >= 2000 || m_cyclesSent >= 16) {
        restoreModifiers(); m_focus = nullptr; emit result(false, QStringLiteral("目标窗口未确认切换，未继续重试。"));
    } else if (!m_waitForChange) {
        // Cycling is not idempotent: never send again while Windows has not
        // acknowledged the previous transition. Repeated presses only change
        // the desired final target until that transition completes.
        sendCycle();
    }
}

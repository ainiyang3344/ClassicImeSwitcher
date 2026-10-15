#pragma once
#include "InputProfile.h"
#include <QThread>
#include <QSemaphore>
#include <array>
#include <atomic>
#include <mutex>
#include <vector>

class KeyTracker {
public:
    enum Decision { Pass, Suppress, Toggle };
    Decision process(unsigned key, bool down, bool enabled, int shortcut);
    void prime(unsigned key, bool down);
    void cancelSwallow(unsigned key);
    void recoverReleased(const std::vector<WORD> &temporarilyReleased = {});
    bool isDown(unsigned key) const;
private:
    std::array<bool, 256> m_down{};
    std::array<bool, 256> m_swallowed{};
};

class SwitchEngine final : public QThread {
    Q_OBJECT
public:
    explicit SwitchEngine(QObject *parent = nullptr, bool testInjectedKeys = false);
    ~SwitchEngine() override;
    bool startEngine(QString *error);
    void stopEngine();
    void configure(bool enabled, int shortcut, const InputProfile &chinese, int chineseCount = 1, HWND testWindow = nullptr);
    void request(HWND foreground);
signals:
    void result(bool ok, const QString &message);
    void testTrace(const QString &message);
protected:
    void run() override;
private:
    static LRESULT CALLBACK hookProc(int code, WPARAM message, LPARAM data);
    void toggle(HWND foreground);
    void poll();
    bool sendCycle();
    void restoreModifiers();
    static SwitchEngine *s_instance;
    QSemaphore m_ready;
    std::atomic<DWORD> m_threadId{0};
    std::atomic_bool m_enabled{false};
    std::atomic_int m_shortcut{0};
    std::mutex m_configMutex;
    InputProfile m_chinese;
    int m_chineseCount = 1;
    std::atomic<HWND> m_testWindow{nullptr};
    KeyTracker m_keys;
    HHOOK m_hook = nullptr;
    DWORD m_startError = 0;
    bool m_installed = false;
    bool m_testInjectedKeys = false;
    HWND m_foreground = nullptr;
    HWND m_focus = nullptr;
    DWORD m_targetThread = 0;
    HKL m_target = nullptr;
    ULONGLONG m_started = 0;
    QString m_cycleBefore;
    int m_cyclesSent = 0;
    bool m_waitForChange = false;
    std::vector<WORD> m_heldModifiers;
};

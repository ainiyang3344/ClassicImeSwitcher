#pragma once
#include "InputProfile.h"
#include <QList>
#include <QAbstractNativeEventFilter>

class OwnInputLanguageFilter final : public QAbstractNativeEventFilter {
public:
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;
private:
    bool m_synchronizing = false;
};

class WindowsInput {
public:
    WindowsInput();
    ~WindowsInput();
    WindowsInput(const WindowsInput &) = delete;
    WindowsInput &operator=(const WindowsInput &) = delete;

    QList<InputProfile> profiles(QString *error = nullptr) const;
    bool setEnabled(const InputProfile &profile, bool enabled, QString *error) const;
    bool setDefault(const InputProfile &profile, QString *error) const;
    static QList<HKL> loadedLayouts();
    static HWND focusedWindow(HWND foreground);
    static QString userSid();
private:
    HMODULE m_module = nullptr;
};

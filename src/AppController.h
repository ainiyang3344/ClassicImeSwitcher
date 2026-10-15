#pragma once
#include "WindowsInput.h"
#include "SwitchEngine.h"
#include <QObject>
#include <QSettings>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class AppController final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application")
    Q_PROPERTY(QVariantList enabledProfiles READ enabledProfiles NOTIFY profilesChanged)
    Q_PROPERTY(QVariantList disabledProfiles READ disabledProfiles NOTIFY profilesChanged)
    Q_PROPERTY(QVariantList chineseChoices READ chineseChoices NOTIFY profilesChanged)
    Q_PROPERTY(QString chineseId READ chineseId WRITE setChineseId NOTIFY settingsChanged)
    Q_PROPERTY(bool hasUS READ hasUS NOTIFY profilesChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY profilesChanged)
    Q_PROPERTY(QString preparationHint READ preparationHint NOTIFY profilesChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY settingsChanged)
    Q_PROPERTY(int shortcut READ shortcut WRITE setShortcut NOTIFY settingsChanged)
    Q_PROPERTY(QString shortcutText READ shortcutText NOTIFY settingsChanged)
    Q_PROPERTY(bool autoStart READ autoStart WRITE setAutoStart NOTIFY settingsChanged)
    Q_PROPERTY(bool closeToTray READ closeToTray WRITE setCloseToTray NOTIFY settingsChanged)
    Q_PROPERTY(QString currentInput READ currentInput NOTIFY currentInputChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(bool noticeError READ noticeError NOTIFY noticeChanged)
    Q_PROPERTY(QString dataDirectory READ dataDirectory CONSTANT)
    Q_PROPERTY(QString qtVersion READ qtVersion CONSTANT)
public:
    explicit AppController(QString stateDirectory, bool safeMode, QObject *parent = nullptr);
    QVariantList enabledProfiles() const;
    QVariantList disabledProfiles() const;
    QVariantList chineseChoices() const;
    QString chineseId() const { return m_chineseId; }
    bool hasUS() const;
    bool ready() const;
    QString preparationHint() const;
    bool enabled() const { return m_enabled; }
    int shortcut() const { return m_shortcut; }
    QString shortcutText() const;
    bool autoStart() const;
    bool closeToTray() const;
    QString currentInput() const { return m_currentInput; }
    QString notice() const { return m_notice; }
    bool noticeError() const { return m_noticeError; }
    QString dataDirectory() const { return m_stateDirectory; }
    QString qtVersion() const { return QString::fromLatin1(qVersion()); }
    void setChineseId(const QString &id);
    void setEnabled(bool enabled);
    void setShortcut(int shortcut);
    void setAutoStart(bool enabled);
    void setCloseToTray(bool enabled);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void addUS();
    Q_INVOKABLE void setProfileEnabled(const QString &id, bool enabled);
    Q_INVOKABLE void setDefaultProfile(const QString &id);
    Q_INVOKABLE void openDataDirectory();
    Q_INVOKABLE void quit();
    Q_INVOKABLE void openWindowsLanguageSettings();
    const QList<InputProfile> &nativeProfiles() const { return m_profiles; }
signals:
    void profilesChanged();
    void settingsChanged();
    void currentInputChanged();
    void noticeChanged();
private:
    const InputProfile *find(const QString &id) const;
    InputProfile selectedChinese() const;
    int enabledChineseCount() const;
    void updateEngine();
    void announce(const QString &message, bool error = false);
    bool backup();
    void updateCurrentInput();
    QString m_stateDirectory;
    QSettings m_settings;
    WindowsInput m_windows;
    SwitchEngine m_engine;
    QList<InputProfile> m_profiles;
    QTimer m_poll;
    QString m_chineseId;
    QString m_currentInput;
    QString m_notice = QStringLiteral("准备就绪。添加美式键盘后，可启用经典切换。");
    int m_shortcut = 0;
    bool m_enabled = false;
    bool m_safeMode = false;
    bool m_engineStarted = false;
    bool m_noticeError = false;
};

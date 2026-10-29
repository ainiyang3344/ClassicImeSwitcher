#include "InputProfile.h"
#include "SwitchEngine.h"
#include <QtTest>
#include "StartupTask.h"
#include "WindowsInput.h"
#include <QScopeGuard>
#include <taskschd.h>
#include <wrl/client.h>
#include <comdef.h>
#include <sddl.h>

class ImeCoreTests : public QObject {
    Q_OBJECT
private slots:
    void elevatedStartupDefinition() {
        const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        QVERIFY(SUCCEEDED(initialized));
        const auto cleanup = qScopeGuard([] { CoUninitialize(); });
        using Microsoft::WRL::ComPtr;
        ComPtr<ITaskService> service;
        QVERIFY(SUCCEEDED(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&service))));
        QVERIFY(SUCCEEDED(service->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t())));
        ComPtr<ITaskDefinition> task;
        QVERIFY(SUCCEEDED(service->NewTask(0, &task)));
        // Spaces, Chinese and XML metacharacters must survive as an executable
        // path, without shell quoting or a command interpreter.
        const QString executable = QStringLiteral("C:/测试 & tools/ClassicImeSwitcher.exe");
        const auto accountSid = [](LPCWSTR account) -> QString {
            DWORD sidSize = 0, domainSize = 0;
            SID_NAME_USE use{};
            LookupAccountNameW(nullptr, account, nullptr, &sidSize, nullptr, &domainSize, &use);
            QByteArray bytes(static_cast<qsizetype>(sidSize), '\0');
            std::vector<wchar_t> domain(domainSize);
            if (!LookupAccountNameW(nullptr, account, bytes.data(), &sidSize, domain.data(), &domainSize, &use)) return {};
            LPWSTR text = nullptr;
            if (!ConvertSidToStringSidW(bytes.data(), &text)) return {};
            const QString value = QString::fromWCharArray(text);
            LocalFree(text);
            return value;
        };
        const QString sid = WindowsInput::userSid();
        QVERIFY(!sid.isEmpty());
        const QString xml = StartupTask::definition(executable, sid);
        const HRESULT parsed = task->put_XmlText(_bstr_t(reinterpret_cast<LPCWSTR>(xml.utf16())));
        QVERIFY2(SUCCEEDED(parsed), qPrintable(QString::number(static_cast<quint32>(parsed), 16)));
        ComPtr<IPrincipal> principal;
        QVERIFY(SUCCEEDED(task->get_Principal(&principal)));
        TASK_RUNLEVEL_TYPE level = TASK_RUNLEVEL_LUA;
        TASK_LOGON_TYPE logon = TASK_LOGON_NONE;
        _bstr_t user;
        QVERIFY(SUCCEEDED(principal->get_RunLevel(&level)));
        QVERIFY(SUCCEEDED(principal->get_LogonType(&logon)));
        QVERIFY(SUCCEEDED(principal->get_UserId(user.GetAddress())));
        QCOMPARE(level, TASK_RUNLEVEL_HIGHEST);
        QCOMPARE(logon, TASK_LOGON_INTERACTIVE_TOKEN);
        QCOMPARE(accountSid(user), sid);
        ComPtr<ITriggerCollection> triggers;
        ComPtr<ITrigger> trigger;
        ComPtr<ILogonTrigger> login;
        LONG count = 0;
        QVERIFY(SUCCEEDED(task->get_Triggers(&triggers)));
        QVERIFY(SUCCEEDED(triggers->get_Count(&count))); QCOMPARE(count, 1L);
        QVERIFY(SUCCEEDED(triggers->get_Item(1, &trigger)));
        QVERIFY(SUCCEEDED(trigger.As(&login)));
        QVERIFY(SUCCEEDED(login->get_UserId(user.GetAddress())));
        QCOMPARE(accountSid(user), sid);
        ComPtr<IActionCollection> actions;
        ComPtr<IAction> action;
        ComPtr<IExecAction> exec;
        QVERIFY(SUCCEEDED(task->get_Actions(&actions)));
        QVERIFY(SUCCEEDED(actions->get_Count(&count))); QCOMPARE(count, 1L);
        QVERIFY(SUCCEEDED(actions->get_Item(1, &action)));
        QVERIFY(SUCCEEDED(action.As(&exec)));
        _bstr_t path, arguments, directory;
        QVERIFY(SUCCEEDED(exec->get_Path(path.GetAddress())));
        QVERIFY(SUCCEEDED(exec->get_Arguments(arguments.GetAddress())));
        QVERIFY(SUCCEEDED(exec->get_WorkingDirectory(directory.GetAddress())));
        QCOMPARE(QDir::fromNativeSeparators(QString::fromWCharArray(path)), executable);
        QCOMPARE(QString::fromWCharArray(arguments), QStringLiteral("--background"));
        QCOMPARE(QDir::fromNativeSeparators(QString::fromWCharArray(directory)), QStringLiteral("C:/测试 & tools"));
        ComPtr<ITaskSettings> settings;
        QVERIFY(SUCCEEDED(task->get_Settings(&settings)));
        VARIANT_BOOL battery = VARIANT_TRUE;
        _bstr_t duration;
        QVERIFY(SUCCEEDED(settings->get_DisallowStartIfOnBatteries(&battery))); QCOMPARE(battery, VARIANT_FALSE);
        QVERIFY(SUCCEEDED(settings->get_StopIfGoingOnBatteries(&battery))); QCOMPARE(battery, VARIANT_FALSE);
        QVERIFY(SUCCEEDED(settings->get_ExecutionTimeLimit(duration.GetAddress())));
        QCOMPARE(QString::fromWCharArray(duration), QStringLiteral("PT0S"));
    }
    void normalizeProfileIds() {
        auto us = InputProfile::parse(QStringLiteral("0x0409:0x00000409"));
        QVERIFY(us.isUS()); QCOMPARE(us.id, QStringLiteral("0409:00000409"));
        auto chinese = InputProfile::parse(QStringLiteral("0804:{81d4e9c9-1d3b-41bc-9e6c-4b40bf79e35e}{fa550b04-5ad7-411f-a5ac-ca038ec515d7}"));
        QVERIFY(chinese.isChinese()); QVERIFY(!chinese.keyboard); QVERIFY(!chinese.id.isEmpty());
        QVERIFY(InputProfile::parse(QStringLiteral("0409:00010409")).isUS() == false);
        QVERIFY(InputProfile::parse(QStringLiteral("0409:invalid")).id.isEmpty());
        QVERIFY(InputProfile::parse(QStringLiteral("0409:00000409;0804:00000804")).id.isEmpty());
    }
    void longPressIsOneToggle() {
        KeyTracker keys;
        QCOMPARE(keys.process(VK_LCONTROL, true, true, 0), KeyTracker::Pass);
        QCOMPARE(keys.process(VK_SPACE, true, true, 0), KeyTracker::Toggle);
        for (int repeat = 0; repeat < 8; ++repeat) QCOMPARE(keys.process(VK_SPACE, true, true, 0), KeyTracker::Suppress);
        QCOMPARE(keys.process(VK_SPACE, false, true, 0), KeyTracker::Suppress);
        QCOMPARE(keys.process(VK_SPACE, true, true, 0), KeyTracker::Toggle);
    }
    void extraModifiersDoNotStealShortcut() {
        KeyTracker keys;
        keys.prime(VK_LCONTROL, true); keys.prime(VK_LSHIFT, true);
        QCOMPARE(keys.process(VK_SPACE, true, true, 0), KeyTracker::Pass);
        keys.process(VK_SPACE, false, true, 0); keys.prime(VK_LSHIFT, false); keys.prime(VK_LWIN, true);
        QCOMPARE(keys.process(VK_SPACE, true, true, 0), KeyTracker::Pass);
    }
    void pausedAndOrdinarySpacePassThrough() {
        KeyTracker keys;
        QCOMPARE(keys.process(VK_SPACE, true, true, 0), KeyTracker::Pass);
        keys.process(VK_SPACE, false, true, 0); keys.prime(VK_RCONTROL, true);
        QCOMPARE(keys.process(VK_SPACE, true, false, 0), KeyTracker::Pass);
    }
    void disablingDuringPressPreservesKeyPair() {
        KeyTracker keys; keys.prime(VK_RCONTROL, true);
        QCOMPARE(keys.process(VK_SPACE, true, true, 0), KeyTracker::Toggle);
        QCOMPARE(keys.process(VK_SPACE, false, false, 0), KeyTracker::Suppress);
        QCOMPARE(keys.process(VK_SPACE, true, false, 0), KeyTracker::Pass);
    }
    void alternativeShortcut() {
        KeyTracker keys; keys.prime(VK_LMENU, true);
        QCOMPARE(keys.process(VK_SPACE, true, true, 2), KeyTracker::Toggle);
        keys.process(VK_SPACE, false, true, 2); keys.prime(VK_LMENU, false); keys.prime(VK_LCONTROL, true); keys.prime(VK_LSHIFT, true);
        QCOMPARE(keys.process(VK_SPACE, true, true, 1), KeyTracker::Toggle);
    }
};
QTEST_GUILESS_MAIN(ImeCoreTests)
#include "tst_imecore.moc"

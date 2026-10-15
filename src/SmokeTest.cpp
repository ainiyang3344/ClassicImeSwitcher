#include "SmokeTest.h"
#include "AppController.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QInputMethod>
#include <QQuickWindow>
#include <QProcess>
#include <QTimer>
#include <QVBoxLayout>
#include <functional>
#include <imm.h>
#include <msctf.h>
#include <wrl/client.h>

namespace {
struct TestRun : std::enable_shared_from_this<TestRun> {
    QQuickWindow *window = nullptr;
    AppController *controller = nullptr;
    QString directory;
    QString mode;
    QJsonObject report;
    QJsonArray steps;
    QJsonArray keyTrace;
    std::shared_ptr<QStringList> warnings;
    std::unique_ptr<QWidget> editor;
    QLineEdit *text = nullptr;
    std::unique_ptr<SwitchEngine> engine;
    std::unique_ptr<QProcess> externalProcess;
    HWND externalWindow = nullptr;
    InputProfile chinese;
    HKL originalLayout = nullptr;
    HWND originalForeground = nullptr;
    int switchResults = 0;
    bool switchError = false;
    QString originalDefault;
    bool restoringInput = false;

    void finish(bool passed, const QString &error = {}) {
        if (engine && (editor || externalWindow) && originalLayout && targetLayout() != originalLayout && !restoringInput) {
            restoringInput = true;
            engine->request(topWindow());
            QTimer::singleShot(900, qApp, [self = shared_from_this(), passed, error] { self->finish(passed, error); });
            return;
        }
        if (engine) { QObject::disconnect(engine.get(), nullptr, qApp, nullptr); engine->stopEngine(); }
        if (editor || externalWindow) {
            if (originalLayout) {
                const bool restored = targetLayout() == originalLayout && !switchError;
                report.insert("originalInputRestored", restored);
                if (!restored) { passed = false; report.insert("restoreError", "Original input layout was not restored through the system switcher."); }
            }
            if (editor) editor->close();
            if (externalWindow) {
                PostMessageW(externalWindow, WM_CLOSE, 0, 0);
                if (!externalProcess->waitForFinished(1500)) { externalProcess->kill(); externalProcess->waitForFinished(1000); }
            }
        }
        if (IsWindow(originalForeground)) SetForegroundWindow(originalForeground);
        report.insert("passed", passed && warnings->isEmpty());
        report.insert("error", error);
        report.insert("mode", mode);
        report.insert("qtVersion", QString::fromLatin1(qVersion()));
        report.insert("userSid", WindowsInput::userSid());
        report.insert("steps", steps);
        report.insert("keyTrace", keyTrace);
        report.insert("qmlWarnings", QJsonArray::fromStringList(*warnings));
        report.insert("enabledProfiles", QJsonArray::fromVariantList(controller->enabledProfiles()));
        QFile file(directory + QStringLiteral("/report.json"));
        const bool saved = file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(report).toJson()) > 0;
        QApplication::exit(saved && report.value("passed").toBool() ? 0 : 2);
    }
    void capture(int page) {
        const auto self = shared_from_this();
        window->setProperty("currentPage", page);
        QTimer::singleShot(350, qApp, [self, page] {
            const QString file = self->directory + QStringLiteral("/page-%1.png").arg(page);
            const auto image = self->window->grabWindow();
            const bool saved = !image.isNull() && image.save(file);
            self->steps.append(QJsonObject{{"page", page}, {"screenshot", file}, {"passed", saved}});
            if (!saved) { self->finish(false, QStringLiteral("Window capture failed.")); return; }
            if (page < 2) self->capture(page + 1);
            else if (self->mode == QStringLiteral("switch")) self->beginSwitch();
            else if (self->mode == QStringLiteral("management")) self->manage();
            else if (self->mode == QStringLiteral("external")) self->beginExternal();
            else self->finish(true);
        });
    }
    void manage() {
        const bool originallyUS = controller->hasUS();
        for (const auto &profile : controller->nativeProfiles()) if (profile.systemDefault) originalDefault = profile.id;
        report.insert("originallyUS", originallyUS);
        report.insert("originalDefault", originalDefault);
        // Leave the VM in the same US-keyboard state after this round trip.
        if (originallyUS) controller->setProfileEnabled(QStringLiteral("0409:00000409"), false);
        else controller->addUS();
        const auto self = shared_from_this();
        QTimer::singleShot(500, qApp, [self, originallyUS] {
            self->controller->refresh();
            const bool first = self->controller->hasUS() != originallyUS;
            self->steps.append(QJsonObject{{"operation", originallyUS ? "disableUS" : "addUS"}, {"passed", first}, {"notice", self->controller->notice()}});
            if (originallyUS) self->controller->addUS();
            else self->controller->setProfileEnabled(QStringLiteral("0409:00000409"), false);
            QTimer::singleShot(500, qApp, [self, originallyUS, first] {
                self->controller->refresh();
                const bool restored = self->controller->hasUS() == originallyUS;
                self->steps.append(QJsonObject{{"operation", "restoreOriginalUSState"}, {"passed", restored}, {"notice", self->controller->notice()}});
                if (!first || !restored) { self->finish(false, QStringLiteral("Input management round trip failed.")); return; }
                if (originallyUS && !self->originalDefault.isEmpty()) self->testDefault();
                else self->testMultipleChineseGuard();
            });
        });
    }
    void testDefault() {
        controller->setDefaultProfile(QStringLiteral("0409:00000409"));
        QTimer::singleShot(300, qApp, [self = shared_from_this()] {
            self->controller->refresh();
            bool changed = false;
            for (const auto &profile : self->controller->nativeProfiles()) if (profile.isUS() && profile.systemDefault) changed = true;
            self->steps.append(QJsonObject{{"operation", "setDefaultUS"}, {"passed", changed}, {"notice", self->controller->notice()}});
            self->controller->setDefaultProfile(self->originalDefault);
            QTimer::singleShot(300, qApp, [self, changed] {
                self->controller->refresh();
                bool restored = false;
                for (const auto &profile : self->controller->nativeProfiles()) if (profile.id == self->originalDefault && profile.systemDefault) restored = true;
                self->steps.append(QJsonObject{{"operation", "restoreDefault"}, {"passed", restored}, {"notice", self->controller->notice()}});
                if (changed && restored) self->testMultipleChineseGuard();
                else self->finish(false, QStringLiteral("Default input profile round trip failed."));
            });
        });
    }
    void testMultipleChineseGuard() {
        QString extraId;
        for (const auto &profile : controller->nativeProfiles()) {
            if (!profile.enabled && profile.isChinese()) { extraId = profile.id; break; }
        }
        if (extraId.isEmpty() || controller->chineseChoices().size() != 1) {
            steps.append(QJsonObject{{"operation", "multipleChineseGuard"}, {"skipped", true}, {"reason", "Requires one enabled and one disabled Chinese IME."}});
            finish(true); return;
        }
        controller->setProfileEnabled(extraId, true);
        QTimer::singleShot(350, qApp, [self = shared_from_this(), extraId] {
            self->controller->refresh();
            bool extraEnabled = false;
            for (const auto &profile : self->controller->nativeProfiles()) if (profile.id == extraId) extraEnabled = profile.enabled;
            self->controller->setEnabled(true);
            const bool blocked = extraEnabled && !self->controller->ready() && !self->controller->enabled();
            self->steps.append(QJsonObject{{"operation", "multipleChineseGuard"}, {"passed", blocked}, {"notice", self->controller->notice()}});
            self->controller->setEnabled(false);
            self->controller->setProfileEnabled(extraId, false);
            QTimer::singleShot(350, qApp, [self, extraId, blocked] {
                self->controller->refresh();
                bool disabled = false;
                for (const auto &profile : self->controller->nativeProfiles()) if (profile.id == extraId) disabled = !profile.enabled;
                const bool restored = disabled && self->controller->ready() && !self->controller->enabled();
                self->steps.append(QJsonObject{{"operation", "restoreSingleChinese"}, {"passed", restored}});
                self->finish(blocked && restored, blocked && restored ? QString() : QStringLiteral("Multiple Chinese IME guard or restoration failed."));
            });
        });
    }
    HWND topWindow() const { return externalWindow ? externalWindow : reinterpret_cast<HWND>(editor->winId()); }
    HWND targetWindow() const { return WindowsInput::focusedWindow(topWindow()); }
    bool targetFocused() const { return GetForegroundWindow() == topWindow() && (externalWindow || text->hasFocus()); }
    QString targetText() const {
        if (!externalWindow) return text->text();
        wchar_t buffer[256]{}; DWORD_PTR copied = 0;
        SendMessageTimeoutW(targetWindow(), WM_GETTEXT, 256, reinterpret_cast<LPARAM>(buffer), SMTO_ABORTIFHUNG, 500, &copied);
        return QString::fromWCharArray(buffer);
    }
    HKL targetLayout() const { return GetKeyboardLayout(GetWindowThreadProcessId(targetWindow(), nullptr)); }
    QJsonObject inputState() const {
        if (externalWindow) {
            return {{"hkl", layoutString(targetLayout())}, {"externalProcess", true},
                {"ctrl", (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0}, {"shift", (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0},
                {"alt", (GetAsyncKeyState(VK_MENU) & 0x8000) != 0}, {"lwin", (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0}, {"rwin", (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0}, {"space", (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0}};
        }
        using Microsoft::WRL::ComPtr;
        ComPtr<ITfInputProcessorProfileMgr> manager;
        TF_INPUTPROCESSORPROFILE profile{};
        const HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&manager));
        const HRESULT active = SUCCEEDED(hr) ? manager->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &profile) : hr;
        const HIMC context = ImmGetContext(targetWindow());
        const bool open = context && ImmGetOpenStatus(context);
        if (context) ImmReleaseContext(targetWindow(), context);
        wchar_t klid[KL_NAMELENGTH]{}; GetKeyboardLayoutNameW(klid);
        return {{"hkl", layoutString(targetLayout())}, {"klid", QString::fromWCharArray(klid)},
            {"activeResult", static_cast<int>(active)}, {"activeType", static_cast<int>(profile.dwProfileType)},
            {"activeLanguage", static_cast<int>(profile.langid)}, {"activeLayout", layoutString(profile.hkl)},
            {"activeGuid", guidString(profile.guidProfile)}, {"immOpen", open},
            {"qtInputLocale", QGuiApplication::inputMethod()->locale().name()}};
    }
    bool sendShortcut(bool held = false, int holdMs = 150) {
        if (!targetFocused()) return false;
        INPUT input[6]{};
        const WORD keys[6] = {VK_LCONTROL, VK_SPACE, VK_SPACE, VK_SPACE, VK_SPACE, VK_LCONTROL};
        for (int i = 0; i < 6; ++i) { input[i].type = INPUT_KEYBOARD; input[i].ki.wVk = keys[i]; }
        input[4].ki.dwFlags = KEYEVENTF_KEYUP;
        input[5].ki.dwFlags = KEYEVENTF_KEYUP;
        if (!held) return SendInput(6, input, sizeof(INPUT)) == 6;
        const bool sent = SendInput(4, input, sizeof(INPUT)) == 4;
        QTimer::singleShot(70, qApp, [self = shared_from_this()] {
            if (!self->targetFocused()) return;
            INPUT repeat{}; repeat.type = INPUT_KEYBOARD; repeat.ki.wVk = VK_SPACE;
            SendInput(1, &repeat, sizeof(INPUT));
        });
        QTimer::singleShot(holdMs, qApp, [] {
            INPUT release{}; release.type = INPUT_KEYBOARD;
            release.ki.wVk = VK_SPACE; release.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &release, sizeof(INPUT));
        });
        QTimer::singleShot(holdMs + 150, qApp, [] {
            INPUT release{}; release.type = INPUT_KEYBOARD;
            release.ki.wVk = VK_LCONTROL; release.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &release, sizeof(INPUT));
        });
        return sent;
    }
    void beginExternal() {
        if (!controller->ready()) { finish(false, QStringLiteral("Switch targets are not ready.")); return; }
        for (const auto &profile : controller->nativeProfiles()) if (profile.id == controller->chineseId()) chinese = profile;
        originalForeground = GetForegroundWindow();
        window->hide();
        const QString executable = QCoreApplication::applicationDirPath() + QStringLiteral("/../test-support/NativeInputTarget.exe");
        if (!QFile::exists(executable)) { finish(false, QStringLiteral("Native validation target is missing.")); return; }
        externalProcess = std::make_unique<QProcess>(); externalProcess->start(executable);
        QTimer::singleShot(350, qApp, [self = shared_from_this()] { self->prepareExternal(0); });
    }
    void prepareExternal(int attempt) {
        struct Search { DWORD pid; HWND window; } search{static_cast<DWORD>(externalProcess->processId()), nullptr};
        EnumWindows([](HWND window, LPARAM context) -> BOOL {
            auto *state = reinterpret_cast<Search *>(context);
            DWORD pid = 0; GetWindowThreadProcessId(window, &pid);
            wchar_t name[100]{}; GetClassNameW(window, name, 100);
            if (pid == state->pid && QString::fromWCharArray(name) == QStringLiteral("LYLab.ClassicIme.NativeInputTarget")) { state->window = window; return FALSE; }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        if (!search.window) {
            if (attempt >= 15) { finish(false, QStringLiteral("Native test target did not open.")); return; }
            QTimer::singleShot(100, qApp, [self = shared_from_this(), attempt] { self->prepareExternal(attempt + 1); }); return;
        }
        externalWindow = search.window;
        const DWORD targetThread = GetWindowThreadProcessId(externalWindow, nullptr);
        const DWORD foregroundThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
        const DWORD ownThread = GetCurrentThreadId();
        const bool foregroundAttached = foregroundThread && foregroundThread != ownThread && AttachThreadInput(ownThread, foregroundThread, TRUE);
        const bool attached = targetThread != ownThread && targetThread != foregroundThread && AttachThreadInput(ownThread, targetThread, TRUE);
        BringWindowToTop(externalWindow);
        SetForegroundWindow(externalWindow);
        SetActiveWindow(externalWindow);
        const HWND child = FindWindowExW(externalWindow, nullptr, L"EDIT", nullptr);
        SetFocus(child);
        if (attached) AttachThreadInput(ownThread, targetThread, FALSE);
        if (foregroundAttached) AttachThreadInput(ownThread, foregroundThread, FALSE);
        engine = std::make_unique<SwitchEngine>(nullptr, true);
        QString error;
        if (!engine->startEngine(&error)) { finish(false, error); return; }
        engine->configure(true, 0, chinese, static_cast<int>(controller->chineseChoices().size()), externalWindow);
        QObject::connect(engine.get(), &SwitchEngine::result, qApp, [self = shared_from_this()](bool ok, const QString &) { ++self->switchResults; if (!ok) self->switchError = true; }, Qt::QueuedConnection);
        QObject::connect(engine.get(), &SwitchEngine::testTrace, qApp, [self = shared_from_this()](const QString &message) { self->keyTrace.append(message); }, Qt::QueuedConnection);
        prepareInput();
    }
    void beginSwitch() {
        if (!controller->ready()) { finish(false, QStringLiteral("Switch targets are not ready.")); return; }
        for (const auto &profile : controller->nativeProfiles()) if (profile.id == controller->chineseId()) chinese = profile;
        window->hide();
        originalForeground = GetForegroundWindow();
        editor = std::make_unique<QWidget>();
        editor->setWindowTitle(QStringLiteral("Classic IME automated verification"));
        editor->resize(530, 170);
        auto *layout = new QVBoxLayout(editor.get());
        text = new QLineEdit(editor.get()); layout->addWidget(text);
        text->setPlaceholderText(QStringLiteral("The test window will close automatically."));
        editor->show(); editor->raise(); editor->activateWindow(); text->setFocus();
        engine = std::make_unique<SwitchEngine>(nullptr, true);
        QString error;
        if (!engine->startEngine(&error)) { finish(false, error); return; }
        engine->configure(true, 0, chinese, static_cast<int>(controller->chineseChoices().size()), reinterpret_cast<HWND>(editor->winId()));
        QObject::connect(engine.get(), &SwitchEngine::result, qApp, [self = shared_from_this()](bool ok, const QString &) { ++self->switchResults; if (!ok) self->switchError = true; }, Qt::QueuedConnection);
        QObject::connect(engine.get(), &SwitchEngine::testTrace, qApp, [self = shared_from_this()](const QString &message) { self->keyTrace.append(message); }, Qt::QueuedConnection);
        QTimer::singleShot(300, qApp, [self = shared_from_this()] {
            const HWND window = reinterpret_cast<HWND>(self->editor->winId());
            const DWORD foregroundThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
            const DWORD ownThread = GetCurrentThreadId();
            const bool attached = foregroundThread && foregroundThread != ownThread && AttachThreadInput(ownThread, foregroundThread, TRUE);
            BringWindowToTop(window);
            SetForegroundWindow(window);
            SetActiveWindow(window);
            self->text->setFocus(Qt::OtherFocusReason);
            if (attached) AttachThreadInput(ownThread, foregroundThread, FALSE);
            QTimer::singleShot(150, qApp, [self] { self->prepareInput(); });
        });
    }
    void prepareInput() {
        originalLayout = targetLayout();
        if (targetLayout() != chinese.layout) engine->request(topWindow());
        QTimer::singleShot(450, qApp, [self = shared_from_this()] {
            if (self->switchError) { self->finish(false, QStringLiteral("Could not prepare Chinese input through the system switcher.")); return; }
            self->switchResults = 0;
            self->switchStep(0);
        });
    }
    void switchStep(int step) {
        if (step == 0 && targetLayout() != chinese.layout) { finish(false, QStringLiteral("Could not prepare Chinese input.")); return; }
        if (!sendShortcut(true, step == 0 ? 2300 : 150)) {
            report.insert("foregroundWindow", QString::number(reinterpret_cast<quintptr>(GetForegroundWindow()), 16));
            report.insert("testWindow", QString::number(reinterpret_cast<quintptr>(topWindow()), 16));
            report.insert("textHasFocus", targetFocused());
            finish(false, QStringLiteral("Test editor lost focus; no keys sent.")); return;
        }
        checkSwitchStep(step, 0);
    }
    void checkSwitchStep(int step, int attempt) {
        QTimer::singleShot(attempt == 0 ? 450 : 100, qApp, [self = shared_from_this(), step, attempt] {
            const bool modifiersReleased = !(GetAsyncKeyState(VK_CONTROL) & 0x8000) && !(GetAsyncKeyState(VK_LWIN) & 0x8000);
            if (!self->switchError && (self->switchResults < step + 1 || !modifiersReleased) && attempt < 30) {
                self->checkSwitchStep(step, attempt + 1); return;
            }
            const bool expectEnglish = step % 2 == 0;
            const QString actual = layoutString(self->targetLayout());
            const bool passed = !self->switchError && modifiersReleased && self->switchResults == step + 1 && self->targetText().isEmpty()
                && (expectEnglish ? actual == QStringLiteral("04090409") : self->targetLayout() == self->chinese.layout);
            self->steps.append(QJsonObject{{"operation", expectEnglish ? "hotkeyToUS" : "hotkeyToChinese"},
                {"passed", passed}, {"layout", actual}, {"confirmedSwitches", self->switchResults}, {"holdMs", step == 0 ? 2300 : 150}, {"modifiersReleased", modifiersReleased}, {"spaceLeak", !self->targetText().isEmpty()}, {"inputState", self->inputState()}});
            if (!passed) { self->finish(false, QStringLiteral("Hotkey switching or repeat suppression failed.")); return; }
            if (step == 1) self->typeChinese();
            else if (step < 2) self->switchStep(step + 1);
            else self->typeEnglish();
        });
    }
    void typeChinese() {
        if (!chinese.id.contains(QStringLiteral("FA550B04-5AD7-411F-A5AC-CA038EC515D7"))) {
            steps.append(QJsonObject{{"operation", "chineseComposition"}, {"skipped", true}, {"reason", "This text assertion requires Microsoft Pinyin."}});
            switchStep(2); return;
        }
        if (!targetFocused()) { finish(false, QStringLiteral("Test editor lost focus.")); return; }
        const WORD keys[] = {'N', 'I', 'H', 'A', 'O', VK_SPACE};
        INPUT inputs[12]{};
        for (int i = 0; i < 6; ++i) {
            inputs[i * 2].type = inputs[i * 2 + 1].type = INPUT_KEYBOARD;
            inputs[i * 2].ki.wScan = inputs[i * 2 + 1].ki.wScan = static_cast<WORD>(MapVirtualKeyW(keys[i], MAPVK_VK_TO_VSC));
            inputs[i * 2].ki.dwFlags = KEYEVENTF_SCANCODE;
            inputs[i * 2 + 1].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
        }
        const bool sent = SendInput(12, inputs, sizeof(INPUT)) == 12;
        QTimer::singleShot(400, qApp, [self = shared_from_this(), sent] {
            const bool passed = sent && self->targetText() == QStringLiteral("你好");
            self->steps.append(QJsonObject{{"operation", "chineseComposition"}, {"passed", passed}, {"actual", self->targetText()}, {"inputState", self->inputState()}});
            if (!passed) { self->finish(false, QStringLiteral("Chinese composition mismatch.")); return; }
            if (self->externalWindow) SendMessageTimeoutW(self->targetWindow(), WM_SETTEXT, 0, reinterpret_cast<LPARAM>(L""), SMTO_ABORTIFHUNG, 500, nullptr);
            else self->text->clear();
            self->switchStep(2);
        });
    }
    void rapidToggle() {
        if (externalWindow) SendMessageTimeoutW(targetWindow(), WM_SETTEXT, 0, reinterpret_cast<LPARAM>(L""), SMTO_ABORTIFHUNG, 500, nullptr);
        else text->clear();
        const int before = switchResults;
        const bool sent = sendShortcut() && sendShortcut();
        QTimer::singleShot(700, qApp, [self = shared_from_this(), before, sent] {
            const bool passed = sent && !self->switchError && self->switchResults > before && self->switchResults <= before + 2
                && layoutString(self->targetLayout()) == QStringLiteral("04090409") && self->targetText().isEmpty();
            self->steps.append(QJsonObject{{"operation", "rapidDoubleToggle"}, {"passed", passed}, {"confirmedSwitches", self->switchResults - before}, {"inputState", self->inputState()}, {"spaceLeak", !self->targetText().isEmpty()}});
            if (!passed) { self->finish(false, QStringLiteral("Rapid double-toggle final state mismatch.")); return; }
            self->typeEnglish(true);
        });
    }
    void typeEnglish(bool afterRapid = false) {
        if (!targetFocused()) { finish(false, QStringLiteral("Test editor lost focus.")); return; }
        INPUT inputs[12]{};
        const WORD keys[] = {'A', 'B', 'C', '1', '2', '3'};
        for (int i = 0; i < 6; ++i) {
            inputs[i * 2].type = inputs[i * 2 + 1].type = INPUT_KEYBOARD;
            inputs[i * 2].ki.wScan = inputs[i * 2 + 1].ki.wScan = static_cast<WORD>(MapVirtualKeyW(keys[i], MAPVK_VK_TO_VSC));
            inputs[i * 2].ki.dwFlags = KEYEVENTF_SCANCODE;
            inputs[i * 2 + 1].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
        }
        const bool sent = SendInput(12, inputs, sizeof(INPUT)) == 12;
        QTimer::singleShot(200, qApp, [self = shared_from_this(), sent, afterRapid] {
            const bool passed = sent && self->targetText() == QStringLiteral("abc123");
            self->steps.append(QJsonObject{{"operation", afterRapid ? "englishAfterRapidToggle" : "directEnglishInput"}, {"passed", passed}, {"actual", self->targetText()}, {"inputState", self->inputState()}});
            if (!passed || afterRapid) self->finish(passed, passed ? QString() : QStringLiteral("English typing mismatch."));
            else self->rapidToggle();
        });
    }
};
}

void SmokeTest::start(QQuickWindow *window, AppController *controller, const QString &directory,
                      const QString &mode, std::shared_ptr<QStringList> warnings)
{
    if (!QDir().mkpath(directory)) { QApplication::exit(2); return; }
    auto test = std::make_shared<TestRun>();
    test->window = window; test->controller = controller; test->directory = directory;
    test->mode = mode; test->warnings = std::move(warnings);
    QTimer::singleShot(500, qApp, [test] { test->capture(0); });
    QTimer::singleShot(20000, qApp, [] { QApplication::exit(3); });
}

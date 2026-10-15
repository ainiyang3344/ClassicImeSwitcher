#include "AppController.h"
#include "SmokeTest.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QMutex>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTemporaryDir>
#include <QTimer>
#include <memory>

namespace {
QString diagnosticPath;
QMutex diagnosticMutex;
void diagnosticMessage(QtMsgType, const QMessageLogContext &, const QString &message)
{
    QMutexLocker lock(&diagnosticMutex);
    QFile file(diagnosticPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append)) file.write(message.toUtf8() + '\n');
    OutputDebugStringW(reinterpret_cast<LPCWSTR>((message + u'\n').utf16()));
}
QIcon appIcon()
{
    QPixmap pixmap(64, 64); pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap); painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen); painter.setBrush(QColor(QStringLiteral("#4169ed"))); painter.drawRoundedRect(2, 2, 60, 60, 14, 14);
    QFont font(QStringLiteral("Segoe UI")); font.setPixelSize(41); font.setBold(true); painter.setFont(font); painter.setPen(Qt::white);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, QStringLiteral("A")); painter.end();
    return QIcon(pixmap);
}
}

int main(int argc, char **argv)
{
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int result = 0;
    {
        QApplication app(argc, argv);
        OwnInputLanguageFilter inputLanguageFilter;
        app.installNativeEventFilter(&inputLanguageFilter);
        app.setOrganizationName(QStringLiteral("LYLab"));
        app.setApplicationName(QStringLiteral("ClassicImeSwitcher"));
        app.setApplicationDisplayName(QStringLiteral("经典输入法切换"));
        app.setApplicationVersion(QStringLiteral("0.1.0"));
        app.setQuitOnLastWindowClosed(false);
        app.setWindowIcon(appIcon());
        QQuickStyle::setStyle(QStringLiteral("Basic"));
        QCommandLineParser parser;
        parser.setApplicationDescription(QStringLiteral("独立的经典输入法切换与输入法管理工具"));
        parser.addHelpOption(); parser.addVersionOption();
        parser.addOption({QStringLiteral("background"), QStringLiteral("启动后留在托盘")});
        parser.addOption({QStringLiteral("smoke-test"), QStringLiteral("只检查 QML 页面并保存截图，不修改输入法"), QStringLiteral("directory")});
        parser.addOption({QStringLiteral("verify-switch"), QStringLiteral("在独立测试窗口模拟热键并验证切换"), QStringLiteral("directory")});
        parser.addOption({QStringLiteral("verify-management"), QStringLiteral("验证美式键盘启用/禁用并恢复原状态"), QStringLiteral("directory")});
        parser.addOption({QStringLiteral("verify-external"), QStringLiteral("在独立 Win32 进程中验证切换与英文输入"), QStringLiteral("directory")});
        parser.process(app);
        QString mode; QString output;
        for (const auto &pair : {std::pair{"smoke-test", "smoke"}, {"verify-switch", "switch"}, {"verify-management", "management"}, {"verify-external", "external"}}) {
            if (parser.isSet(QString::fromLatin1(pair.first))) {
                if (!mode.isEmpty()) return 2;
                mode = QString::fromLatin1(pair.second); output = QDir::fromNativeSeparators(parser.value(QString::fromLatin1(pair.first)));
            }
        }
        const bool testing = !mode.isEmpty();
        if (testing) {
            QDir().mkpath(output);
            diagnosticPath = output + QStringLiteral("/qt.log");
            QFile::remove(diagnosticPath);
            qInstallMessageHandler(diagnosticMessage);
        }
        QTemporaryDir temporary;
        const QString stateDirectory = testing ? temporary.path() : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        QLocalServer server;
        const QString serverName = QStringLiteral("LYLab.ClassicImeSwitcher.v1.") + WindowsInput::userSid();
        if (!testing) {
            server.setSocketOptions(QLocalServer::UserAccessOption);
            if (!server.listen(serverName)) {
                QLocalSocket existing; existing.connectToServer(serverName);
                if (existing.waitForConnected(500)) { existing.write("show"); existing.waitForBytesWritten(500); return 0; }
                QLocalServer::removeServer(serverName);
                if (!server.listen(serverName)) return 1;
            }
        }
        AppController controller(stateDirectory, mode != QStringLiteral("management") && testing);
        const bool hasTray = !testing && QSystemTrayIcon::isSystemTrayAvailable();
        auto warnings = std::make_shared<QStringList>();
        QQmlApplicationEngine engine;
        QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app, [warnings](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings->append(error.toString());
        });
        QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
        engine.setInitialProperties({{"backend", QVariant::fromValue(&controller)}, {"trayAvailable", hasTray}, {"startHidden", parser.isSet(QStringLiteral("background")) && !testing}});
        engine.loadFromModule(QStringLiteral("ClassicIme"), QStringLiteral("Main"));
        if (engine.rootObjects().isEmpty()) {
            if (testing) {
                QFile report(output + QStringLiteral("/report.json"));
                if (report.open(QIODevice::WriteOnly)) report.write(QJsonDocument(QJsonObject{{"passed", false}, {"error", "QML initialization failed"}, {"qmlWarnings", QJsonArray::fromStringList(*warnings)}}).toJson());
            }
            return 1;
        }
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().front());
        if (!window) return 1;
        QMenu menu;
        auto *show = menu.addAction(QStringLiteral("打开设置"));
        auto *toggle = menu.addAction(QStringLiteral("开启经典切换")); toggle->setCheckable(true); toggle->setChecked(controller.enabled());
        menu.addSeparator(); auto *quit = menu.addAction(QStringLiteral("退出"));
        const auto showWindow = [window] {
            if (window->visibility() == QWindow::Minimized) window->showNormal();
            else window->show();
            window->raise(); window->requestActivate();
        };
        QObject::connect(show, &QAction::triggered, &app, showWindow);
        QObject::connect(toggle, &QAction::triggered, &controller, &AppController::setEnabled);
        QObject::connect(&controller, &AppController::settingsChanged, &app, [&controller, toggle] { toggle->setChecked(controller.enabled()); });
        QObject::connect(quit, &QAction::triggered, &app, &QApplication::quit);
        QSystemTrayIcon tray(app.windowIcon());
        tray.setToolTip(QStringLiteral("经典输入法切换")); tray.setContextMenu(&menu);
        QObject::connect(&tray, &QSystemTrayIcon::activated, &app, [showWindow](QSystemTrayIcon::ActivationReason reason) { if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) showWindow(); });
        QObject::connect(&server, &QLocalServer::newConnection, &app, [&server, showWindow] {
            while (auto *socket = server.nextPendingConnection()) { socket->deleteLater(); showWindow(); }
        });
        if (hasTray) tray.show();
        if (testing) SmokeTest::start(window, &controller, output, mode, warnings);
        result = app.exec();
    }
    if (SUCCEEDED(com)) CoUninitialize();
    return result;
}

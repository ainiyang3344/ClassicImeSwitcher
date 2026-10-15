#pragma once
#include <QString>
#include <memory>

class AppController;
class QQuickWindow;
class SmokeTest {
public:
    static void start(QQuickWindow *window, AppController *controller, const QString &directory,
                      const QString &mode, std::shared_ptr<QStringList> warnings);
};

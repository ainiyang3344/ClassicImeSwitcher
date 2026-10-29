#pragma once
#include <QString>

namespace StartupTask {
QString definition(const QString &executable, const QString &sid);
bool enabled();
bool setEnabled(bool enabled, QString *error);
}

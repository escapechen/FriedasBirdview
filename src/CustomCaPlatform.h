#pragma once

#include <QString>

namespace CustomCaPlatform {
bool install(const QString &id, const QString &certificatePath, QString *error);
bool remove(const QString &id, QString *error);
}

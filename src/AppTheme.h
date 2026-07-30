#pragma once

class QApplication;

namespace AppTheme {

// Keeps the settings window, tray dialogs, and overlay visually consistent on
// KDE and Windows without depending on either desktop's current color scheme.
void apply(QApplication &application);

} // namespace AppTheme

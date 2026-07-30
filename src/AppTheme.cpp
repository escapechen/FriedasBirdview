#include "AppTheme.h"

#include <QApplication>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>

namespace {

QString applicationStyleSheet()
{
    return QStringLiteral(R"(
        QWidget {
            color: #eef3f7;
        }

        QDialog {
            background: #0e1620;
        }

        QTabWidget::pane {
            background: #121c26;
            border: 1px solid #304151;
            border-radius: 10px;
            top: -1px;
        }

        QTabBar::tab {
            background: #17212c;
            color: #b8c6d2;
            border: 1px solid #304151;
            border-bottom: none;
            border-top-left-radius: 8px;
            border-top-right-radius: 8px;
            min-height: 28px;
            padding: 5px 14px;
            margin-right: 3px;
        }

        QTabBar::tab:hover {
            background: #233244;
            color: #f5f8fb;
        }

        QTabBar::tab:selected {
            background: #1d639c;
            color: white;
            border-color: #3394d2;
        }

        QGroupBox {
            background: #141f29;
            border: 1px solid #354857;
            border-radius: 10px;
            font-weight: 600;
            margin-top: 12px;
            padding-top: 4px;
        }

        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top center;
            color: #f2f6fa;
            padding: 0 8px;
        }

        QLineEdit, QComboBox, QSpinBox, QListWidget {
            background: #0c1218;
            border: 1px solid #435667;
            border-radius: 7px;
            min-height: 28px;
            padding: 1px 8px;
            selection-background-color: #267db7;
            selection-color: white;
        }

        QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QListWidget:focus {
            border: 1px solid #2996d6;
        }

        QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled {
            background: #101820;
            color: #738493;
            border-color: #2d3c49;
        }

        QComboBox::drop-down {
            border: none;
            width: 26px;
        }

        QComboBox::down-arrow {
            width: 8px;
            height: 8px;
        }

        QSpinBox::up-button, QSpinBox::down-button {
            width: 20px;
            border: none;
        }

        QPushButton {
            background: #273440;
            border: 1px solid #506372;
            border-radius: 7px;
            min-height: 28px;
            padding: 2px 12px;
        }

        QPushButton:hover {
            background: #34495a;
            border-color: #7191a8;
        }

        QPushButton:pressed {
            background: #1f2a34;
        }

        QPushButton:disabled {
            background: #1a232c;
            color: #72818e;
            border-color: #35434e;
        }

        QPushButton#primaryButton {
            background: #1c6ca8;
            border-color: #3aa2df;
            color: white;
            font-weight: 600;
        }

        QPushButton#primaryButton:hover {
            background: #2680be;
            border-color: #72c2ee;
        }

        QListWidget::item {
            padding: 4px 6px;
            border-radius: 4px;
        }

        QListWidget::item:hover {
            background: #1c2b38;
        }

        QListWidget::item:selected {
            background: #245f89;
            color: white;
        }

        QCheckBox {
            spacing: 6px;
        }

        QCheckBox::indicator {
            width: 17px;
            height: 17px;
            border: 1px solid #5a7182;
            border-radius: 5px;
            background: #17212b;
        }

        QCheckBox::indicator:hover {
            border-color: #66b8e7;
        }

        QCheckBox::indicator:checked {
            background: #247eb5;
            border-color: #65c2ef;
        }

        QRadioButton {
            spacing: 7px;
        }

        QRadioButton::indicator {
            width: 17px;
            height: 17px;
            border: 1px solid #5a7182;
            border-radius: 9px;
            background: #17212b;
        }

        QRadioButton::indicator:checked {
            border: 5px solid #2786c3;
        }

        QSlider::groove:horizontal {
            height: 6px;
            border-radius: 3px;
            background: #354553;
        }

        QSlider::sub-page:horizontal {
            border-radius: 3px;
            background: #278bc5;
        }

        QSlider::handle:horizontal {
            background: #d1e8f5;
            border: 1px solid #7db9d8;
            border-radius: 8px;
            width: 16px;
            margin: -5px 0;
        }

        QScrollBar:vertical {
            background: transparent;
            width: 12px;
            margin: 4px 1px;
        }

        QScrollBar::handle:vertical {
            background: #4c6474;
            border-radius: 5px;
            min-height: 24px;
        }

        QScrollBar::handle:vertical:hover {
            background: #638196;
        }

        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
        }

        QLabel#verificationStatus[status="success"] {
            background: #173e2c;
            border: 1px solid #3b915f;
            border-radius: 7px;
            color: #c5f3d5;
            padding: 7px 9px;
        }

        QLabel#verificationStatus[status="error"] {
            background: #542325;
            border: 1px solid #aa5254;
            border-radius: 7px;
            color: #ffc3bd;
            padding: 7px 9px;
        }

        QLabel#verificationStatus[status="progress"] {
            background: #1c3650;
            border: 1px solid #3b78a5;
            border-radius: 7px;
            color: #d6edff;
            padding: 7px 9px;
        }

        QLabel#dialogStatus {
            color: #bdcbd6;
            padding: 2px 1px;
        }

        QToolTip {
            background: #22313d;
            color: #f4f8fb;
            border: 1px solid #608196;
            border-radius: 5px;
            padding: 5px;
        }

        QMenu {
            background: #18232e;
            border: 1px solid #405564;
            padding: 5px;
        }

        QMenu::item {
            border-radius: 5px;
            padding: 6px 24px 6px 18px;
        }

        QMenu::item:selected {
            background: #275d84;
        }
    )");
}

void setPaletteColor(QPalette *palette, QPalette::ColorRole role, const QColor &color)
{
    palette->setColor(QPalette::Active, role, color);
    palette->setColor(QPalette::Inactive, role, color);
}

} // namespace

namespace AppTheme {

void apply(QApplication &application)
{
    if (QStyle *style = QStyleFactory::create(QStringLiteral("Fusion"))) {
        application.setStyle(style);
    }

    QPalette palette;
    setPaletteColor(&palette, QPalette::Window, QColor(QStringLiteral("#0e1620")));
    setPaletteColor(&palette, QPalette::WindowText, QColor(QStringLiteral("#eef3f7")));
    setPaletteColor(&palette, QPalette::Base, QColor(QStringLiteral("#0c1218")));
    setPaletteColor(&palette, QPalette::AlternateBase, QColor(QStringLiteral("#17232e")));
    setPaletteColor(&palette, QPalette::Text, QColor(QStringLiteral("#eef3f7")));
    setPaletteColor(&palette, QPalette::Button, QColor(QStringLiteral("#273440")));
    setPaletteColor(&palette, QPalette::ButtonText, QColor(QStringLiteral("#eef3f7")));
    setPaletteColor(&palette, QPalette::Highlight, QColor(QStringLiteral("#247eb5")));
    setPaletteColor(&palette, QPalette::HighlightedText, QColor(Qt::white));
    setPaletteColor(&palette, QPalette::ToolTipBase, QColor(QStringLiteral("#22313d")));
    setPaletteColor(&palette, QPalette::ToolTipText, QColor(QStringLiteral("#f4f8fb")));
    setPaletteColor(&palette, QPalette::PlaceholderText, QColor(QStringLiteral("#8b9ca9")));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(QStringLiteral("#738493")));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(QStringLiteral("#738493")));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(QStringLiteral("#738493")));
    application.setPalette(palette);
    application.setStyleSheet(applicationStyleSheet());
}

} // namespace AppTheme

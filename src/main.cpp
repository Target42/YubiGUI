#include "MainWindow.h"

#include "ConsoleProcess.h"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QIcon>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>

#include <cstring>

namespace {

void applyAppearance()
{
    if (QFontDatabase::families().contains(QStringLiteral("Segoe UI"), Qt::CaseInsensitive)) {
        QFont font(QStringLiteral("Segoe UI"));
        font.setPointSize(10);
        QApplication::setFont(font);
    }

    if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        QApplication::setStyle(fusion);

    QPalette palette;
    const QColor window(0xF3, 0xF5, 0xF7);
    const QColor ink(0x1A, 0x23, 0x30);
    const QColor muted(0x5C, 0x6B, 0x7A);
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, ink);
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::AlternateBase, QColor(0xF7, 0xF9, 0xFA));
    palette.setColor(QPalette::Text, ink);
    palette.setColor(QPalette::Button, Qt::white);
    palette.setColor(QPalette::ButtonText, ink);
    palette.setColor(QPalette::Highlight, QColor(0x2F, 0x6F, 0x4E));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::PlaceholderText, QColor(0x8A, 0x96, 0xA3));
    palette.setColor(QPalette::Mid, QColor(0xD0, 0xD7, 0xDE));
    palette.setColor(QPalette::Disabled, QPalette::Text, muted);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, muted);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, muted);
    QApplication::setPalette(palette);

    qApp->setStyleSheet(QString::fromUtf8(R"css(
QMainWindow { background: #F3F5F7; }
QWidget#central { background: #F3F5F7; }
QDialog { background: #F7F8FA; }

QWidget#nav { background: #1C2832; }
QLabel#brand {
    color: #F4F7F5;
    background: transparent;
    font-size: 15px;
    font-weight: 600;
    padding: 0;
}
QListWidget#pages {
    background: transparent;
    border: none;
    outline: none;
    font-size: 13px;
    padding: 8px 8px 12px 8px;
}
QListWidget#pages::item {
    color: #D5DEE6;
    padding: 8px 12px;
    margin: 2px 4px;
    border-radius: 7px;
}
QListWidget#pages::item:selected {
    background: #2F6F4E;
    color: #FFFFFF;
}
QListWidget#pages::item:hover:!selected {
    background: #2A3844;
}

QWidget#header {
    background: #FFFFFF;
    border-bottom: 1px solid #E4E8EC;
}
QLabel#deviceTitle {
    color: #1A2330;
    background: transparent;
    font-size: 18px;
    font-weight: 600;
}
QLabel#caption {
    color: #6B7785;
    background: transparent;
    font-size: 11px;
}
QLabel#value {
    color: #1A2330;
    background: transparent;
}
QLabel#section {
    color: #1A2330;
    background: transparent;
    font-size: 14px;
    font-weight: 600;
}
QLabel#note {
    color: #3D4A55;
    background: #F4F7F5;
    border: 1px solid #E1E8E4;
    border-radius: 8px;
    padding: 10px 12px;
}
QLabel#warning {
    color: #7A5B1E;
    background: #FBF6EC;
    border: 1px solid #EAD9B4;
    border-radius: 8px;
    padding: 8px 12px;
}
QLabel#bridge {
    color: #6B7785;
    padding-right: 8px;
}

QWidget#card {
    background: #FFFFFF;
    border: 1px solid #E4E8EC;
    border-radius: 10px;
}

QPushButton {
    background: #FFFFFF;
    border: 1px solid #D0D7DE;
    border-radius: 6px;
    padding: 5px 12px;
    min-height: 18px;
    color: #1A2330;
}
QPushButton:hover {
    background: #F3F6F8;
    border-color: #B7C2CC;
}
QPushButton:pressed { background: #E7ECF0; }
QPushButton:disabled {
    color: #9AA6B2;
    background: #F6F8F9;
    border-color: #E2E6EA;
}
QPushButton#primary {
    background: #2F6F4E;
    border-color: #275C41;
    color: #FFFFFF;
    font-weight: 600;
}
QPushButton#primary:hover { background: #275C41; }
QPushButton#primary:pressed { background: #1E4A34; }
QPushButton#primary:disabled {
    background: #A9C7B6;
    border-color: #A9C7B6;
    color: #FFFFFF;
}
QPushButton#danger {
    background: #FFFFFF;
    border-color: #E3B4B4;
    color: #9B2C2C;
}
QPushButton#danger:hover {
    background: #FDF2F2;
    border-color: #D98989;
}
QPushButton#danger:pressed { background: #F8E6E6; }
QPushButton#danger:disabled {
    color: #C9A8A8;
    border-color: #EADCDC;
    background: #FBF8F8;
}
QPushButton#help {
    background: transparent;
    border: 1px solid #D0D7DE;
    border-radius: 6px;
    color: #5C6B7A;
    font-weight: 700;
    min-width: 28px;
    max-width: 28px;
    padding: 5px 0;
}
QPushButton#help:hover {
    background: #F3F6F8;
    color: #1A2330;
    border-color: #B7C2CC;
}
QPushButton#help:pressed { background: #E7ECF0; }

QLineEdit, QComboBox, QSpinBox, QPlainTextEdit {
    background: #FFFFFF;
    border: 1px solid #D0D7DE;
    border-radius: 6px;
    padding: 5px 8px;
    selection-background-color: #D7EBDD;
    selection-color: #1A2330;
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QPlainTextEdit:focus {
    border-color: #2F6F4E;
}
QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled, QPlainTextEdit:disabled {
    background: #F4F6F8;
    color: #8A96A3;
}

QTableWidget {
    background: #FFFFFF;
    alternate-background-color: #F7F9FA;
    border: 1px solid #E2E6EA;
    border-radius: 8px;
    gridline-color: transparent;
    selection-background-color: #E3F1E8;
    selection-color: #1A2330;
}
QTableWidget::item { padding: 4px 8px; }
QTableWidget::item:selected {
    background: #E3F1E8;
    color: #1A2330;
}
QHeaderView::section {
    background: #F7F8FA;
    color: #5C6B7A;
    border: none;
    border-bottom: 1px solid #E2E6EA;
    padding: 7px 8px;
    font-weight: 600;
}

QCheckBox { spacing: 8px; padding: 3px 0; }

QStatusBar {
    background: #F3F5F7;
    color: #5C6B7A;
    border-top: 1px solid #E4E8EC;
}
QStatusBar::item { border: none; }
)css"));
}

} // namespace

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    if (argc >= 3 && std::strcmp(argv[1], "--fido-pin-relay") == 0)
        return runFidoPinRelay(argv[2]);
#else
    Q_UNUSED(argc);
    Q_UNUSED(argv);
#endif

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("yubigui"));
    QApplication::setApplicationDisplayName(QStringLiteral("YubiGUI"));
    QApplication::setApplicationVersion(QStringLiteral(YUBIGUI_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/yubigui.png")));
    QApplication::setDesktopFileName(QStringLiteral("yubigui"));
    applyAppearance();

    MainWindow window;
    window.show();
    return app.exec();
}

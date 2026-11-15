# YubiGUI — Qt Widgets. Der Key wird über USB oder ein Lesegerät angesprochen.
# Die NFC-Schalter der Anwendungen werden über dieselbe Verbindung mit verwaltet.
# qmake, absichtlich kein CMake.
#
# Kommandozeile mit dem MinGW-Kit von Qt 6.10:
#   mkdir build
#   cd build
#   D:\Qt\6.10.0\mingw_64\bin\qmake.exe ..\YubiGUI.pro -spec win32-g++
#   D:\Qt\Tools\mingw1310_64\bin\mingw32-make.exe
#
# Qt Creator: diese Datei öffnen und ein Qt-6.10-Kit wählen.
# Das Kit steht hier nicht fest. MSVC und MinGW bauen dieselbe Datei.
#
# Linux, Qt 6.9.1 aus dem Installer unter ~/Qt:
#   mkdir -p build && cd build
#   ~/Qt/6.9.1/gcc_64/bin/qmake ../YubiGUI.pro
#   make

QT += widgets

CONFIG += c++17 warn_on
CONFIG -= app_bundle

msvc: QMAKE_CXXFLAGS += /utf-8
win32-g++: QMAKE_CXXFLAGS += -finput-charset=UTF-8
win32-clang-g++: QMAKE_CXXFLAGS += -finput-charset=UTF-8

TARGET = yubigui
TEMPLATE = app

SOURCES += \
    src/main.cpp \
    src/MainWindow.cpp \
    src/YkmanClient.cpp \
    src/Settings.cpp \
    src/ConsoleProcess.cpp

HEADERS += \
    src/MainWindow.h \
    src/YkmanClient.h \
    src/Settings.h \
    src/ConsoleProcess.h

unix: LIBS += -lutil

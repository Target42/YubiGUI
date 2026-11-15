#pragma once

#include <QString>
#include <QStringList>

struct ConsoleResult
{
    int exitCode = -1;
    QString output;
    QString failure;
};

// workDir leer: Arbeitsverzeichnis des Prozesses. tool steht in den Fehlermeldungen.
// promptHints, parallel zu lines: eine Zeile geht erst raus, wenn die Konsole
// den Hinweis enthält. Leer lässt die bisherige Reihenfolge.
ConsoleResult runOnConsole(const QString &program,
                           const QStringList &args,
                           const QStringList &lines,
                           int timeoutMs,
                           const QString &workDir = QString(),
                           const QString &tool = QStringLiteral("ykman"),
                           const QStringList &promptHints = QStringList());

#ifdef Q_OS_WIN
int runFidoPinRelay(const char *pipeName);
ConsoleResult runElevatedConsole(const QString &helperProgram,
                                 const QString &program,
                                 const QStringList &args,
                                 const QStringList &lines);
#endif

#include "Settings.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QSysInfo>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

QString configFile()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return dir + QStringLiteral("/yubigui.ini");
}

QString firstLine(const QString &text)
{
    QString line = text.trimmed();
    const int breakAt = line.indexOf(QLatin1Char('\n'));
    if (breakAt >= 0)
        line = line.left(breakAt).trimmed();
    if (line.size() > 180)
        line.resize(180);
    return line;
}

bool writeOverride(const QString &path, QString *error)
{
    const QString file = configFile();
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSettings settings(file, QSettings::IniFormat);
    if (path.isEmpty())
        settings.remove(QStringLiteral("ykman"));
    else
        settings.setValue(QStringLiteral("ykman"), path);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        if (error)
            *error = QStringLiteral("Der Pfad ließ sich nicht speichern.");
        return false;
    }
    return true;
}

bool probeYkman(const QString &program, QString *error)
{
    QProcess process;
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    process.start(program, {QStringLiteral("--color"), QStringLiteral("never"), QStringLiteral("version")});
    if (!process.waitForStarted(5000)) {
        if (error)
            *error = QStringLiteral("Die Datei ließ sich nicht starten.");
        return false;
    }
    if (!process.waitForFinished(30000)) {
        process.kill();
        process.waitForFinished(2000);
        if (error)
            *error = QStringLiteral("ykman hat nicht rechtzeitig geantwortet.");
        return false;
    }

    const QString output = QString::fromUtf8(process.readAllStandardOutput());
    const QString err = QString::fromUtf8(process.readAllStandardError());
    const QString combined = output + QLatin1Char('\n') + err;
    if (process.exitCode() == 0 && combined.contains(QLatin1String("ykman"), Qt::CaseInsensitive))
        return true;
    if (combined.contains(QLatin1String("No such option"), Qt::CaseInsensitive)
        && combined.contains(QLatin1String("--color"))) {
        if (error)
            *error = QStringLiteral("Diese Datei ist ykman 5. YubiGUI braucht ykman 6.");
        return false;
    }

    const QString line = firstLine(err.trimmed().isEmpty() ? output : err);
    if (error)
        *error = line.isEmpty() ? QStringLiteral("Die Datei antwortet nicht wie ykman.") : line;
    return false;
}

QString cleanedPath(QString path)
{
    path = path.trimmed();
    if (path.size() >= 2 && path.front() == QLatin1Char('"') && path.back() == QLatin1Char('"'))
        path = path.mid(1, path.size() - 2).trimmed();
    return path;
}

QString existingFile(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isFile())
        return {};
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

void addCandidate(QStringList *found, const QString &path)
{
    const QString file = existingFile(path);
    if (!file.isEmpty() && !found->contains(file))
        found->append(file);
}

bool acceptsYkman6(const QString &program)
{
    QProcess process;
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    process.start(program, {QStringLiteral("--color"), QStringLiteral("never"), QStringLiteral("version")});
    if (!process.waitForStarted(3000))
        return false;
    if (!process.waitForFinished(8000)) {
        process.kill();
        process.waitForFinished(1000);
        return false;
    }
    const QString combined = QString::fromUtf8(process.readAllStandardOutput())
                             + QLatin1Char('\n')
                             + QString::fromUtf8(process.readAllStandardError());
    return process.exitCode() == 0 && combined.contains(QLatin1String("ykman"), Qt::CaseInsensitive);
}

} // namespace

namespace Settings {

QString ykmanOverride()
{
    QSettings settings(configFile(), QSettings::IniFormat);
    return settings.value(QStringLiteral("ykman")).toString().trimmed();
}

QString resolveYkman()
{
    QStringList candidates;
    addCandidate(&candidates, ykmanOverride());
#ifndef Q_OS_WIN
    addCandidate(&candidates, QStringLiteral("/usr/local/bin/ykman"));
#endif
    const QString pathEnv = QString::fromLocal8Bit(qgetenv("PATH"));
    for (const QString &dir : pathEnv.split(QDir::listSeparator(), Qt::SkipEmptyParts)) {
        addCandidate(&candidates, QStandardPaths::findExecutable(QStringLiteral("ykman"), {dir}));
    }
#ifdef Q_OS_WIN
    addCandidate(&candidates, QStringLiteral("C:/Program Files/Yubico/YubiKey Manager CLI/ykman.exe"));
#endif

    for (const QString &candidate : candidates) {
        if (acceptsYkman6(candidate))
            return candidate;
    }
    return candidates.isEmpty() ? QString() : candidates.first();
}

QString resolveSshKeygen()
{
    QStringList candidates;
    const QString pathEnv = QString::fromLocal8Bit(qgetenv("PATH"));
    for (const QString &dir : pathEnv.split(QDir::listSeparator(), Qt::SkipEmptyParts)) {
        addCandidate(&candidates, QStandardPaths::findExecutable(QStringLiteral("ssh-keygen"), {dir}));
    }
#ifdef Q_OS_WIN
    addCandidate(&candidates, QStringLiteral("C:/Windows/System32/OpenSSH/ssh-keygen.exe"));
#else
    addCandidate(&candidates, QStringLiteral("/usr/bin/ssh-keygen"));
#endif
    return candidates.isEmpty() ? QString() : candidates.first();
}

QString missingSshKeygenText()
{
#ifdef Q_OS_WIN
    return QStringLiteral(
        "ssh-keygen wurde nicht gefunden. Unter Windows ist das der optionale OpenSSH-Client.");
#else
    return QStringLiteral(
        "ssh-keygen wurde nicht gefunden. Das Paket heißt openssh-client.");
#endif
}

QString missingYkmanText()
{
    const QString saved = ykmanOverride();
    if (!saved.isEmpty() && !QFileInfo::exists(saved))
        return QStringLiteral("Der gespeicherte Pfad zu ykman fehlt.");

#ifdef Q_OS_WIN
    return QStringLiteral(
        "ykman wurde nicht gefunden. Der Installer des YubiKey Manager CLI legt ykman.exe ab.");
#else
    return QStringLiteral(
        "ykman 6 wurde nicht gefunden. Unter „Pakete“ stehen die Befehle zum Kopieren.");
#endif
}

QString searchHint()
{
#ifdef Q_OS_WIN
    return QStringLiteral(
        "Ohne eigenen Pfad sucht YubiGUI zuerst über PATH und danach im Installationsordner "
        "des YubiKey Manager CLI. Ein eigener Pfad ersetzt diese Suche.");
#else
    return QStringLiteral(
        "Ohne eigenen Pfad sucht YubiGUI über PATH. Unter „Pakete“ stehen die Befehle "
        "für ykman 6 und den Smartcard-Dienst. Ein eigener Pfad ersetzt diese Suche.");
#endif
}

bool pcscdRunning()
{
#ifndef Q_OS_LINUX
    return true;
#else
    QProcess process;
    process.start(QStringLiteral("systemctl"), {QStringLiteral("is-active"), QStringLiteral("pcscd")});
    if (!process.waitForStarted(1500))
        return true;
    if (!process.waitForFinished(1500)) {
        process.kill();
        process.waitForFinished(500);
        return true;
    }
    return process.readAllStandardOutput().trimmed() == "active";
#endif
}

QString linuxInstallText(bool ykman, bool pcscd)
{
#ifndef Q_OS_LINUX
    Q_UNUSED(ykman);
    Q_UNUSED(pcscd);
    return {};
#else
    QStringList blocks;
    if (pcscd) {
        blocks << QStringLiteral(
            "sudo apt install pcscd libccid\n"
            "sudo systemctl enable --now pcscd");
    }
    if (ykman) {
        const QString arch = QSysInfo::currentCpuArchitecture() == QLatin1String("arm64")
                                 ? QStringLiteral("linux-arm64")
                                 : QStringLiteral("linux-x64");
        const QString file = QStringLiteral("yubikey-manager-6.0.0-beta.0-%1.tar.gz").arg(arch);
        const QString url = QStringLiteral(
                                "https://github.com/Yubico/yubikey-manager/releases/download/6.0.0-beta.0/%1")
                                .arg(file);
        blocks << QStringLiteral(
                    "curl -fsSL -o /tmp/ykman.tgz \"%1\"\n"
                    "rm -rf /tmp/ykman6\n"
                    "mkdir -p /tmp/ykman6\n"
                    "tar -xzf /tmp/ykman.tgz -C /tmp/ykman6\n"
                    "sudo install -m 755 /tmp/ykman6/yubikey-manager-6.0.0-beta.0-%2/ykman /usr/local/bin/ykman")
                    .arg(url, arch);
    }
    return blocks.join(QStringLiteral("\n\n"));
#endif
}

bool storeYkman(const QString &path, QString *error)
{
    const QString cleaned = cleanedPath(path);
    if (cleaned.isEmpty())
        return writeOverride(QString(), error);

    const QFileInfo info(cleaned);
    if (!info.exists()) {
        if (error)
            *error = QStringLiteral("Diese Datei gibt es nicht.");
        return false;
    }
    if (!info.isFile()) {
        if (error)
            *error = QStringLiteral("Das ist ein Ordner.");
        return false;
    }
#ifndef Q_OS_WIN
    if (!info.isExecutable()) {
        if (error)
            *error = QStringLiteral("Diese Datei ist nicht ausführbar.");
        return false;
    }
#endif
    const QString absolute = info.absoluteFilePath();
    if (QFileInfo(ykmanOverride()).absoluteFilePath() == absolute)
        return true;
    if (!probeYkman(absolute, error))
        return false;
    return writeOverride(absolute, error);
}

} // namespace Settings

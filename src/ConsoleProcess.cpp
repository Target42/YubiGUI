#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000006
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

#include "ConsoleProcess.h"

#include <QRegularExpression>

#include <mutex>
#include <string>
#include <thread>

#ifdef Q_OS_UNIX
#include <pty.h>
#include <errno.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <QDateTime>
#include <QVector>
#endif

namespace {

void wipe(QString &text)
{
    text.fill(QLatin1Char('0'));
    text.clear();
}

void wipe(QByteArray &bytes)
{
    if (!bytes.isEmpty())
        bytes.fill('\0');
    bytes.clear();
}

QString stripConsole(QString text)
{
    text.remove(QRegularExpression(QStringLiteral("\x1B\\][^\x07]*\x07")));
    text.remove(QRegularExpression(QStringLiteral("\x1B\\[[0-9;?]*[ -/]*[@-~]")));
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    text.remove(QLatin1Char('\r'));
    return text.trimmed();
}

bool consolePrompt(const QString &text)
{
    if (text.size() < 2)
        return false;
    if (text.endsWith(QLatin1Char(':'))
        || text.endsWith(QLatin1String("[y/N]"), Qt::CaseInsensitive))
        return true;
    const int pin = text.lastIndexOf(QLatin1String("PIN:"));
    return pin >= 0 && text.size() - pin <= 16;
}

// ykman 6 reads a PIN through rpassword. That switches the console to raw
// mode and drops every key that was typed before the switch. The line is
// therefore held back until the prompt is on screen and has settled.
class PromptGate
{
public:
    explicit PromptGate(const QStringList &lines, const QStringList &hints = {})
        : m_lines(lines)
        , m_hints(hints)
    {
    }

    ~PromptGate()
    {
        for (QString &line : m_lines)
            wipe(line);
        wipe(m_answered);
    }

    bool pending() const { return m_next < m_lines.size(); }
    bool sent() const { return m_next > 0; }

    QString takeIfReady(const QByteArray &output, qint64 nowMs)
    {
        QString line;
        if (output.size() != m_lastSize) {
            m_lastSize = output.size();
            m_quietAt = nowMs;
            return line;
        }
        if (nowMs - m_quietAt < 250)
            return line;
        QString text = stripConsole(QString::fromUtf8(output));
        if (!m_hints.isEmpty() && text != m_answered
            && text.endsWith(QLatin1String("Overwrite (y/n)?"), Qt::CaseInsensitive)) {
            line = QStringLiteral("n");
            wipe(m_answered);
            m_answered = text;
            wipe(text);
            return line;
        }
        if (!pending()) {
            wipe(text);
            return line;
        }
        const bool prompt = consolePrompt(text);
        if (prompt && text != m_answered) {
            int index = m_next;
            if (!m_hints.isEmpty()) {
                index = -1;
                const int last = m_lines.size() < m_hints.size() ? int(m_lines.size()) : int(m_hints.size());
                for (int i = m_next; i < last; ++i) {
                    if (m_hints.at(i).isEmpty()
                        || text.contains(m_hints.at(i), Qt::CaseInsensitive)) {
                        index = i;
                        break;
                    }
                }
                if (index < 0) {
                    wipe(text);
                    return line;
                }
            }
            line = std::move(m_lines[index]);
            m_lines[index].clear();
            m_next = index + 1;
            wipe(m_answered);
            m_answered = text;
        }
        wipe(text);
        return line;
    }

private:
    QStringList m_lines;
    QStringList m_hints;
    QString m_answered;
    int m_next = 0;
    qsizetype m_lastSize = -1;
    qint64 m_quietAt = 0;
};

#ifdef Q_OS_WIN
QString quoteArg(const QString &arg)
{
    if (arg.isEmpty())
        return QStringLiteral("\"\"");
    if (!arg.contains(QLatin1Char(' ')) && !arg.contains(QLatin1Char('"')))
        return arg;
    QString quoted;
    quoted.reserve(arg.size() + 2);
    quoted += QLatin1Char('"');
    for (const QChar ch : arg) {
        if (ch == QLatin1Char('"'))
            quoted += QLatin1String("\\\"");
        else
            quoted += ch;
    }
    quoted += QLatin1Char('"');
    return quoted;
}

QString windowsError(const QString &what)
{
    return QStringLiteral("%1 (Windows-Fehler %2).").arg(what).arg(GetLastError());
}

bool writeAll(HANDLE handle, const char *data, qint64 size)
{
    qint64 sent = 0;
    while (sent < size) {
        DWORD chunk = 0;
        if (!WriteFile(handle, data + sent, DWORD(size - sent), &chunk, nullptr) || chunk == 0)
            return false;
        sent += chunk;
    }
    return true;
}

bool writeBlock(HANDLE pipe, const QByteArray &bytes)
{
    const quint32 size = quint32(bytes.size());
    return writeAll(pipe, reinterpret_cast<const char *>(&size), sizeof(size))
           && (size == 0 || writeAll(pipe, bytes.constData(), size));
}

QByteArray readBlock(HANDLE pipe, bool *ok)
{
    quint32 size = 0;
    DWORD got = 0;
    if (!ReadFile(pipe, &size, sizeof(size), &got, nullptr) || got != sizeof(size) || size > 1024 * 1024) {
        *ok = false;
        return {};
    }
    QByteArray bytes(int(size), '\0');
    qint64 have = 0;
    while (have < size) {
        DWORD chunk = 0;
        if (!ReadFile(pipe, bytes.data() + have, DWORD(size - have), &chunk, nullptr) || chunk == 0) {
            *ok = false;
            wipe(bytes);
            return {};
        }
        have += chunk;
    }
    *ok = true;
    return bytes;
}

#endif

} // namespace

#ifdef Q_OS_WIN

ConsoleResult runOnConsole(const QString &program,
                           const QStringList &args,
                           const QStringList &lines,
                           int timeoutMs,
                           const QString &workDir,
                           const QString &tool,
                           const QStringList &promptHints)
{
    ConsoleResult result;
    const QString toolName = tool.isEmpty() ? QStringLiteral("ykman") : tool;
    SECURITY_ATTRIBUTES inherit;
    inherit.nLength = sizeof(inherit);
    inherit.lpSecurityDescriptor = nullptr;
    inherit.bInheritHandle = FALSE;

    HANDLE inputRead = nullptr;
    HANDLE inputWrite = nullptr;
    HANDLE outputRead = nullptr;
    HANDLE outputWrite = nullptr;
    if (!CreatePipe(&inputRead, &inputWrite, &inherit, 0)
        || !CreatePipe(&outputRead, &outputWrite, &inherit, 0)) {
        result.failure = windowsError(QStringLiteral("Die Konsole für %1 ließ sich nicht öffnen").arg(toolName));
        return result;
    }

    HPCON console = nullptr;
    const HRESULT created = CreatePseudoConsole({140, 40}, inputRead, outputWrite, 0, &console);
    CloseHandle(inputRead);
    CloseHandle(outputWrite);
    if (FAILED(created)) {
        CloseHandle(inputWrite);
        CloseHandle(outputRead);
        result.failure = QStringLiteral("Die Konsole für %1 ließ sich nicht öffnen (0x%2).")
                             .arg(toolName)
                             .arg(uint(created), 8, 16, QLatin1Char('0'));
        return result;
    }

    SIZE_T attributeSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);
    STARTUPINFOEXW startup = {};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(), 0, attributeSize));
    PROCESS_INFORMATION process = {};
    QByteArray output;

    const auto cleanup = [&]() {
        if (process.hThread)
            CloseHandle(process.hThread);
        if (process.hProcess)
            CloseHandle(process.hProcess);
        if (startup.lpAttributeList) {
            DeleteProcThreadAttributeList(startup.lpAttributeList);
            HeapFree(GetProcessHeap(), 0, startup.lpAttributeList);
        }
        if (console)
            ClosePseudoConsole(console);
        if (inputWrite)
            CloseHandle(inputWrite);
        if (outputRead)
            CloseHandle(outputRead);
        output.fill('\0');
    };

    if (!startup.lpAttributeList
        || !InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attributeSize)
        || !UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                      console, sizeof(console), nullptr, nullptr)) {
        result.failure = windowsError(QStringLiteral("Die Konsole für %1 ließ sich nicht vorbereiten").arg(toolName));
        cleanup();
        return result;
    }

    QString command = quoteArg(program);
    for (const QString &arg : args)
        command += QLatin1Char(' ') + quoteArg(arg);
    std::wstring commandLine = command.toStdWString();
    const std::wstring application = program.toStdWString();
    const std::wstring directory = workDir.toStdWString();
    const BOOL createdProcess = CreateProcessW(application.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                                               EXTENDED_STARTUPINFO_PRESENT, nullptr,
                                               workDir.isEmpty() ? nullptr : directory.c_str(),
                                               &startup.StartupInfo, &process);
    command.fill(QLatin1Char('0'));
    command.clear();
    for (wchar_t &character : commandLine)
        character = L'\0';
    commandLine.clear();
    if (!createdProcess) {
        result.failure = windowsError(QStringLiteral("%1 ließ sich nicht starten").arg(toolName));
        cleanup();
        return result;
    }

    std::mutex outputLock;
    std::thread reader([&output, &outputLock, outputRead]() {
        char buffer[4096];
        DWORD got = 0;
        while (ReadFile(outputRead, buffer, sizeof(buffer), &got, nullptr) && got > 0) {
            const std::lock_guard<std::mutex> lock(outputLock);
            output.append(buffer, int(got));
        }
    });

    PromptGate gate(lines, promptHints);
    const DWORD started = GetTickCount();
    while (WaitForSingleObject(process.hProcess, 50) == WAIT_TIMEOUT) {
        if (int(GetTickCount() - started) > timeoutMs) {
            TerminateProcess(process.hProcess, 1);
            result.failure = QStringLiteral("%1 hat nicht rechtzeitig geantwortet.").arg(toolName);
            break;
        }
        QByteArray snapshot;
        {
            const std::lock_guard<std::mutex> lock(outputLock);
            snapshot = output;
        }
        QString line = gate.takeIfReady(snapshot, qint64(GetTickCount()));
        wipe(snapshot);
        if (line.isEmpty())
            continue;
        QByteArray typed = line.toUtf8();
        wipe(line);
        typed += '\r';
        const bool wrote = writeAll(inputWrite, typed.constData(), typed.size());
        wipe(typed);
        if (!wrote) {
            result.failure = windowsError(QStringLiteral("Die Eingabe konnte nicht übergeben werden"));
            TerminateProcess(process.hProcess, 1);
            break;
        }
    }
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    result.exitCode = int(exitCode);
    CloseHandle(inputWrite);
    inputWrite = nullptr;
    ClosePseudoConsole(console);
    console = nullptr;
    reader.join();
    result.output = stripConsole(QString::fromUtf8(output));
    cleanup();
    return result;
}

int runFidoPinRelay(const char *pipeName)
{
    const HANDLE pipe = CreateFileA(pipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE)
        return 2;

    bool ok = true;
    const QByteArray programBytes = readBlock(pipe, &ok);
    QString program = QString::fromUtf8(programBytes);
    quint32 argc = 0;
    DWORD got = 0;
    if (!ok || !ReadFile(pipe, &argc, sizeof(argc), &got, nullptr) || got != sizeof(argc) || argc > 32)
        ok = false;
    QStringList args;
    for (quint32 index = 0; ok && index < argc; ++index) {
        QByteArray arg = readBlock(pipe, &ok);
        args << QString::fromUtf8(arg);
        wipe(arg);
    }
    quint32 pins = 0;
    if (!ok || !ReadFile(pipe, &pins, sizeof(pins), &got, nullptr) || got != sizeof(pins) || pins > 4)
        ok = false;
    QStringList lines;
    for (quint32 index = 0; ok && index < pins; ++index) {
        QByteArray pin = readBlock(pipe, &ok);
        lines << QString::fromUtf8(pin);
        wipe(pin);
    }

    ConsoleResult result;
    if (ok)
        result = runOnConsole(program, args, lines, 60000);
    else
        result.failure = QStringLiteral("Die erhöhte Sitzung hat den PIN nicht vollständig erhalten.");
    for (QString &line : lines)
        wipe(line);

    const QByteArray output = (result.failure.isEmpty() ? result.output : result.failure).toUtf8();
    const qint32 code = result.failure.isEmpty() ? result.exitCode : -1;
    writeAll(pipe, reinterpret_cast<const char *>(&code), sizeof(code));
    writeBlock(pipe, output);
    CloseHandle(pipe);
    return 0;
}

ConsoleResult runElevatedConsole(const QString &helperProgram,
                                 const QString &program,
                                 const QStringList &args,
                                 const QStringList &lines)
{
    ConsoleResult result;
    const QString pipeName = QStringLiteral("\\\\.\\pipe\\yubigui-%1").arg(quintptr(GetCurrentThreadId()), 0, 16);
    const HANDLE pipe = CreateNamedPipeW(reinterpret_cast<LPCWSTR>(pipeName.utf16()),
                                         PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                         PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                         1, 65536, 65536, 60000, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        result.failure = windowsError(QStringLiteral("Die Verbindung zur erhöhten Sitzung ließ sich nicht öffnen"));
        return result;
    }

    OVERLAPPED overlapped = {};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ConnectNamedPipe(pipe, &overlapped);
    const DWORD connectError = GetLastError();

    const QString parameters = QStringLiteral("--fido-pin-relay \"%1\"").arg(pipeName);
    SHELLEXECUTEINFOW execute = {};
    execute.cbSize = sizeof(execute);
    execute.fMask = SEE_MASK_NOCLOSEPROCESS;
    execute.lpVerb = L"runas";
    execute.lpFile = reinterpret_cast<LPCWSTR>(helperProgram.utf16());
    const std::wstring parameterLine = parameters.toStdWString();
    execute.lpParameters = parameterLine.c_str();
    execute.nShow = SW_HIDE;
    if (!ShellExecuteExW(&execute)) {
        const DWORD error = GetLastError();
        CloseHandle(overlapped.hEvent);
        CloseHandle(pipe);
        result.failure = error == ERROR_CANCELLED
                             ? QStringLiteral("Die erhöhte Sitzung wurde abgebrochen.")
                             : windowsError(QStringLiteral("Die erhöhte Sitzung ließ sich nicht starten"));
        return result;
    }
    if (execute.hProcess)
        CloseHandle(execute.hProcess);

    const bool connected = connectError == ERROR_PIPE_CONNECTED
                           || (connectError == ERROR_IO_PENDING
                               && WaitForSingleObject(overlapped.hEvent, 120000) == WAIT_OBJECT_0);
    CloseHandle(overlapped.hEvent);
    if (!connected) {
        CancelIo(pipe);
        CloseHandle(pipe);
        result.failure = QStringLiteral("Die erhöhte Sitzung hat sich nicht gemeldet.");
        return result;
    }

    const auto send = [&](const QByteArray &bytes) { return writeBlock(pipe, bytes); };
    bool sent = send(program.toUtf8());
    const quint32 argc = quint32(args.size());
    sent = sent && writeAll(pipe, reinterpret_cast<const char *>(&argc), sizeof(argc));
    for (const QString &arg : args)
        sent = sent && send(arg.toUtf8());
    const quint32 pins = quint32(lines.size());
    sent = sent && writeAll(pipe, reinterpret_cast<const char *>(&pins), sizeof(pins));
    for (const QString &line : lines) {
        const QByteArray bytes = line.toUtf8();
        sent = sent && send(bytes);
    }
    if (!sent) {
        CloseHandle(pipe);
        result.failure = QStringLiteral("Der PIN konnte an die erhöhte Sitzung nicht übergeben werden.");
        return result;
    }

    qint32 code = -1;
    DWORD got = 0;
    bool ok = ReadFile(pipe, &code, sizeof(code), &got, nullptr) && got == sizeof(code);
    QByteArray output = readBlock(pipe, &ok);
    CloseHandle(pipe);
    result.exitCode = ok ? int(code) : -1;
    result.output = ok ? QString::fromUtf8(output) : QString();
    wipe(output);
    if (!ok && result.output.isEmpty())
        result.failure = QStringLiteral("Die erhöhte Sitzung hat keine Antwort gegeben.");
    return result;
}

#elif defined(Q_OS_UNIX)

namespace {

// rpassword switches the console to raw mode and drops a line that was
// still sitting in the canonical buffer. The answer is therefore written
// only after that switch, as the Enter key would send it.
void setRawConsole(int fd)
{
    termios term{};
    if (tcgetattr(fd, &term) != 0)
        return;
    term.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    term.c_oflag &= ~OPOST;
    term.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    term.c_cflag &= ~(CSIZE | PARENB);
    term.c_cflag |= CS8;
    term.c_cc[VMIN] = 1;
    term.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSANOW, &term);
}

bool reapChild(pid_t child, int *exitCode)
{
    int status = 0;
    if (waitpid(child, &status, WNOHANG) != child)
        return false;
    *exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    return true;
}

} // namespace

ConsoleResult runOnConsole(const QString &program,
                           const QStringList &args,
                           const QStringList &lines,
                           int timeoutMs,
                           const QString &workDir,
                           const QString &tool,
                           const QStringList &promptHints)
{
    ConsoleResult result;
    const QString toolName = tool.isEmpty() ? QStringLiteral("ykman") : tool;
    QVector<QByteArray> storage;
    storage.append(program.toUtf8());
    for (const QString &arg : args)
        storage.append(arg.toUtf8());
    QVector<char *> argv;
    argv.reserve(storage.size() + 1);
    for (QByteArray &item : storage)
        argv.append(item.data());
    argv.append(nullptr);

    int master = -1;
    winsize size{};
    size.ws_row = 24;
    size.ws_col = 80;
    const pid_t child = forkpty(&master, nullptr, nullptr, &size);
    if (child < 0) {
        result.failure = QStringLiteral("Die Konsole für %1 ließ sich nicht öffnen.").arg(toolName);
        return result;
    }
    if (child == 0) {
        if (!workDir.isEmpty() && chdir(workDir.toUtf8().constData()) != 0)
            _exit(126);
        execv(argv.at(0), argv.data());
        _exit(127);
    }

    PromptGate gate(lines, promptHints);
    QByteArray output;
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(master, &set);
        timeval wait = {0, 50000};
        const int ready = select(master + 1, &set, nullptr, nullptr, &wait);
        if (ready > 0) {
            char buffer[4096];
            const ssize_t got = ::read(master, buffer, sizeof(buffer));
            if (got > 0) {
                output.append(buffer, int(got));
            } else if (got < 0 && (errno == EINTR || errno == EAGAIN)) {
                // Interrupted. Keep waiting.
            } else if (reapChild(child, &result.exitCode)) {
                break;
            } else {
                // EIO or EOF while the slave is not open yet. select reports
                // that immediately, before ykman has started. Do not kill it.
                usleep(20000);
            }
        } else if (ready < 0 && errno != EINTR) {
            result.failure = QStringLiteral("Die Konsole für %1 wurde unterbrochen.").arg(toolName);
            break;
        }
        {
            QString line = gate.takeIfReady(output, QDateTime::currentMSecsSinceEpoch());
            if (!line.isEmpty()) {
                setRawConsole(master);
                QByteArray typed = line.toUtf8();
                wipe(line);
                typed += '\r';
                qint64 sent = 0;
                bool wrote = true;
                while (sent < typed.size()) {
                    const ssize_t chunk = ::write(master, typed.constData() + sent, size_t(typed.size() - sent));
                    if (chunk < 0 && errno == EINTR)
                        continue;
                    if (chunk <= 0) {
                        wrote = false;
                        break;
                    }
                    sent += chunk;
                }
                wipe(typed);
                if (!wrote) {
                    result.failure = QStringLiteral("Die Eingabe konnte nicht übergeben werden.");
                    break;
                }
            }
        }
        if (reapChild(child, &result.exitCode))
            break;
    }
    const bool answered = gate.sent();
    if (result.exitCode == 127 && result.failure.isEmpty())
        result.failure = QStringLiteral("%1 ließ sich nicht starten.").arg(toolName);
    if (result.exitCode == 126 && result.failure.isEmpty())
        result.failure = QStringLiteral("Der Ordner ließ sich nicht öffnen.");
    if (result.exitCode < 0) {
        kill(child, SIGKILL);
        waitpid(child, nullptr, 0);
        if (result.failure.isEmpty()) {
            result.failure = answered
                                 ? QStringLiteral("Die Eingabe wurde übergeben, aber %1 hat nicht fertig geantwortet. "
                                                  "Falls der Key blinkt, berühren.")
                                       .arg(toolName)
                                 : QStringLiteral("%1 hat die Eingabe nicht abgefragt und nicht rechtzeitig geantwortet.")
                                       .arg(toolName);
        }
    }
    ::close(master);
    result.output = stripConsole(QString::fromUtf8(output));
    output.fill('\0');
    for (QByteArray &item : storage)
        wipe(item);
    return result;
}

#else

ConsoleResult runOnConsole(const QString &, const QStringList &, const QStringList &, int,
                           const QString &, const QString &, const QStringList &)
{
    ConsoleResult result;
    result.failure = QStringLiteral("Der FIDO-PIN wird auf diesem System noch nicht geändert.");
    return result;
}

#endif

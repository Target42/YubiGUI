#include "YkmanClient.h"

#include "ConsoleProcess.h"
#include "Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QThread>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <QUuid>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

QString locateProgram()
{
    return Settings::resolveYkman();
}

QString missingProgramMessage()
{
    return Settings::missingYkmanText();
}

QString appId(const QString &name)
{
    if (name == QLatin1String("Yubico OTP"))
        return QStringLiteral("otp");
    if (name == QLatin1String("FIDO U2F"))
        return QStringLiteral("u2f");
    if (name == QLatin1String("FIDO2"))
        return QStringLiteral("fido2");
    if (name == QLatin1String("OATH"))
        return QStringLiteral("oath");
    if (name == QLatin1String("PIV"))
        return QStringLiteral("piv");
    if (name == QLatin1String("OpenPGP"))
        return QStringLiteral("openpgp");
    if (name == QLatin1String("YubiHSM Auth"))
        return QStringLiteral("hsmauth");
    return {};
}

QStringList colorOff(const QStringList &args)
{
    QStringList all{QStringLiteral("--color"), QStringLiteral("never")};
    all << args;
    return all;
}

QStringList listArgs()
{
    return colorOff({QStringLiteral("--log-level"), QStringLiteral("debug"), QStringLiteral("list")});
}

QStringList parseTransports(const QString &text)
{
    QStringList found;
    static const QRegularExpression re(
        QStringLiteral("\\btransport=(Nfc|Usb)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator it = re.globalMatch(text);
    while (it.hasNext())
        found << it.next().captured(1);
    if (!found.isEmpty())
        return found;

    const QRegularExpression usbRe(QStringLiteral("Fast scan:\\s+(\\d+)\\s+USB device"));
    const QRegularExpressionMatch usb = usbRe.match(text);
    if (usb.hasMatch()) {
        const int count = usb.captured(1).toInt();
        for (int index = 0; index < count; ++index)
            found << QStringLiteral("Usb");
    }

    const QString marker = QStringLiteral("Checking NFC reader:");
    int from = 0;
    while ((from = text.indexOf(marker, from)) >= 0) {
        const int next = text.indexOf(marker, from + marker.size());
        const QString chunk = next < 0 ? text.mid(from) : text.mid(from, next - from);
        if (!chunk.contains(QStringLiteral("Skipping NFC reader")))
            found << QStringLiteral("Nfc");
        if (next < 0)
            break;
        from = next;
    }
    return found;
}

QString withoutYkmanLog(const QString &text)
{
    QStringList kept;
    const QRegularExpression logLine(
        QStringLiteral("^(?:INFO|DEBUG|WARNING|ERROR|TRACE)\\s+\\d"));
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')) || logLine.match(trimmed).hasMatch())
            continue;
        kept << trimmed;
    }
    return kept.join(QLatin1Char('\n'));
}

QString ykmanFailure(const QString &detail)
{
    if (detail.contains(QLatin1String("No such option"), Qt::CaseInsensitive)
        && detail.contains(QLatin1String("--color"))) {
        return QStringLiteral(
            "Das gefundene ykman kennt --color nicht. Das ist die Fassung 5. "
            "YubiGUI braucht ykman 6. Unter „Pakete“ stehen die Befehle.");
    }
    return detail;
}

QString transportState(const QString &text)
{
    if (text == QLatin1String("Enabled"))
        return QStringLiteral("enabled");
    if (text == QLatin1String("Disabled"))
        return QStringLiteral("disabled");
    return {};
}

QStringList configArgs(const QString &serial,
                       const QString &transport,
                       const QStringList &enable,
                       const QStringList &disable,
                       const QString &lockCode)
{
    QStringList args{QStringLiteral("--device"), serial, QStringLiteral("config"), transport,
                     QStringLiteral("--force")};
    for (const QString &id : enable)
        args << QStringLiteral("--enable") << id;
    for (const QString &id : disable)
        args << QStringLiteral("--disable") << id;
    if (!lockCode.isEmpty())
        args << QStringLiteral("--lock-code") << lockCode;
    return colorOff(args);
}

QString formFactorLabel(const QString &text)
{
    if (text == QLatin1String("Keychain (USB-A)"))
        return QStringLiteral("Schlüsselanhänger (USB-A)");
    if (text == QLatin1String("Nano (USB-A)"))
        return QStringLiteral("Nano (USB-A)");
    if (text == QLatin1String("Keychain (USB-C)"))
        return QStringLiteral("Schlüsselanhänger (USB-C)");
    if (text == QLatin1String("Nano (USB-C)"))
        return QStringLiteral("Nano (USB-C)");
    if (text == QLatin1String("USB-C Lightning"))
        return QStringLiteral("USB-C/Lightning");
    if (text == QLatin1String("Bio (USB-A)"))
        return QStringLiteral("Bio (USB-A)");
    if (text == QLatin1String("Bio (USB-C)"))
        return QStringLiteral("Bio (USB-C)");
    return text;
}

QList<DeviceRecord> parseList(const QString &text)
{
    QList<DeviceRecord> devices;
    const QRegularExpression lineRe(
        QStringLiteral("^(.+)\\s+\\(([^)]+)\\)\\s+\\[([^\\]]+)\\]\\s+Serial:\\s+(\\d+)\\s*$"));
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty())
            continue;
        DeviceRecord device;
        const QRegularExpressionMatch match = lineRe.match(line);
        if (match.hasMatch()) {
            device.name = match.captured(1).trimmed();
            device.version = match.captured(2).trimmed();
            device.usbInterfaces = match.captured(3).split(QLatin1Char('+'), Qt::SkipEmptyParts);
            device.serial = match.captured(4).trimmed();
        }
        devices.append(device);
    }
    return devices;
}

void parseInfo(const QString &text, DeviceRecord &device)
{
    const QRegularExpression appRe(QStringLiteral(
        "^(.+?)\\s+(Enabled|Disabled|Not available)(?:\\s+(Enabled|Disabled|Not available))?\\s*$"));
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty())
            continue;

        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon > 0) {
            const QString key = line.left(colon).trimmed();
            const QString value = line.mid(colon + 1).trimmed();
            if (key == QLatin1String("Device type"))
                device.name = value;
            else if (key == QLatin1String("Serial number"))
                device.serial = value;
            else if (key == QLatin1String("Firmware version"))
                device.version = value;
            else if (key == QLatin1String("Form factor"))
                device.formFactor = formFactorLabel(value);
            else if (key == QLatin1String("Enabled USB interfaces"))
                device.usbInterfaces = value.split(QStringLiteral(", "), Qt::SkipEmptyParts);
            continue;
        }

        if (line.contains(QLatin1String("NFC"), Qt::CaseInsensitive)
            && (line.contains(QLatin1String("transport"), Qt::CaseInsensitive)
                || line.contains(QLatin1String("interface"), Qt::CaseInsensitive))) {
            device.nfcCapable = true;
            device.nfcTransportEnabled = line.contains(QLatin1String("enabled"), Qt::CaseInsensitive)
                                         && !line.contains(QLatin1String("disabled"), Qt::CaseInsensitive);
            continue;
        }

        if (line.contains(QStringLiteral("lock code"), Qt::CaseInsensitive)
            || line.contains(QStringLiteral("configuration locked"), Qt::CaseInsensitive))
            device.locked = true;

        const QRegularExpressionMatch match = appRe.match(line);
        if (!match.hasMatch())
            continue;
        UsbApp app;
        app.name = match.captured(1).trimmed();
        app.id = appId(app.name);
        if (app.id.isEmpty())
            continue;
        app.usb = transportState(match.captured(2));
        app.nfc = transportState(match.captured(3));
        if (app.usb.isEmpty() && app.nfc.isEmpty())
            continue;
        if (!app.nfc.isEmpty())
            device.nfcCapable = true;
        device.applications.append(app);
    }
}

OtpRecord parseOtp(const QString &text)
{
    OtpRecord otp;
    const QRegularExpression lineRe(QStringLiteral("^Slot\\s+([12]):\\s+(\\S+)"));
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (QString line : lines) {
        line = line.trimmed();
        const QRegularExpressionMatch match = lineRe.match(line);
        if (!match.hasMatch())
            continue;
        OtpSlot slot;
        slot.slot = match.captured(1).toInt();
        slot.configured = match.captured(2).compare(QStringLiteral("empty"), Qt::CaseInsensitive) != 0;
        otp.otpSlots.append(slot);
    }
    return otp;
}

OathRecord parseOathInfo(const QString &text)
{
    OathRecord oath;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (QString line : lines) {
        line = line.trimmed();
        if (line.contains(QLatin1String("remembered by ykman"), Qt::CaseInsensitive))
            oath.passwordRemembered = true;
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        const QString key = line.left(colon).trimmed();
        const QString value = line.mid(colon + 1).trimmed();
        if (key == QLatin1String("OATH version"))
            oath.version = value;
        else if (key == QLatin1String("Password protection"))
            oath.passwordProtected = value.compare(QLatin1String("enabled"), Qt::CaseInsensitive) == 0;
    }
    return oath;
}

QList<OathAccount> parseOathList(const QString &text)
{
    QList<OathAccount> accounts;
    const QRegularExpression lineRe(
        QStringLiteral("^(.*) \\((TOTP|HOTP)\\)(?: \\[period: (\\d+)\\])?$"));
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty())
            continue;
        const QRegularExpressionMatch match = lineRe.match(line);
        if (!match.hasMatch())
            continue;
        OathAccount account;
        account.name = match.captured(1).trimmed();
        account.type = match.captured(2);
        account.period = match.captured(3).toInt();
        if (!account.name.isEmpty())
            accounts.append(account);
    }
    return accounts;
}

FidoRecord parseFido(const QString &text)
{
    FidoRecord fido;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (QString line : lines) {
        line = line.trimmed();
        if (line.contains(QLatin1String("Factory reset is blocked"), Qt::CaseInsensitive))
            fido.resetBlocked = true;
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        const QString key = line.left(colon).trimmed();
        const QString value = line.mid(colon + 1).trimmed();
        if (key == QLatin1String("AAGUID"))
            fido.aaguid = value;
        else if (key == QLatin1String("PIN")) {
            fido.pin = value;
            if (value.contains(QLatin1String("not set"), Qt::CaseInsensitive)) {
                fido.pinSet = false;
            } else {
                const QRegularExpressionMatch attempts =
                    QRegularExpression(QStringLiteral("^(\\d+)\\s+attempt")).match(value);
                if (attempts.hasMatch()) {
                    fido.pinSet = true;
                    fido.attemptsRemaining = attempts.captured(1).toInt();
                }
            }
        } else if (key == QLatin1String("Minimum PIN length")) {
            fido.minimumPinLength = value;
            bool ok = false;
            const int length = value.toInt(&ok);
            if (ok)
                fido.minPinLength = length;
        }
        else if (key.compare(QLatin1String("Always Require UV"), Qt::CaseInsensitive) == 0)
            fido.alwaysUv = value;
        else if (key == QLatin1String("Credential storage remaining"))
            fido.storageRemaining = value;
        else if (key == QLatin1String("Fingerprints"))
            fido.fingerprints = value;
    }
    return fido;
}

void readTries(const QString &value, int &remaining, int &total)
{
    remaining = -1;
    total = -1;
    const QRegularExpression pair(QStringLiteral("^(\\d+)(?:/(\\d+))?$"));
    const QRegularExpressionMatch match = pair.match(value.trimmed());
    if (match.hasMatch()) {
        remaining = match.captured(1).toInt();
        if (!match.captured(2).isEmpty())
            total = match.captured(2).toInt();
        return;
    }
    if (value.contains(QLatin1String("15 or more"), Qt::CaseInsensitive))
        remaining = 15;
}

PivRecord parsePiv(const QString &text)
{
    PivRecord piv;
    PivSlotInfo slot;
    bool inSlot = false;
    const auto flush = [&]() {
        if (!inSlot)
            return;
        piv.entries.append(slot);
        slot = {};
        inSlot = false;
    };
    const QRegularExpression slotLine(
        QStringLiteral("^Slot\\s+([0-9A-Fa-f]{2})\\s+\\(([^)]+)\\)\\s*:?\\s*$"));
    for (QString line : text.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;
        if (trimmed.startsWith(QLatin1String("WARNING:"), Qt::CaseInsensitive)) {
            if (trimmed.contains(QLatin1String("default PIN"), Qt::CaseInsensitive))
                piv.pinDefault = true;
            if (trimmed.contains(QLatin1String("default PUK"), Qt::CaseInsensitive))
                piv.pukDefault = true;
            if (trimmed.contains(QLatin1String("Management key"), Qt::CaseInsensitive))
                piv.managementDefault = true;
            continue;
        }
        if (trimmed.contains(QLatin1String("Factory reset is blocked"), Qt::CaseInsensitive)) {
            piv.resetBlocked = true;
            continue;
        }
        const QRegularExpressionMatch heading = slotLine.match(trimmed);
        if (heading.hasMatch()) {
            flush();
            inSlot = true;
            slot.slot = heading.captured(1).toUpper();
            slot.name = heading.captured(2).trimmed();
            continue;
        }
        const int colon = trimmed.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        const QString key = trimmed.left(colon).trimmed();
        const QString value = trimmed.mid(colon + 1).trimmed();
        if (inSlot) {
            if (key == QLatin1String("Subject DN"))
                slot.subject = value;
            else if (key == QLatin1String("Not after"))
                slot.expires = value;
            else if (slot.keyType.isEmpty()
                     && (key == QLatin1String("Private key type") || key == QLatin1String("Public key type")))
                slot.keyType = value;
            continue;
        }
        if (key == QLatin1String("PIV version"))
            piv.version = value;
        else if (key == QLatin1String("PIN tries remaining"))
            readTries(value, piv.pinAttempts, piv.pinTotal);
        else if (key == QLatin1String("PUK tries remaining"))
            readTries(value, piv.pukAttempts, piv.pukTotal);
        else if (key == QLatin1String("Management key algorithm"))
            piv.managementAlgorithm = value;
        else if (key == QLatin1String("Biometrics"))
            piv.biometrics = value;
    }
    flush();
    return piv;
}

int openPgpAttempts(const QString &value)
{
    bool ok = false;
    const int attempts = value.trimmed().toInt(&ok);
    return ok ? attempts : -1;
}

OpenPgpRecord parseOpenPgp(const QString &text)
{
    OpenPgpRecord record;
    OpenPgpKey current;
    bool inKey = false;
    const auto flush = [&]() {
        if (inKey && !current.id.isEmpty())
            record.keys.append(current);
        current = {};
        inKey = false;
    };
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("\\r?\\n")));
    for (QString line : lines) {
        const bool indented = line.startsWith(QLatin1Char(' ')) || line.startsWith(QLatin1Char('\t'));
        line = line.trimmed();
        if (line.isEmpty())
            continue;
        if (!indented && line.endsWith(QLatin1Char(':')) && line.contains(QLatin1String("key"), Qt::CaseInsensitive)) {
            flush();
            const QString heading = line.left(line.size() - 1).trimmed();
            if (heading.startsWith(QLatin1String("Signature")))
                current.id = QStringLiteral("sig");
            else if (heading.startsWith(QLatin1String("Decryption")))
                current.id = QStringLiteral("dec");
            else if (heading.startsWith(QLatin1String("Authentication")))
                current.id = QStringLiteral("aut");
            else if (heading.startsWith(QLatin1String("Attestation")))
                current.id = QStringLiteral("att");
            else
                continue;
            current.name = heading;
            inKey = true;
            continue;
        }
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon < 1)
            continue;
        const QString key = line.left(colon).trimmed();
        const QString value = line.mid(colon + 1).trimmed();
        if (inKey) {
            if (key == QLatin1String("Fingerprint"))
                current.fingerprint = value;
            else if (key == QLatin1String("Touch policy"))
                current.touch = value;
            continue;
        }
        if (key == QLatin1String("OpenPGP version"))
            record.version = value;
        else if (key == QLatin1String("Application version"))
            record.application = value;
        else if (key == QLatin1String("PIN tries remaining"))
            record.pinAttempts = openPgpAttempts(value);
        else if (key == QLatin1String("Reset code tries remaining"))
            record.resetAttempts = openPgpAttempts(value);
        else if (key == QLatin1String("Admin PIN tries remaining"))
            record.adminAttempts = openPgpAttempts(value);
        else if (key == QLatin1String("Require PIN for signature"))
            record.signaturePolicy = value;
        else if (key == QLatin1String("KDF enabled"))
            record.kdf = value.compare(QLatin1String("True"), Qt::CaseInsensitive) == 0;
    }
    flush();
    return record;
}

DeviceRecord *deviceForSerial(QList<DeviceRecord> &devices, const QString &serial)
{
    for (DeviceRecord &device : devices) {
        if (device.serial == serial)
            return &device;
    }
    return devices.isEmpty() ? nullptr : &devices.first();
}

} // namespace

YkmanClient::YkmanClient(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
    , m_timeout(new QTimer(this))
{
    m_timeout->setSingleShot(true);
    m_timeout->setInterval(20000);

#ifdef Q_OS_WIN
    m_process->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *args) {
            args->flags |= CREATE_NO_WINDOW;
        });
#endif

    connect(m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus) {
        m_timeout->stop();
        m_stdout += QString::fromUtf8(m_process->readAllStandardOutput());
        m_stderr += QString::fromUtf8(m_process->readAllStandardError());
        handleOutput(exitCode);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            fail(QStringLiteral("ykman ließ sich nicht starten."));
    });
    connect(m_timeout, &QTimer::timeout, this, [this]() {
        m_snapshot.error = QStringLiteral("ykman hat nicht rechtzeitig geantwortet.");
        m_queue.clear();
        m_process->kill();
    });
}

YkmanClient::~YkmanClient()
{
    if (m_pinThread && m_pinThread->isRunning())
        m_pinThread->wait(3000);
}

void YkmanClient::setApplications(const QString &serial,
                                 const QStringList &usbEnable,
                                 const QStringList &usbDisable,
                                 const QStringList &nfcEnable,
                                 const QStringList &nfcDisable,
                                 const QString &lockCode)
{
    if (m_busy || serial.isEmpty())
        return;
    if (usbEnable.isEmpty() && usbDisable.isEmpty() && nfcEnable.isEmpty() && nfcDisable.isEmpty())
        return;

    m_preferredSerial = serial;
    m_snapshot.program = locateProgram();
    if (m_snapshot.program.isEmpty()) {
        m_snapshot.error = missingProgramMessage();
        emit refreshFinished();
        return;
    }

    m_busy = true;
    m_probeAttempts = 0;
    m_snapshot.error.clear();
    m_queue.clear();
    if (!nfcEnable.isEmpty() || !nfcDisable.isEmpty())
        m_queue.append({Step::Nfc, configArgs(serial, QStringLiteral("nfc"), nfcEnable, nfcDisable, lockCode)});
    if (!usbEnable.isEmpty() || !usbDisable.isEmpty())
        m_queue.append({Step::Usb, configArgs(serial, QStringLiteral("usb"), usbEnable, usbDisable, lockCode)});
    startNext();
}

void YkmanClient::refresh(const QString &preferredSerial)
{
    if (m_busy)
        return;

    m_preferredSerial = preferredSerial;
    m_snapshot = {};
    m_snapshot.program = locateProgram();
    if (m_snapshot.program.isEmpty()) {
        m_snapshot.error = missingProgramMessage();
        emit refreshFinished();
        return;
    }

    m_busy = true;
    m_queue = {{Step::Version, colorOff({QStringLiteral("version")})},
               {Step::List, listArgs()}};
    startNext();
}

bool YkmanClient::busy() const
{
    return m_busy;
}

const YkmanSnapshot &YkmanClient::snapshot() const
{
    return m_snapshot;
}

void YkmanClient::startNext()
{
    if (m_queue.isEmpty()) {
        m_busy = false;
        emit refreshFinished();
        return;
    }

    m_current = m_queue.takeFirst();
    m_stdout.clear();
    m_stderr.clear();
    QStringList args = m_current.args;
    const bool writesConfig = m_current.step == Step::Usb || m_current.step == Step::Nfc;
    if (writesConfig)
        m_current.args.clear();
    m_process->start(m_snapshot.program, args);
    if (writesConfig) {
        for (QString &arg : args)
            arg.fill(QLatin1Char('0'));
        args.clear();
    }
    m_timeout->start();
}

void YkmanClient::probeDevice()
{
    if (!m_busy)
        return;
    m_current = {Step::Probe, colorOff({QStringLiteral("list")})};
    m_stdout.clear();
    m_stderr.clear();
    m_process->start(m_snapshot.program, m_current.args);
    m_timeout->start();
}

void YkmanClient::fail(const QString &message)
{
    m_timeout->stop();
    m_queue.clear();
    m_snapshot.error = message;
    m_busy = false;
    emit refreshFinished();
}

void YkmanClient::handleOutput(int exitCode)
{
    const QStringList transports = m_current.step == Step::List ? parseTransports(m_stderr) : QStringList();
    m_stderr = withoutYkmanLog(m_stderr);

    if (!m_snapshot.error.isEmpty()) {
        m_busy = false;
        emit refreshFinished();
        return;
    }

    if (exitCode != 0 && m_current.step != Step::Otp && m_current.step != Step::Fido
        && m_current.step != Step::OathInfo && m_current.step != Step::OathList
        && m_current.step != Step::Piv && m_current.step != Step::OpenPgp
        && m_current.step != Step::Probe) {
        const QString detail = m_stderr.trimmed().isEmpty() ? m_stdout.trimmed() : m_stderr.trimmed();
        const QString message = ykmanFailure(detail.isEmpty() ? QStringLiteral("ykman meldet einen Fehler.") : detail);
        if ((m_current.step == Step::Usb || m_current.step == Step::Nfc)
            && message.contains(QStringLiteral("lock"), Qt::CaseInsensitive)) {
            m_queue.clear();
            m_busy = false;
            emit lockCodeNeeded(message);
            return;
        }
        fail(message);
        return;
    }

    switch (m_current.step) {
    case Step::Version:
        m_snapshot.version = m_stdout.trimmed();
        break;
    case Step::List:
        m_snapshot.devices = parseList(m_stdout);
        for (int index = 0; index < m_snapshot.devices.size() && index < transports.size(); ++index)
            m_snapshot.devices[index].transport = transports.at(index);
        if (!m_snapshot.devices.isEmpty()) {
            QString serial = m_snapshot.devices.first().serial;
            for (const DeviceRecord &device : m_snapshot.devices) {
                if (!m_preferredSerial.isEmpty() && device.serial == m_preferredSerial)
                    serial = device.serial;
            }
            if (!serial.isEmpty()) {
                const QStringList deviceArgs{QStringLiteral("--device"), serial};
                m_queue.prepend({Step::OpenPgp, colorOff(deviceArgs + QStringList{
                    QStringLiteral("openpgp"), QStringLiteral("info")})});
                m_queue.prepend({Step::Piv, colorOff(deviceArgs + QStringList{
                    QStringLiteral("piv"), QStringLiteral("info")})});
                m_queue.prepend({Step::OathList, colorOff(deviceArgs + QStringList{
                    QStringLiteral("oath"), QStringLiteral("accounts"), QStringLiteral("list"),
                    QStringLiteral("-o"), QStringLiteral("-P")})});
                m_queue.prepend({Step::OathInfo, colorOff(deviceArgs + QStringList{
                    QStringLiteral("oath"), QStringLiteral("info")})});
                m_queue.prepend({Step::Fido, colorOff(deviceArgs + QStringList{QStringLiteral("fido"), QStringLiteral("info")})});
                m_queue.prepend({Step::Otp, colorOff(deviceArgs + QStringList{QStringLiteral("otp"), QStringLiteral("info")})});
                m_queue.prepend({Step::Info, colorOff(deviceArgs + QStringList{QStringLiteral("info")})});
            }
        }
        break;
    case Step::Info:
        if (DeviceRecord *device = deviceForSerial(m_snapshot.devices, m_preferredSerial))
            parseInfo(m_stdout, *device);
        break;
    case Step::Otp:
        if (exitCode != 0) {
            const QString detail = m_stderr.trimmed().isEmpty() ? m_stdout.trimmed() : m_stderr.trimmed();
            m_snapshot.otp.error = detail.isEmpty() ? QStringLiteral("OTP-Status nicht lesbar.") : detail;
        } else {
            m_snapshot.otp = parseOtp(m_stdout);
        }
        break;
    case Step::Fido:
        if (exitCode != 0) {
            const QString detail = m_stderr.trimmed().isEmpty() ? m_stdout.trimmed() : m_stderr.trimmed();
            m_snapshot.fido.error = detail.isEmpty() ? QStringLiteral("FIDO-Status nicht lesbar.") : detail;
        } else {
            m_snapshot.fido = parseFido(m_stdout);
        }
        break;
    case Step::OathInfo: {
        if (exitCode != 0) {
            const QString detail = m_stderr.trimmed().isEmpty() ? m_stdout.trimmed() : m_stderr.trimmed();
            m_snapshot.oath.error = detail.isEmpty() ? QStringLiteral("OATH ist nicht erreichbar.") : detail;
        } else {
            const bool loaded = m_snapshot.oath.loaded;
            const QList<OathAccount> accounts = m_snapshot.oath.accounts;
            m_snapshot.oath = parseOathInfo(m_stdout);
            m_snapshot.oath.loaded = loaded;
            m_snapshot.oath.accounts = accounts;
        }
        const bool skipList = !m_snapshot.oath.error.isEmpty()
                              || (m_snapshot.oath.passwordProtected && !m_snapshot.oath.passwordRemembered);
        if (skipList) {
            for (int index = 0; index < m_queue.size(); ++index) {
                if (m_queue.at(index).step == Step::OathList) {
                    m_queue.removeAt(index);
                    break;
                }
            }
        }
        break;
    }
    case Step::OathList:
        if (exitCode != 0) {
            const QString detail = m_stderr.trimmed().isEmpty() ? m_stdout.trimmed() : m_stderr.trimmed();
            m_snapshot.oath.error = detail.isEmpty() ? QStringLiteral("Die OATH-Konten sind nicht lesbar.") : detail;
            m_snapshot.oath.loaded = false;
        } else {
            m_snapshot.oath.accounts = parseOathList(m_stdout);
            m_snapshot.oath.loaded = true;
            m_snapshot.oath.error.clear();
        }
        break;
    case Step::Piv:
        if (exitCode != 0) {
            const QString detail = m_stderr.trimmed().isEmpty() ? m_stdout.trimmed() : m_stderr.trimmed();
            m_snapshot.piv.error = detail.isEmpty() ? QStringLiteral("PIV ist nicht erreichbar.") : detail;
        } else {
            m_snapshot.piv = parsePiv(m_stdout);
        }
        break;
    case Step::OpenPgp:
        if (exitCode != 0) {
            const QString detail = m_stderr.trimmed().isEmpty() ? m_stdout.trimmed() : m_stderr.trimmed();
            m_snapshot.openpgp.error = detail.isEmpty()
                                           ? QStringLiteral("OpenPGP ist nicht erreichbar.")
                                           : detail;
        } else {
            m_snapshot.openpgp = parseOpenPgp(m_stdout);
        }
        break;
    case Step::Nfc:
    case Step::Usb:
        if (!m_queue.isEmpty()
            && (m_queue.first().step == Step::Nfc || m_queue.first().step == Step::Usb)) {
            m_probeAttempts = 0;
            QTimer::singleShot(600, this, [this]() { probeDevice(); });
            return;
        }
        m_queue = {{Step::Version, colorOff({QStringLiteral("version")})},
                   {Step::List, listArgs()}};
        break;
    case Step::Probe:
        if (m_stdout.contains(m_preferredSerial))
            break;
        if (++m_probeAttempts >= 12) {
            fail(QStringLiteral("Der Key war nach der Änderung nicht wieder da."));
            return;
        }
        QTimer::singleShot(500, this, [this]() { probeDevice(); });
        return;
    }

    startNext();
}

namespace {

void wipeSecret(QString &text)
{
    text.fill(QLatin1Char('0'));
    text.clear();
}

class FidoPinWorker : public QThread
{
public:
    FidoPinWorker(YkmanClient *client,
                  QString program,
                  QString helper,
                  QString serial,
                  QString current,
                  QString next,
                  bool elevated)
        : m_client(client)
        , m_program(std::move(program))
        , m_helper(std::move(helper))
        , m_serial(std::move(serial))
        , m_current(std::move(current))
        , m_next(std::move(next))
        , m_elevated(elevated)
    {
    }

    void run() override
    {
        const QStringList args{QStringLiteral("--color"), QStringLiteral("never"),
                               QStringLiteral("--device"), m_serial,
                               QStringLiteral("fido"), QStringLiteral("access"),
                               QStringLiteral("change-pin")};
        QStringList lines;
        if (!m_current.isEmpty())
            lines << m_current;
        lines << m_next << m_next;

#ifdef Q_OS_WIN
        const ConsoleResult result = m_elevated
                                         ? runElevatedConsole(m_helper, m_program, args, lines)
                                         : runOnConsole(m_program, args, lines, 60000);
#else
        Q_UNUSED(m_helper);
        Q_UNUSED(m_elevated);
        const ConsoleResult result = runOnConsole(m_program, args, lines, 60000);
#endif
        for (QString &line : lines)
            wipeSecret(line);
        wipeSecret(m_current);
        wipeSecret(m_next);

        const bool ok = result.failure.isEmpty() && result.exitCode == 0;
        const QString detail = result.failure.isEmpty() ? result.output : result.failure;
        QMetaObject::invokeMethod(m_client, [client = m_client, ok, detail]() {
            client->deliverFidoPin(ok, detail);
        }, Qt::QueuedConnection);
    }

private:
    YkmanClient *m_client;
    QString m_program;
    QString m_helper;
    QString m_serial;
    QString m_current;
    QString m_next;
    bool m_elevated;
};

class FidoResetWorker : public QThread
{
public:
    FidoResetWorker(YkmanClient *client, QString program, QString helper, QString serial, bool elevated)
        : m_client(client)
        , m_program(std::move(program))
        , m_helper(std::move(helper))
        , m_serial(std::move(serial))
        , m_elevated(elevated)
    {
    }

    void run() override
    {
        const QStringList args{QStringLiteral("--color"), QStringLiteral("never"),
                               QStringLiteral("--device"), m_serial,
                               QStringLiteral("fido"), QStringLiteral("reset")};
        const QStringList lines{QStringLiteral("y")};

#ifdef Q_OS_WIN
        const ConsoleResult result = m_elevated
                                         ? runElevatedConsole(m_helper, m_program, args, lines)
                                         : runOnConsole(m_program, args, lines, 180000);
#else
        Q_UNUSED(m_helper);
        Q_UNUSED(m_elevated);
        const ConsoleResult result = runOnConsole(m_program, args, lines, 180000);
#endif
        const bool ok = result.failure.isEmpty() && result.exitCode == 0;
        const QString detail = result.failure.isEmpty() ? result.output : result.failure;
        QMetaObject::invokeMethod(m_client, [client = m_client, ok, detail]() {
            client->deliverFidoReset(ok, detail);
        }, Qt::QueuedConnection);
    }

private:
    YkmanClient *m_client;
    QString m_program;
    QString m_helper;
    QString m_serial;
    bool m_elevated;
};

class FidoCredentialWorker : public QThread
{
public:
    FidoCredentialWorker(YkmanClient *client, QString program, QString helper, QString serial,
                         FidoCredentialAction action, QString pin, QString credentialId, bool elevated)
        : m_client(client)
        , m_program(std::move(program))
        , m_helper(std::move(helper))
        , m_serial(std::move(serial))
        , m_action(action)
        , m_pin(std::move(pin))
        , m_credentialId(std::move(credentialId))
        , m_elevated(elevated)
    {
    }

    void run() override
    {
        QStringList args{QStringLiteral("--color"), QStringLiteral("never"),
                         QStringLiteral("--device"), m_serial,
                         QStringLiteral("fido"), QStringLiteral("credentials")};
        if (m_action == FidoCredentialAction::Delete)
            args << QStringLiteral("delete") << QStringLiteral("--force") << m_credentialId;
        else
            args << QStringLiteral("list") << QStringLiteral("--csv");
        QStringList lines{m_pin};

#ifdef Q_OS_WIN
        const ConsoleResult result = m_elevated
                                         ? runElevatedConsole(m_helper, m_program, args, lines)
                                         : runOnConsole(m_program, args, lines, 60000);
#else
        Q_UNUSED(m_helper);
        Q_UNUSED(m_elevated);
        const ConsoleResult result = runOnConsole(m_program, args, lines, 120000);
#endif
        for (QString &line : lines)
            wipeSecret(line);
        wipeSecret(m_pin);

        const bool ok = result.failure.isEmpty() && result.exitCode == 0;
        QString output = result.output;
        QString detail = ok ? QString() : (result.failure.isEmpty() ? output : result.failure);
        const int action = int(m_action);
        QMetaObject::invokeMethod(m_client, [client = m_client, ok, action, detail, output]() mutable {
            client->deliverFidoCredentials(ok, action, detail, output);
            output.fill(QLatin1Char('0'));
            output.clear();
            detail.fill(QLatin1Char('0'));
            detail.clear();
        }, Qt::QueuedConnection);
        wipeSecret(output);
        wipeSecret(detail);
    }

private:
    YkmanClient *m_client;
    QString m_program;
    QString m_helper;
    QString m_serial;
    FidoCredentialAction m_action;
    QString m_pin;
    QString m_credentialId;
    bool m_elevated;
};

QString revealOtp(const QString &output)
{
    QStringList rows;
    const auto take = [&rows](const QString &line, const QString &marker, const QString &label) {
        const int at = line.indexOf(marker);
        if (at < 0)
            return;
        const QString value = line.mid(at + marker.size()).trimmed();
        if (!value.isEmpty())
            rows << label + value;
    };
    for (QString line : output.split(QLatin1Char('\n'))) {
        line = line.trimmed();
        take(line, QStringLiteral("public ID:"), QStringLiteral("Öffentliche ID: "));
        take(line, QStringLiteral("private ID:"), QStringLiteral("Private ID: "));
        take(line, QStringLiteral("secret key:"), QStringLiteral("Geheimer Schlüssel: "));
        take(line, QStringLiteral("key (hex):"), QStringLiteral("Schlüssel (hex): "));
        take(line, QStringLiteral("key (base32):"), QStringLiteral("Schlüssel (Base32): "));
    }
    return rows.join(QLatin1Char('\n'));
}

QString publicOtpText(const QString &output)
{
    QStringList kept;
    for (QString line : output.split(QLatin1Char('\n'))) {
        line = line.trimmed();
        if (line.isEmpty())
            continue;
        const QString lower = line.toLower();
        if (lower.contains(QLatin1String("public id"))
            || lower.contains(QLatin1String("private id"))
            || lower.contains(QLatin1String("secret key"))
            || lower.contains(QLatin1String("generated key"))
            || lower.contains(QLatin1String("randomly generated")))
            continue;
        kept << line;
    }
    return kept.join(QLatin1Char('\n'));
}

class OtpWorker : public QThread
{
public:
    OtpWorker(YkmanClient *client, QString program, QString serial, OtpRequest request)
        : m_client(client)
        , m_program(std::move(program))
        , m_serial(std::move(serial))
        , m_request(std::move(request))
    {
    }

    void run() override
    {
        QStringList args{QStringLiteral("--color"), QStringLiteral("never"),
                         QStringLiteral("--device"), m_serial,
                         QStringLiteral("otp")};
        if (!m_request.accessCode.isEmpty())
            args << QStringLiteral("--access-code") << m_request.accessCode;

        QStringList lines;
        const QString slot = QString::number(m_request.slot);
        switch (m_request.action) {
        case OtpAction::Delete:
            args << QStringLiteral("delete") << slot << QStringLiteral("--force");
            break;
        case OtpAction::Swap:
            args << QStringLiteral("swap") << QStringLiteral("--force");
            break;
        case OtpAction::YubiOtp:
            args << QStringLiteral("yubiotp") << slot
                 << QStringLiteral("--serial-public-id")
                 << QStringLiteral("--generate-private-id")
                 << QStringLiteral("--generate-key")
                 << QStringLiteral("--force");
            break;
        case OtpAction::ChalResp:
            args << QStringLiteral("chalresp") << slot
                 << QStringLiteral("--generate")
                 << QStringLiteral("--force");
            if (m_request.touch)
                args << QStringLiteral("--touch");
            break;
        case OtpAction::StaticPassword:
            args << QStringLiteral("static") << slot
                 << QStringLiteral("--keyboard-layout") << m_request.layout
                 << QStringLiteral("--force");
            lines << m_request.secret << m_request.secret;
            break;
        case OtpAction::Hotp:
            args << QStringLiteral("hotp") << slot
                 << QStringLiteral("--digits") << QString::number(m_request.digits)
                 << QStringLiteral("--force");
            lines << m_request.secret;
            break;
        }

        ConsoleResult result = runOnConsole(m_program, args, lines, 60000);
        for (QString &line : lines)
            wipeSecret(line);
        for (QString &arg : args)
            wipeSecret(arg);
        wipeSecret(m_request.secret);
        wipeSecret(m_request.accessCode);

        QString output = result.output;
        wipeSecret(result.output);
        const bool ok = result.failure.isEmpty() && result.exitCode == 0;
        const QString revealed = ok ? revealOtp(output) : QString();
        const QString detail = result.failure.isEmpty() ? publicOtpText(output) : result.failure;
        wipeSecret(output);
        QMetaObject::invokeMethod(m_client, [client = m_client, ok, detail, revealed]() {
            client->deliverOtp(ok, ok ? QString() : detail, revealed);
        }, Qt::QueuedConnection);
    }

private:
    YkmanClient *m_client;
    QString m_program;
    QString m_serial;
    OtpRequest m_request;
};

class OathWorker : public QThread
{
public:
    OathWorker(YkmanClient *client, QString program, QString serial, OathRequest request)
        : m_client(client)
        , m_program(std::move(program))
        , m_serial(std::move(serial))
        , m_request(std::move(request))
    {
    }

    void run() override
    {
        QStringList args{QStringLiteral("--color"), QStringLiteral("never"),
                         QStringLiteral("--device"), m_serial,
                         QStringLiteral("oath")};
        QStringList lines;
        const bool locked = !m_request.password.isEmpty();
        switch (m_request.action) {
        case OathAction::List:
            args << QStringLiteral("accounts") << QStringLiteral("list")
                 << QStringLiteral("-o") << QStringLiteral("-P");
            if (locked)
                lines << m_request.password;
            break;
        case OathAction::Code:
            args << QStringLiteral("accounts") << QStringLiteral("code");
            if (locked)
                lines << m_request.password;
            break;
        case OathAction::CodeOne:
            args << QStringLiteral("accounts") << QStringLiteral("code")
                 << QStringLiteral("--single") << m_request.query;
            if (locked)
                lines << m_request.password;
            break;
        case OathAction::Add:
            args << QStringLiteral("accounts") << QStringLiteral("add") << m_request.query;
            if (!m_request.issuer.isEmpty())
                args << QStringLiteral("--issuer") << m_request.issuer;
            args << QStringLiteral("--oath-type") << m_request.oathType
                 << QStringLiteral("--digits") << QString::number(m_request.digits)
                 << QStringLiteral("--algorithm") << m_request.algorithm
                 << QStringLiteral("--force");
            if (m_request.oathType == QLatin1String("hotp"))
                args << QStringLiteral("--counter") << QString::number(m_request.counter);
            else
                args << QStringLiteral("--period") << QString::number(m_request.period);
            if (m_request.touch)
                args << QStringLiteral("--touch");
            lines << m_request.secret;
            if (locked)
                lines << m_request.password;
            break;
        case OathAction::Rename:
            args << QStringLiteral("accounts") << QStringLiteral("rename")
                 << QStringLiteral("--force") << m_request.query << m_request.newName;
            if (locked)
                lines << m_request.password;
            break;
        case OathAction::Delete:
            args << QStringLiteral("accounts") << QStringLiteral("delete")
                 << QStringLiteral("--force") << m_request.query;
            if (locked)
                lines << m_request.password;
            break;
        case OathAction::SetPassword:
            args << QStringLiteral("access") << QStringLiteral("change");
            if (locked)
                lines << m_request.password;
            lines << m_request.newPassword << m_request.newPassword;
            break;
        case OathAction::ClearPassword:
            args << QStringLiteral("access") << QStringLiteral("change") << QStringLiteral("--clear");
            if (locked)
                lines << m_request.password;
            break;
        }

        ConsoleResult result = runOnConsole(m_program, args, lines, 90000);
        for (QString &line : lines)
            wipeSecret(line);
        for (QString &arg : args)
            wipeSecret(arg);
        wipeSecret(m_request.secret);
        wipeSecret(m_request.password);
        wipeSecret(m_request.newPassword);

        QString output = result.output;
        wipeSecret(result.output);
        const bool ok = result.failure.isEmpty() && result.exitCode == 0;
        const QString detail = ok ? QString() : (result.failure.isEmpty() ? output : result.failure);
        const int action = int(m_request.action);
        QMetaObject::invokeMethod(m_client, [client = m_client, ok, action, detail, output]() mutable {
            client->deliverOath(ok, action, detail, output);
            output.fill(QLatin1Char('0'));
            output.clear();
        }, Qt::QueuedConnection);
        wipeSecret(output);
    }

private:
    YkmanClient *m_client;
    QString m_program;
    QString m_serial;
    OathRequest m_request;
};

QString tempPublicPem()
{
    return QDir::temp().filePath(QStringLiteral("yubigui-%1.pem")
                                      .arg(QUuid::createUuid().toString(QUuid::Id128)));
}

bool pemToOpenSsh(const QString &pemPath, const QString &dest, const QString &comment, QString *error)
{
    const QString program = Settings::resolveSshKeygen();
    if (program.isEmpty()) {
        if (error)
            *error = Settings::missingSshKeygenText();
        return false;
    }
    const ConsoleResult result = runOnConsole(
        program,
        {QStringLiteral("-i"), QStringLiteral("-m"), QStringLiteral("PKCS8"), QStringLiteral("-f"), pemPath},
        {}, 20000, {}, QStringLiteral("ssh-keygen"));
    if (!result.failure.isEmpty() || result.exitCode != 0) {
        const QString why = result.failure.isEmpty() ? result.output : result.failure;
        if (error)
            *error = why.isEmpty() ? QStringLiteral("ssh-keygen konnte den Schlüssel nicht umrechnen.") : why;
        return false;
    }
    QString line;
    for (const QString &row : result.output.split(QLatin1Char('\n'))) {
        const QString trimmed = row.trimmed();
        if (!trimmed.isEmpty() && !trimmed.startsWith(QLatin1String("-----"))) {
            line = trimmed;
            break;
        }
    }
    const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() < 2) {
        if (error)
            *error = line.isEmpty() ? QStringLiteral("ssh-keygen hat keinen öffentlichen Schlüssel geschrieben.")
                                    : line;
        return false;
    }
    const QString note = comment.trimmed();
    if (!note.isEmpty() && parts.size() == 2)
        line += QLatin1Char(' ') + note;
    QFile file(dest);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QStringLiteral("Die Datei ließ sich nicht schreiben.");
        return false;
    }
    const QByteArray bytes = (line + QLatin1Char('\n')).toUtf8();
    const bool wrote = file.write(bytes) == bytes.size();
    file.close();
    if (!wrote && error)
        *error = QStringLiteral("Die Datei ließ sich nicht schreiben.");
    return wrote;
}

class PivWorker : public QThread
{
public:
    PivWorker(YkmanClient *client, QString program, QString serial, PivRequest request)
        : m_client(client)
        , m_program(std::move(program))
        , m_serial(std::move(serial))
        , m_request(std::move(request))
    {
    }

    void run() override
    {
        QStringList args{QStringLiteral("--color"), QStringLiteral("never"),
                         QStringLiteral("--device"), m_serial};
        QStringList lines;
        int timeout = 90000;
        const bool asSsh = m_request.format == QLatin1String("ssh")
                           && (m_request.action == PivAction::GenerateKey
                               || m_request.action == PivAction::ExportKey);
        QString sshTemp;
        QString keyOut = m_request.outputPath;
        if (asSsh) {
            sshTemp = tempPublicPem();
            keyOut = sshTemp;
        }
        switch (m_request.action) {
        case PivAction::ChangePin:
            args << QStringLiteral("piv") << QStringLiteral("access") << QStringLiteral("change-pin");
            lines << m_request.current << m_request.next << m_request.next;
            break;
        case PivAction::ChangePuk:
            args << QStringLiteral("piv") << QStringLiteral("access") << QStringLiteral("change-puk");
            lines << m_request.current << m_request.next << m_request.next;
            break;
        case PivAction::UnblockPin:
            args << QStringLiteral("piv") << QStringLiteral("access") << QStringLiteral("unblock-pin");
            lines << m_request.current << m_request.next << m_request.next;
            break;
        case PivAction::GenerateKey:
            timeout = 180000;
            args << QStringLiteral("piv") << QStringLiteral("keys") << QStringLiteral("generate")
                 << QStringLiteral("--algorithm") << m_request.algorithm
                 << QStringLiteral("--pin-policy") << m_request.pinPolicy
                 << QStringLiteral("--touch-policy") << m_request.touchPolicy;
            if (asSsh)
                args << QStringLiteral("-F") << QStringLiteral("pem");
            args << m_request.slot << keyOut;
            lines = consoleLines();
            break;
        case PivAction::ExportKey:
            args << QStringLiteral("piv") << QStringLiteral("keys") << QStringLiteral("export")
                 << QStringLiteral("-F") << QStringLiteral("pem")
                 << m_request.slot << keyOut;
            break;
        case PivAction::ImportCert:
            args << QStringLiteral("piv") << QStringLiteral("certificates") << QStringLiteral("import")
                 << m_request.slot << m_request.outputPath;
            lines = consoleLines();
            break;
        case PivAction::GenerateCert:
            timeout = 180000;
            args << QStringLiteral("piv") << QStringLiteral("certificates") << QStringLiteral("generate")
                 << QStringLiteral("--subject") << m_request.subject
                 << QStringLiteral("--valid-days") << QString::number(m_request.validDays)
                 << m_request.slot;
            lines = consoleLines();
            break;
        case PivAction::DeleteCert:
            args << QStringLiteral("piv") << QStringLiteral("certificates") << QStringLiteral("delete")
                 << m_request.slot;
            lines = consoleLines();
            break;
        case PivAction::ChangeManagement:
            args << QStringLiteral("piv") << QStringLiteral("access") << QStringLiteral("change-management-key")
                 << QStringLiteral("--algorithm") << m_request.algorithm;
            if (m_request.generateManagement)
                args << QStringLiteral("--generate");
            if (m_request.managementTouch)
                args << QStringLiteral("--touch");
            if (m_request.protectManagement)
                args << QStringLiteral("--protect");
            lines = consoleLines();
            break;
        case PivAction::Reset:
            args << QStringLiteral("piv") << QStringLiteral("reset") << QStringLiteral("--force");
            break;
        }

        ConsoleResult result = runOnConsole(m_program, args, lines, timeout);
        for (QString &line : lines)
            wipeSecret(line);
        wipeSecret(m_request.current);
        wipeSecret(m_request.next);
        wipeSecret(m_request.pin);
        wipeSecret(m_request.managementKey);
        wipeSecret(m_request.newManagementKey);
        wipeSecret(m_request.filePassword);

        QString output = result.output;
        wipeSecret(result.output);
        bool ok = result.failure.isEmpty() && result.exitCode == 0;
        const bool keepOutput = ok && m_request.action == PivAction::ChangeManagement;
        QString detail = ok ? (keepOutput ? output : QString())
                            : (result.failure.isEmpty() ? output : result.failure);
        if (!sshTemp.isEmpty()) {
            if (ok) {
                QString convertError;
                if (!pemToOpenSsh(sshTemp, m_request.outputPath, m_request.comment, &convertError)) {
                    ok = false;
                    detail = m_request.action == PivAction::GenerateKey
                                 ? QStringLiteral("Der Schlüssel liegt im Slot. Die OpenSSH-Datei fehlt. %1")
                                       .arg(convertError)
                                 : convertError;
                } else {
                    detail = QStringLiteral("ssh");
                }
            }
            QFile::remove(sshTemp);
        }
        const int action = int(m_request.action);
        QMetaObject::invokeMethod(m_client, [client = m_client, ok, action, detail]() mutable {
            client->deliverPiv(ok, action, detail);
            detail.fill(QLatin1Char('0'));
            detail.clear();
        }, Qt::QueuedConnection);
        wipeSecret(output);
        wipeSecret(detail);
    }

    QStringList consoleLines() const
    {
        QStringList lines;
        const bool askCurrentKey = !m_request.managementDefault && !m_request.managementStored;
        if (m_request.action == PivAction::ImportCert && !m_request.filePassword.isEmpty())
            lines << m_request.filePassword;
        if (m_request.action == PivAction::ChangeManagement) {
            if (askCurrentKey)
                lines << m_request.managementKey;
            else if (m_request.managementStored)
                lines << m_request.pin;
            if (m_request.protectManagement && !m_request.managementStored)
                lines << m_request.pin;
            if (!m_request.generateManagement)
                lines << m_request.newManagementKey;
            return lines;
        }
        if (askCurrentKey)
            lines << m_request.managementKey;
        lines << m_request.pin;
        return lines;
    }

private:
    YkmanClient *m_client;
    QString m_program;
    QString m_serial;
    PivRequest m_request;
};

class OpenPgpWorker : public QThread
{
public:
    OpenPgpWorker(YkmanClient *client, QString program, QString serial, OpenPgpRequest request)
        : m_client(client)
        , m_program(std::move(program))
        , m_serial(std::move(serial))
        , m_request(std::move(request))
    {
    }

    void run() override
    {
        QStringList args{QStringLiteral("--color"), QStringLiteral("never"),
                         QStringLiteral("--device"), m_serial};
        QStringList lines;
        switch (m_request.action) {
        case OpenPgpAction::ChangePin:
            args << QStringLiteral("openpgp") << QStringLiteral("access") << QStringLiteral("change-pin");
            lines << m_request.current << m_request.next << m_request.next;
            break;
        case OpenPgpAction::ChangeAdmin:
            args << QStringLiteral("openpgp") << QStringLiteral("access") << QStringLiteral("change-admin-pin");
            lines << m_request.current << m_request.next << m_request.next;
            break;
        case OpenPgpAction::SetResetCode:
            // ykman asks for the new reset code twice, and only then for the admin PIN.
            args << QStringLiteral("openpgp") << QStringLiteral("access") << QStringLiteral("change-reset-code");
            lines << m_request.next << m_request.next << m_request.admin;
            break;
        case OpenPgpAction::SetSignature:
            args << QStringLiteral("openpgp") << QStringLiteral("access")
                 << QStringLiteral("set-signature-policy") << m_request.policy;
            lines << m_request.admin;
            break;
        case OpenPgpAction::SetTouch:
            args << QStringLiteral("openpgp") << QStringLiteral("keys") << QStringLiteral("set-touch")
                 << m_request.key << m_request.policy << QStringLiteral("--force");
            lines << m_request.admin;
            break;
        }

        ConsoleResult result = runOnConsole(m_program, args, lines, 90000);
        for (QString &line : lines)
            wipeSecret(line);
        wipeSecret(m_request.current);
        wipeSecret(m_request.next);
        wipeSecret(m_request.admin);

        QString output = result.output;
        wipeSecret(result.output);
        const bool ok = result.failure.isEmpty() && result.exitCode == 0;
        QString detail = ok ? QString() : (result.failure.isEmpty() ? output : result.failure);
        const int action = int(m_request.action);
        QMetaObject::invokeMethod(m_client, [client = m_client, ok, action, detail]() mutable {
            client->deliverOpenPgp(ok, action, detail);
            detail.fill(QLatin1Char('0'));
            detail.clear();
        }, Qt::QueuedConnection);
        wipeSecret(output);
        wipeSecret(detail);
    }

private:
    YkmanClient *m_client;
    QString m_program;
    QString m_serial;
    OpenPgpRequest m_request;
};

class SshWorker : public QThread
{
public:
    SshWorker(YkmanClient *client, QString program, SshRequest request)
        : m_client(client)
        , m_program(std::move(program))
        , m_request(std::move(request))
    {
    }

    void run() override
    {
        QStringList args;
        QStringList lines;
        QStringList hints;
        QString workDir;
        const bool download = m_request.action == SshAction::Download;
        if (download) {
            args << QStringLiteral("-K");
            lines << m_request.pin;
            hints << QStringLiteral("PIN");
            workDir = m_request.path;
        } else {
            args << QStringLiteral("-t") << m_request.type
                 << QStringLiteral("-f") << m_request.path
                 << QStringLiteral("-O")
                 << QStringLiteral("application=%1").arg(m_request.application);
            if (!m_request.comment.trimmed().isEmpty())
                args << QStringLiteral("-C") << m_request.comment.trimmed();
            if (m_request.resident)
                args << QStringLiteral("-O") << QStringLiteral("resident");
            if (m_request.verify)
                args << QStringLiteral("-O") << QStringLiteral("verify-required");
            args << QStringLiteral("-N") << QString();
            if (m_request.resident) {
                lines.prepend(m_request.pin);
                hints.prepend(QStringLiteral("PIN"));
            }
        }

        ConsoleResult result = runOnConsole(m_program, args, lines, 180000, workDir,
                                            QStringLiteral("ssh-keygen"), hints);
        for (QString &line : lines)
            wipeSecret(line);
        for (QString &arg : args)
            wipeSecret(arg);
        wipeSecret(m_request.pin);

        QString output = result.output;
        wipeSecret(result.output);
        const bool ok = result.failure.isEmpty() && result.exitCode == 0;
        QString detail;
        if (!ok) {
            detail = result.failure.isEmpty() ? output : result.failure;
        } else if (download) {
            QStringList saved;
            for (const QString &row : output.split(QLatin1Char('\n'))) {
                const QString trimmed = row.trimmed();
                if (trimmed.contains(QLatin1String("Saved"), Qt::CaseInsensitive))
                    saved << trimmed;
            }
            detail = saved.isEmpty()
                         ? QStringLiteral("Die Stub-Dateien wurden in den gewählten Ordner geschrieben.")
                         : saved.join(QLatin1Char('\n'));
        }
        const int action = int(m_request.action);
        QMetaObject::invokeMethod(m_client, [client = m_client, ok, action, detail]() mutable {
            client->deliverSsh(ok, action, detail);
            detail.fill(QLatin1Char('0'));
            detail.clear();
        }, Qt::QueuedConnection);
        wipeSecret(output);
        wipeSecret(detail);
    }

private:
    YkmanClient *m_client;
    QString m_program;
    SshRequest m_request;
};

} // namespace

void YkmanClient::deliverFidoPin(bool ok, const QString &detail)
{
    m_busy = false;
    m_pinThread = nullptr;
    emit fidoPinFinished(ok, detail);
}

void YkmanClient::changeFidoPin(const QString &serial, const QString &current, const QString &next, bool elevated)
{
    if (m_busy || serial.isEmpty() || next.isEmpty())
        return;

    if (m_snapshot.program.isEmpty())
        m_snapshot.program = locateProgram();
    if (m_snapshot.program.isEmpty()) {
        emit fidoPinFinished(false, missingProgramMessage());
        return;
    }

    m_busy = true;
    auto *worker = new FidoPinWorker(this, m_snapshot.program, QCoreApplication::applicationFilePath(),
                                     serial, current, next, elevated);
    m_pinThread = worker;
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void YkmanClient::deliverFidoReset(bool ok, const QString &detail)
{
    m_busy = false;
    m_pinThread = nullptr;
    emit fidoResetFinished(ok, detail);
}

void YkmanClient::resetFido(const QString &serial, bool elevated)
{
    if (m_busy || serial.isEmpty()) {
        emit fidoResetFinished(false, QStringLiteral("Der FIDO-Reset wurde nicht gestartet."));
        return;
    }

    if (m_snapshot.program.isEmpty())
        m_snapshot.program = locateProgram();
    if (m_snapshot.program.isEmpty()) {
        emit fidoResetFinished(false, missingProgramMessage());
        return;
    }

    m_busy = true;
    auto *worker = new FidoResetWorker(this, m_snapshot.program, QCoreApplication::applicationFilePath(),
                                       serial, elevated);
    m_pinThread = worker;
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void YkmanClient::deliverFidoCredentials(bool ok, int action, const QString &detail, const QString &output)
{
    m_busy = false;
    m_pinThread = nullptr;
    emit fidoCredentialsFinished(ok, action, detail, output);
}

void YkmanClient::runFidoCredentials(const QString &serial, FidoCredentialAction action, const QString &pin,
                                     const QString &credentialId, bool elevated)
{
    const bool idOk = QRegularExpression(QStringLiteral("^[0-9a-fA-F]+$")).match(credentialId).hasMatch();
    if (m_busy || serial.isEmpty() || pin.isEmpty()
        || (action == FidoCredentialAction::Delete && !idOk)) {
        emit fidoCredentialsFinished(false, int(action),
                                     QStringLiteral("Die FIDO-Anmeldedaten wurden nicht abgefragt."), QString());
        return;
    }

    if (m_snapshot.program.isEmpty())
        m_snapshot.program = locateProgram();
    if (m_snapshot.program.isEmpty()) {
        emit fidoCredentialsFinished(false, int(action), missingProgramMessage(), QString());
        return;
    }

    m_busy = true;
    auto *worker = new FidoCredentialWorker(this, m_snapshot.program, QCoreApplication::applicationFilePath(),
                                            serial, action, pin, credentialId, elevated);
    m_pinThread = worker;
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void YkmanClient::deliverOtp(bool ok, const QString &detail, const QString &revealed)
{
    m_busy = false;
    m_pinThread = nullptr;
    emit otpFinished(ok, detail, revealed);
}

void YkmanClient::runOtp(const QString &serial, const OtpRequest &request)
{
    const bool needsSlot = request.action != OtpAction::Swap;
    const bool needsSecret = request.action == OtpAction::StaticPassword || request.action == OtpAction::Hotp;
    if (m_busy || serial.isEmpty() || (needsSlot && (request.slot < 1 || request.slot > 2))
        || (needsSecret && request.secret.isEmpty())
        || (request.action == OtpAction::StaticPassword && request.layout.isEmpty())) {
        emit otpFinished(false, QStringLiteral("Die OTP-Änderung wurde nicht gestartet."), QString());
        return;
    }

    if (m_snapshot.program.isEmpty())
        m_snapshot.program = locateProgram();
    if (m_snapshot.program.isEmpty()) {
        emit otpFinished(false, missingProgramMessage(), QString());
        return;
    }

    m_busy = true;
    auto *worker = new OtpWorker(this, m_snapshot.program, serial, request);
    m_pinThread = worker;
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void YkmanClient::deliverOath(bool ok, int action, const QString &detail, const QString &output)
{
    QString body = output;
    if (ok && action == int(OathAction::List)) {
        m_snapshot.oath.accounts = parseOathList(body);
        m_snapshot.oath.loaded = true;
        m_snapshot.oath.error.clear();
    }
    m_busy = false;
    m_pinThread = nullptr;
    emit oathFinished(ok, action, detail, body);
    body.fill(QLatin1Char('0'));
    body.clear();
}

void YkmanClient::runOath(const QString &serial, const OathRequest &request)
{
    const bool needsQuery = request.action == OathAction::CodeOne || request.action == OathAction::Rename
                            || request.action == OathAction::Delete || request.action == OathAction::Add;
    if (m_busy || serial.isEmpty() || (needsQuery && request.query.isEmpty())
        || (request.action == OathAction::Add && request.secret.isEmpty())
        || (request.action == OathAction::Rename && request.newName.isEmpty())
        || (request.action == OathAction::SetPassword && request.newPassword.isEmpty())) {
        emit oathFinished(false, int(request.action),
                          QStringLiteral("Die OATH-Aktion wurde nicht gestartet."), QString());
        return;
    }

    if (m_snapshot.program.isEmpty())
        m_snapshot.program = locateProgram();
    if (m_snapshot.program.isEmpty()) {
        emit oathFinished(false, int(request.action), missingProgramMessage(), QString());
        return;
    }

    m_busy = true;
    auto *worker = new OathWorker(this, m_snapshot.program, serial, request);
    m_pinThread = worker;
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void YkmanClient::deliverPiv(bool ok, int action, const QString &detail)
{
    m_busy = false;
    m_pinThread = nullptr;
    emit pivFinished(ok, action, detail);
}

void YkmanClient::runPiv(const QString &serial, const PivRequest &request)
{
    const bool pair = request.action == PivAction::ChangePin || request.action == PivAction::ChangePuk
                      || request.action == PivAction::UnblockPin;
    const bool needsPin = request.action == PivAction::GenerateKey || request.action == PivAction::ImportCert
                          || request.action == PivAction::GenerateCert || request.action == PivAction::DeleteCert
                          || (request.action == PivAction::ChangeManagement
                              && (request.managementStored || request.protectManagement));
    const bool needsSlot = request.action == PivAction::GenerateKey || request.action == PivAction::ImportCert
                           || request.action == PivAction::GenerateCert || request.action == PivAction::DeleteCert
                           || request.action == PivAction::ExportKey;
    const bool needsFile = request.action == PivAction::GenerateKey || request.action == PivAction::ImportCert
                           || request.action == PivAction::ExportKey;
    if (m_busy || serial.isEmpty() || (pair && (request.current.isEmpty() || request.next.isEmpty()))
        || (needsPin && request.pin.isEmpty()) || (needsSlot && request.slot.isEmpty())
        || (needsFile && request.outputPath.isEmpty())
        || (request.action == PivAction::GenerateKey && request.algorithm.isEmpty())
        || (request.action == PivAction::GenerateCert && request.subject.isEmpty())
        || (request.action == PivAction::ChangeManagement && request.algorithm.isEmpty())
        || (request.action == PivAction::ChangeManagement && !request.generateManagement
            && request.newManagementKey.isEmpty())
        || (request.action == PivAction::ChangeManagement && !request.managementDefault
            && !request.managementStored && request.managementKey.isEmpty())) {
        emit pivFinished(false, int(request.action),
                         QStringLiteral("Die PIV-Änderung wurde nicht gestartet."));
        return;
    }

    if (m_snapshot.program.isEmpty())
        m_snapshot.program = locateProgram();
    if (m_snapshot.program.isEmpty()) {
        emit pivFinished(false, int(request.action), missingProgramMessage());
        return;
    }

    m_busy = true;
    auto *worker = new PivWorker(this, m_snapshot.program, serial, request);
    m_pinThread = worker;
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void YkmanClient::deliverOpenPgp(bool ok, int action, const QString &detail)
{
    m_busy = false;
    m_pinThread = nullptr;
    emit openpgpFinished(ok, action, detail);
}

void YkmanClient::runOpenPgp(const QString &serial, const OpenPgpRequest &request)
{
    const bool pinPair = request.action == OpenPgpAction::ChangePin
                         || request.action == OpenPgpAction::ChangeAdmin;
    const bool knownKey = request.key == QLatin1String("sig") || request.key == QLatin1String("dec")
                          || request.key == QLatin1String("aut") || request.key == QLatin1String("att");
    const bool knownTouch = request.policy == QLatin1String("off") || request.policy == QLatin1String("on")
                            || request.policy == QLatin1String("fixed") || request.policy == QLatin1String("cached")
                            || request.policy == QLatin1String("cached-fixed");
    const bool knownSignature = request.policy == QLatin1String("once") || request.policy == QLatin1String("always");
    if (m_busy || serial.isEmpty() || (pinPair && (request.current.isEmpty() || request.next.isEmpty()))
        || (request.action == OpenPgpAction::SetResetCode && (request.next.isEmpty() || request.admin.isEmpty()))
        || (request.action == OpenPgpAction::SetSignature && (request.admin.isEmpty() || !knownSignature))
        || (request.action == OpenPgpAction::SetTouch
            && (request.admin.isEmpty() || !knownKey || !knownTouch))) {
        emit openpgpFinished(false, int(request.action),
                             QStringLiteral("Die OpenPGP-Änderung wurde nicht gestartet."));
        return;
    }

    if (m_snapshot.program.isEmpty())
        m_snapshot.program = locateProgram();
    if (m_snapshot.program.isEmpty()) {
        emit openpgpFinished(false, int(request.action), missingProgramMessage());
        return;
    }

    m_busy = true;
    auto *worker = new OpenPgpWorker(this, m_snapshot.program, serial, request);
    m_pinThread = worker;
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void YkmanClient::deliverSsh(bool ok, int action, const QString &detail)
{
    m_busy = false;
    m_pinThread = nullptr;
    emit sshFinished(ok, action, detail);
}

void YkmanClient::runSsh(const SshRequest &request)
{
    const bool generate = request.action == SshAction::Generate;
    const bool download = request.action == SshAction::Download;
    const bool typeOk = request.type == QLatin1String("ed25519-sk")
                        || request.type == QLatin1String("ecdsa-sk");
    const bool applicationOk = request.application.startsWith(QLatin1String("ssh:"))
                               && !request.application.contains(QLatin1Char(' '))
                               && !request.application.contains(QLatin1Char('\n'))
                               && !request.application.contains(QLatin1Char('\r'));
    if (m_busy || (!generate && !download)
        || (generate && (!typeOk || request.path.isEmpty() || !applicationOk))
        || (download && request.path.isEmpty())
        || ((download || (generate && request.resident)) && request.pin.isEmpty())) {
        emit sshFinished(false, int(request.action),
                         QStringLiteral("Der SSH-Aufruf wurde nicht gestartet."));
        return;
    }

    const QString program = Settings::resolveSshKeygen();
    if (program.isEmpty()) {
        emit sshFinished(false, int(request.action), Settings::missingSshKeygenText());
        return;
    }

    m_busy = true;
    auto *worker = new SshWorker(this, program, request);
    m_pinThread = worker;
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;
class QThread;
class QTimer;

struct UsbApp
{
    QString id;
    QString name;
    QString usb;
    QString nfc;
};

struct DeviceRecord
{
    QString name;
    QString serial;
    QString version;
    QString formFactor;
    QStringList usbInterfaces;
    bool nfcCapable = false;
    bool nfcTransportEnabled = false;
    QString transport;
    bool locked = false;
    QList<UsbApp> applications;
};

struct OtpSlot
{
    int slot = 0;
    bool configured = false;
};

struct OtpRecord
{
    QString error;
    QList<OtpSlot> otpSlots;
};

struct FidoRecord
{
    QString error;
    QString aaguid;
    QString pin;
    QString minimumPinLength;
    QString alwaysUv;
    QString storageRemaining;
    QString fingerprints;
    bool pinSet = false;
    bool resetBlocked = false;
    int attemptsRemaining = -1;
    int minPinLength = -1;
};

struct OathAccount
{
    QString name;
    QString type;
    int period = 0;
};

struct OathRecord
{
    QString error;
    QString version;
    bool passwordProtected = false;
    bool passwordRemembered = false;
    bool loaded = false;
    QList<OathAccount> accounts;
};

enum class OathAction
{
    List,
    Code,
    CodeOne,
    Add,
    Rename,
    Delete,
    SetPassword,
    ClearPassword
};

struct OathRequest
{
    OathAction action = OathAction::List;
    QString query;
    QString newName;
    QString issuer;
    QString secret;
    QString password;
    QString newPassword;
    QString oathType;
    QString algorithm;
    bool touch = false;
    int digits = 6;
    int period = 30;
    int counter = 0;
};

struct PivSlotInfo
{
    QString slot;
    QString name;
    QString subject;
    QString expires;
    QString keyType;
};

struct PivRecord
{
    QString error;
    QString version;
    QString biometrics;
    QString managementAlgorithm;
    bool pinDefault = false;
    bool pukDefault = false;
    bool managementDefault = false;
    bool resetBlocked = false;
    int pinAttempts = -1;
    int pinTotal = -1;
    int pukAttempts = -1;
    int pukTotal = -1;
    QList<PivSlotInfo> entries;
};

enum class PivAction
{
    ChangePin,
    ChangePuk,
    UnblockPin,
    GenerateKey,
    ImportCert,
    GenerateCert,
    DeleteCert,
    ChangeManagement,
    Reset,
    ExportKey
};

struct PivRequest
{
    PivAction action = PivAction::ChangePin;
    QString current;
    QString next;
    QString pin;
    QString managementKey;
    QString newManagementKey;
    QString filePassword;
    QString slot;
    QString algorithm;
    QString pinPolicy;
    QString touchPolicy;
    QString outputPath;
    QString format;
    QString comment;
    QString subject;
    int validDays = 365;
    bool managementDefault = false;
    bool managementStored = false;
    bool generateManagement = false;
    bool protectManagement = false;
    bool managementTouch = false;
};

struct OpenPgpKey
{
    QString id;
    QString name;
    QString fingerprint;
    QString touch;
};

struct OpenPgpRecord
{
    QString error;
    QString version;
    QString application;
    QString signaturePolicy;
    bool kdf = false;
    int pinAttempts = -1;
    int resetAttempts = -1;
    int adminAttempts = -1;
    QList<OpenPgpKey> keys;
};

enum class OpenPgpAction
{
    ChangePin,
    ChangeAdmin,
    SetResetCode,
    SetSignature,
    SetTouch
};

struct OpenPgpRequest
{
    OpenPgpAction action = OpenPgpAction::ChangePin;
    QString current;
    QString next;
    QString admin;
    QString key;
    QString policy;
};

struct FidoCredential
{
    QString id;
    QString rpId;
    QString userName;
    QString displayName;
};

enum class FidoCredentialAction
{
    List,
    Delete
};

struct YkmanSnapshot
{
    QString program;
    QString version;
    QString error;
    QList<DeviceRecord> devices;
    OtpRecord otp;
    FidoRecord fido;
    OathRecord oath;
    PivRecord piv;
    OpenPgpRecord openpgp;
};

enum class OtpAction
{
    Delete,
    Swap,
    YubiOtp,
    ChalResp,
    StaticPassword,
    Hotp
};

enum class SshAction
{
    Generate,
    Download
};

struct SshRequest
{
    SshAction action = SshAction::Generate;
    QString type;
    QString path;
    QString comment;
    QString application;
    QString pin;
    bool resident = true;
    bool verify = false;
};

struct OtpRequest
{
    bool accepted = false;
    OtpAction action = OtpAction::Delete;
    int slot = 1;
    bool touch = true;
    int digits = 6;
    QString layout;
    QString secret;
    QString accessCode;
};

class YkmanClient : public QObject
{
    Q_OBJECT

public:
    explicit YkmanClient(QObject *parent = nullptr);
    ~YkmanClient() override;

    void refresh(const QString &preferredSerial);
    void changeFidoPin(const QString &serial, const QString &current, const QString &next, bool elevated);
    void deliverFidoPin(bool ok, const QString &detail);
    void resetFido(const QString &serial, bool elevated);
    void deliverFidoReset(bool ok, const QString &detail);
    void runFidoCredentials(const QString &serial, FidoCredentialAction action, const QString &pin,
                            const QString &credentialId, bool elevated);
    void deliverFidoCredentials(bool ok, int action, const QString &detail, const QString &output);
    void runOtp(const QString &serial, const OtpRequest &request);
    void deliverOtp(bool ok, const QString &detail, const QString &revealed);
    void runOath(const QString &serial, const OathRequest &request);
    void deliverOath(bool ok, int action, const QString &detail, const QString &output);
    void runPiv(const QString &serial, const PivRequest &request);
    void deliverPiv(bool ok, int action, const QString &detail);
    void runOpenPgp(const QString &serial, const OpenPgpRequest &request);
    void deliverOpenPgp(bool ok, int action, const QString &detail);
    void runSsh(const SshRequest &request);
    void deliverSsh(bool ok, int action, const QString &detail);
    void setApplications(const QString &serial,
                         const QStringList &usbEnable,
                         const QStringList &usbDisable,
                         const QStringList &nfcEnable,
                         const QStringList &nfcDisable,
                         const QString &lockCode);
    bool busy() const;
    const YkmanSnapshot &snapshot() const;

signals:
    void refreshFinished();
    void lockCodeNeeded(const QString &detail);
    void fidoPinFinished(bool ok, const QString &detail);
    void fidoResetFinished(bool ok, const QString &detail);
    void fidoCredentialsFinished(bool ok, int action, const QString &detail, const QString &output);
    void otpFinished(bool ok, const QString &detail, const QString &revealed);
    void oathFinished(bool ok, int action, const QString &detail, const QString &output);
    void pivFinished(bool ok, int action, const QString &detail);
    void openpgpFinished(bool ok, int action, const QString &detail);
    void sshFinished(bool ok, int action, const QString &detail);

private:
    enum class Step
    {
        Version,
        List,
        Info,
        Otp,
        Fido,
        OathInfo,
        OathList,
        Piv,
        OpenPgp,
        Usb,
        Nfc,
        Probe
    };

    struct Command
    {
        Step step;
        QStringList args;
    };

    void startNext();
    void probeDevice();
    void fail(const QString &message);
    void handleOutput(int exitCode);

    QProcess *m_process = nullptr;
    QTimer *m_timeout = nullptr;
    QList<Command> m_queue;
    Command m_current{};
    QString m_stdout;
    QString m_stderr;
    QString m_preferredSerial;
    YkmanSnapshot m_snapshot;
    bool m_busy = false;
    int m_probeAttempts = 0;
    QThread *m_pinThread = nullptr;
};

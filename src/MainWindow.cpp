#include "MainWindow.h"

#include "Settings.h"

#include <QApplication>
#include <QDir>
#include <QCheckBox>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QGuiApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QString dash()
{
    return QStringLiteral("—");
}

bool onReader(const DeviceRecord &device)
{
    return device.transport.compare(QLatin1String("nfc"), Qt::CaseInsensitive) == 0;
}

QString connectionLabel(const DeviceRecord &device)
{
    if (onReader(device))
        return QStringLiteral("Lesegerät");
    if (device.transport.compare(QLatin1String("usb"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("USB");
    return dash();
}

QString connectedMessage(const DeviceRecord &device)
{
    if (onReader(device))
        return QStringLiteral("Verbunden über das Lesegerät.");
    if (device.transport.compare(QLatin1String("usb"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("Verbunden über USB.");
    return QStringLiteral("Verbunden.");
}

enum SwitchMark
{
    SwitchOff = 0,
    SwitchOn = 1
};

class SwitchMarkDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const QVariant mark = index.data(Qt::UserRole);
        if (!mark.isValid()) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        opt.text.clear();
        opt.icon = QIcon();
        QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &opt, painter);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        const bool on = mark.toInt() == SwitchOn;
        QPen pen(on ? QColor(0x2F, 0x6F, 0x4E) : QColor(0x8A, 0x96, 0xA3));
        pen.setWidthF(1.7);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        const QPointF center = QRectF(option.rect).center();
        if (on) {
            QPainterPath path;
            path.moveTo(center.x() - 5.4, center.y() + 0.2);
            path.lineTo(center.x() - 1.6, center.y() + 3.8);
            path.lineTo(center.x() + 5.8, center.y() - 4.2);
            painter->drawPath(path);
        } else {
            const qreal arm = 3.5;
            painter->drawLine(center + QPointF(-arm, -arm), center + QPointF(arm, arm));
            painter->drawLine(center + QPointF(-arm, arm), center + QPointF(arm, -arm));
        }
        painter->restore();
    }
};

QTableWidgetItem *stateCell(const QString &state)
{
    auto *item = new QTableWidgetItem;
    item->setFlags(Qt::ItemIsEnabled);
    item->setTextAlignment(Qt::AlignCenter);

    if (state == QLatin1String("enabled") || state == QLatin1String("disabled")) {
        const bool on = state == QLatin1String("enabled");
        const QString meaning = on ? QStringLiteral("Ein") : QStringLiteral("Aus");
        item->setData(Qt::UserRole, on ? SwitchOn : SwitchOff);
        item->setData(Qt::AccessibleTextRole, meaning);
        item->setToolTip(meaning);
        return item;
    }

    item->setText(state.isEmpty() ? dash() : state);
    if (!state.isEmpty())
        item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    return item;
}

void setApplicationColumns(QTableWidget *table, bool nfc)
{
    const QStringList headers = nfc
                                    ? QStringList{QStringLiteral("Anwendung"), QStringLiteral("USB"), QStringLiteral("NFC")}
                                    : QStringList{QStringLiteral("Anwendung"), QStringLiteral("USB")};
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < headers.size(); ++column) {
        table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
        if (QTableWidgetItem *header = table->horizontalHeaderItem(column))
            header->setTextAlignment(Qt::AlignCenter);
    }
}

QString slotState(const OtpSlot &slot)
{
    return slot.configured ? QStringLiteral("Programmiert") : QStringLiteral("Leer");
}

QString onOff(const QString &value)
{
    if (value.compare(QLatin1String("on"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("Ein");
    if (value.compare(QLatin1String("off"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("Aus");
    return value.isEmpty() ? dash() : value;
}

QString pinLabel(const QString &pin)
{
    if (pin.isEmpty())
        return dash();
    if (pin.contains(QLatin1String("not set"), Qt::CaseInsensitive))
        return QStringLiteral("Nicht gesetzt");
    const QRegularExpressionMatch attempts = QRegularExpression(QStringLiteral("^(\\d+)\\s+attempt")).match(pin);
    if (attempts.hasMatch()) {
        const int count = attempts.captured(1).toInt();
        if (count == 0)
            return QStringLiteral("Gesperrt, keine Versuche mehr");
        if (count == 1)
            return QStringLiteral("Gesetzt, 1 Versuch übrig");
        return QStringLiteral("Gesetzt, %1 Versuche übrig").arg(count);
    }
    if (pin.contains(QLatin1String("must be changed"), Qt::CaseInsensitive))
        return QStringLiteral("Gesetzt, muss vor der nächsten Nutzung geändert werden");
    return pin;
}

QString fingerprintLabel(const QString &value)
{
    if (value.contains(QLatin1String("not registered"), Qt::CaseInsensitive))
        return QStringLiteral("Keine");
    return value.isEmpty() ? dash() : value;
}

void wipeSecret(QString &text)
{
    text.fill(QLatin1Char('0'));
    text.clear();
}

bool fidoAccessDenied(const QString &detail)
{
    const QString text = detail.toLower();
    return text.contains(QLatin1String("access is denied"))
           || text.contains(QLatin1String("requires elevation"))
           || text.contains(QLatin1String("0x80070005"))
           || text.contains(QLatin1String("administrator"));
}

QString fidoPinText(bool ok, const QString &detail)
{
    if (ok)
        return QStringLiteral("Der FIDO-PIN wurde geändert.");
    const QRegularExpressionMatch wrong =
        QRegularExpression(QStringLiteral("Wrong PIN,\\s*(\\d+)\\s+attempt")).match(detail);
    if (wrong.hasMatch()) {
        const int count = wrong.captured(1).toInt();
        if (count == 0)
            return QStringLiteral("Falscher PIN. Keine Versuche mehr. Nur ein FIDO-Reset hilft, und der löscht die Anmeldedaten.");
        if (count == 1)
            return QStringLiteral("Falscher PIN. Noch 1 Versuch.");
        return QStringLiteral("Falscher PIN. Noch %1 Versuche.").arg(count);
    }
    if (detail.contains(QLatin1String("Wrong PIN"), Qt::CaseInsensitive))
        return QStringLiteral("Falscher PIN. Ein Fehlversuch verbraucht einen.");
    if (detail.contains(QLatin1String("PIN is blocked"), Qt::CaseInsensitive))
        return QStringLiteral("Der PIN ist gesperrt. Nur ein FIDO-Reset setzt ihn zurück und löscht die Anmeldedaten.");
    if (detail.contains(QLatin1String("re-insert"), Qt::CaseInsensitive))
        return QStringLiteral(
            "Die PIN-Prüfung ist kurz gesperrt. Den Key abziehen oder vom Lesegerät nehmen und wieder anlegen.");
    if (detail.contains(QLatin1String("complexity"), Qt::CaseInsensitive))
        return QStringLiteral("Der neue PIN erfüllt die Vorgaben nicht.");
    if (detail.contains(QLatin1String("do not match"), Qt::CaseInsensitive))
        return QStringLiteral("Die beiden neuen PINs stimmen nicht überein.");
    if (detail.isEmpty())
        return QStringLiteral("Der FIDO-PIN wurde nicht geändert.");
    return detail;
}

bool serialOnReader(const YkmanSnapshot &snap, const QString &serial)
{
    for (const DeviceRecord &device : snap.devices) {
        if (device.serial == serial)
            return onReader(device);
    }
    return false;
}

QString fidoCredentialText(const QString &detail, bool reader)
{
    const QString text = detail.toLower();
    if (text.contains(QLatin1String("notallowed")) || text.contains(QLatin1String("0x30"))) {
        if (reader)
            return QStringLiteral(
                "Der Key hat das Auslesen abgelehnt. Über das Lesegerät gilt die Freigabe nur direkt nach dem Auflegen, und die ist verbraucht. Den Key per USB anstecken und die Anmeldedaten erneut öffnen. Der PIN war gültig.");
        return QStringLiteral(
            "Der Key hat das Auslesen abgelehnt. Abziehen, wieder einstecken und die Anmeldedaten sofort erneut öffnen. Der PIN war gültig.");
    }
    return fidoPinText(false, detail);
}

QString fidoResetText(bool ok, const QString &detail)
{
    if (ok)
        return QStringLiteral("Die FIDO-Anwendung wurde zurückgesetzt.");
    const QString text = detail.toLower();
    if (text.contains(QLatin1String("within 5 seconds")))
        return QStringLiteral(
            "Der Reset kam zu spät. Den Key abziehen oder vom Lesegerät nehmen, wieder anlegen und den Reset sofort erneut starten.");
    if (text.contains(QLatin1String("touch")))
        return QStringLiteral("Der Reset wurde nicht bestätigt. Den Key berühren, solange er blinkt. Bleibt das Licht an, den Kontakt halten.");
    if (text.contains(QLatin1String("piv is configured")) || text.contains(QLatin1String("factory reset")))
        return QStringLiteral("Der FIDO-Reset ist gesperrt, solange PIV eingerichtet ist. Ein vollständiger Werksreset würde den ganzen Key leeren.");
    if (text.contains(QLatin1String("not enabled")))
        return QStringLiteral("FIDO2 ist auf diesem Key aus.");
    if (text.contains(QLatin1String("reinsert")) || text.contains(QLatin1String("wrong device")))
        return QStringLiteral(
            "Der Key wurde danach nicht wiedererkannt. Denselben Key einstecken oder auf das Lesegerät legen und erneut versuchen.");
    if (text.contains(QLatin1String("aborted")))
        return QStringLiteral("Der FIDO-Reset wurde abgebrochen.");
    if (text.contains(QLatin1String("nicht rechtzeitig")))
        return QStringLiteral("Die Zeit zum Abziehen und Einstecken ist abgelaufen. Der Key wurde nicht zurückgesetzt.");
    if (detail.trimmed().isEmpty())
        return QStringLiteral("Der FIDO-Reset ist fehlgeschlagen.");
    return detail.trimmed();
}

struct PinChoice
{
    bool accepted = false;
    QString current;
    QString next;
};

PinChoice askFidoPin(QWidget *parent, const FidoRecord &fido)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(fido.pinSet ? QStringLiteral("FIDO-PIN ändern") : QStringLiteral("FIDO-PIN setzen"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *info = new QLabel;
    info->setWordWrap(true);
    if (fido.pinSet && fido.attemptsRemaining == 1) {
        info->setText(QStringLiteral(
            "Noch 1 Versuch. Ein Fehlversuch sperrt den PIN. Ein Reset löscht die Anmeldedaten."));
    } else if (fido.pinSet) {
        info->setText(QStringLiteral("Noch %1 Versuche. Ein Fehlversuch verbraucht einen davon.")
                          .arg(fido.attemptsRemaining));
    } else {
        info->setText(QStringLiteral("Es ist noch kein PIN gesetzt. Mindestens %1 Zeichen.")
                          .arg(fido.minPinLength));
    }
    layout->addWidget(info);

    auto *current = new QLineEdit;
    auto *next = new QLineEdit;
    auto *repeat = new QLineEdit;
    for (QLineEdit *edit : {current, next, repeat})
        edit->setEchoMode(QLineEdit::Password);
    auto *form = new QFormLayout;
    if (fido.pinSet)
        form->addRow(QStringLiteral("Bisheriger PIN"), current);
    form->addRow(QStringLiteral("Neuer PIN"), next);
    form->addRow(QStringLiteral("Wiederholen"), repeat);
    layout->addLayout(form);

    auto *problem = new QLabel;
    problem->setWordWrap(true);
    layout->addWidget(problem);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(fido.pinSet ? QStringLiteral("Ändern")
                                                               : QStringLiteral("Setzen"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Abbrechen"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (fido.pinSet && current->text().isEmpty()) {
            problem->setText(QStringLiteral("Der bisherige PIN fehlt."));
            return;
        }
        if (next->text() != repeat->text()) {
            problem->setText(QStringLiteral("Die beiden neuen PINs stimmen nicht überein."));
            return;
        }
        if (next->text().size() < fido.minPinLength) {
            problem->setText(QStringLiteral("Der neue PIN braucht mindestens %1 Zeichen.").arg(fido.minPinLength));
            return;
        }
        if (next->text().toUtf8().size() > 63 || next->text().contains(QLatin1Char('\n'))
            || next->text().contains(QLatin1Char('\r'))) {
            problem->setText(QStringLiteral("Der neue PIN ist zu lang oder enthält einen Zeilenumbruch."));
            return;
        }
        dialog.accept();
    });

    PinChoice choice;
    if (dialog.exec() == QDialog::Accepted) {
        choice.accepted = true;
        choice.current = current->text();
        choice.next = next->text();
    }
    current->clear();
    next->clear();
    repeat->clear();
    return choice;
}

void wipeEdit(QLineEdit *edit)
{
    edit->setText(QString(edit->text().size(), QLatin1Char('0')));
    edit->clear();
}

QString askFidoVerifyPin(QWidget *parent, const FidoRecord &fido, const QString &title, const QString &extra,
                         const QString &acceptLabel)
{
    if (!fido.pinSet || fido.attemptsRemaining < 1)
        return {};
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setMinimumWidth(440);
    auto *layout = new QVBoxLayout(&dialog);
    auto *info = new QLabel;
    info->setWordWrap(true);
    QString text = fido.attemptsRemaining == 1
                       ? QStringLiteral("Noch 1 Versuch. Ein Fehlversuch sperrt den PIN. Ein Reset löscht die Anmeldedaten.")
                       : QStringLiteral("Noch %1 Versuche. Ein Fehlversuch verbraucht einen davon.")
                             .arg(fido.attemptsRemaining);
    if (!extra.isEmpty())
        text += QLatin1Char(' ') + extra;
    text += QStringLiteral(" Der PIN geht an die Konsole von ykman und steht nicht in der Befehlszeile.");
    info->setText(text);
    layout->addWidget(info);
    auto *pin = new QLineEdit;
    pin->setEchoMode(QLineEdit::Password);
    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("PIN"), pin);
    layout->addLayout(form);
    auto *problem = new QLabel;
    problem->setWordWrap(true);
    layout->addWidget(problem);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(acceptLabel);
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Abbrechen"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (pin->text().isEmpty()) {
            problem->setText(QStringLiteral("Der PIN fehlt."));
            return;
        }
        if (pin->text().toUtf8().size() > 63 || pin->text().contains(QLatin1Char('\n'))
            || pin->text().contains(QLatin1Char('\r'))) {
            problem->setText(QStringLiteral("Der PIN ist zu lang oder enthält einen Zeilenumbruch."));
            return;
        }
        dialog.accept();
    });
    QString value;
    if (dialog.exec() == QDialog::Accepted)
        value = pin->text();
    wipeEdit(pin);
    return value;
}

struct FidoPrint
{
    QString id;
    QString name;
};

bool fidoTemplateId(const QString &id)
{
    return QRegularExpression(QStringLiteral("^[0-9a-fA-F]+$")).match(id).hasMatch() && id.size() % 2 == 0;
}

QString fidoPrintNameProblem(const QString &name)
{
    if (name.isEmpty())
        return QStringLiteral("Der Name fehlt.");
    if (name.startsWith(QLatin1Char('-')))
        return QStringLiteral("Der Name darf nicht mit einem Strich beginnen.");
    if (name.contains(QLatin1Char('\n')) || name.contains(QLatin1Char('\r')))
        return QStringLiteral("Der Name darf keinen Zeilenumbruch enthalten.");
    if (name.toUtf8().size() > 15)
        return QStringLiteral("Der Name darf höchstens 15 Bytes lang sein.");
    return {};
}

QString askFidoPrintName(QWidget *parent, const QString &title, const QString &current)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setMinimumWidth(420);
    auto *layout = new QVBoxLayout(&dialog);
    auto *info = new QLabel(QStringLiteral(
        "Ein kurzer Name, höchstens 15 Bytes. Zum Beispiel „linker Daumen“."));
    info->setWordWrap(true);
    layout->addWidget(info);
    auto *name = new QLineEdit(current);
    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Name"), name);
    layout->addLayout(form);
    auto *problem = new QLabel;
    problem->setWordWrap(true);
    layout->addWidget(problem);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Weiter"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Abbrechen"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        const QString issue = fidoPrintNameProblem(name->text().trimmed());
        if (!issue.isEmpty()) {
            problem->setText(issue);
            return;
        }
        dialog.accept();
    });
    QString value;
    if (dialog.exec() == QDialog::Accepted)
        value = name->text().trimmed();
    name->clear();
    return value;
}

int askFidoMinLength(QWidget *parent, int current, int ceiling)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Mindestlänge"));
    dialog.setMinimumWidth(440);
    auto *layout = new QVBoxLayout(&dialog);
    auto *info = new QLabel(QStringLiteral(
        "Die Mindestlänge lässt sich nur erhöhen. Kürzer geht nur über einen FIDO-Reset, "
        "und der löscht die Anmeldedaten."));
    info->setWordWrap(true);
    layout->addWidget(info);
    auto *length = new QSpinBox;
    length->setRange(current + 1, ceiling);
    length->setValue(qMin(current + 1, ceiling));
    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Neue Mindestlänge"), length);
    layout->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Setzen"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Abbrechen"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return 0;
    return length->value();
}

QList<FidoPrint> parseFidoPrints(const QString &text)
{
    QList<FidoPrint> prints;
    const QRegularExpression lineRe(QStringLiteral("^ID:\\s*([0-9a-fA-F]+)(?:\\s+\\((.*)\\))?$"));
    for (QString line : text.split(QLatin1Char('\n'))) {
        line = line.trimmed();
        const QRegularExpressionMatch match = lineRe.match(line);
        if (!match.hasMatch() || !fidoTemplateId(match.captured(1)))
            continue;
        FidoPrint print;
        print.id = match.captured(1).toLower();
        print.name = match.captured(2).trimmed();
        prints.append(print);
    }
    return prints;
}

QString fidoConfigText(bool ok, FidoConfigAction action, const QString &detail, const QString &output)
{
    if (ok) {
        switch (action) {
        case FidoConfigAction::ToggleAlwaysUv:
            if (output.contains(QLatin1String("is off"), Qt::CaseInsensitive))
                return QStringLiteral("Die Nutzerprüfung ist nicht mehr bei jeder Anmeldung verlangt.");
            return QStringLiteral("Die Nutzerprüfung ist jetzt bei jeder Anmeldung verlangt.");
        case FidoConfigAction::SetMinLength:
            return QStringLiteral("Die Mindestlänge wurde gesetzt. Kürzer geht nur über einen FIDO-Reset.");
        case FidoConfigAction::ForceChange:
            return QStringLiteral("Der PIN muss vor der nächsten Nutzung geändert werden.");
        case FidoConfigAction::FingerprintsAdd:
            return QStringLiteral("Der Fingerabdruck wurde gespeichert. Die Liste neu öffnen.");
        case FidoConfigAction::FingerprintsRename:
            return QStringLiteral("Der Fingerabdruck wurde umbenannt.");
        case FidoConfigAction::FingerprintsDelete:
            return QStringLiteral("Der Fingerabdruck wurde gelöscht.");
        case FidoConfigAction::FingerprintsList:
            return {};
        }
    }
    const QString text = detail.toLower();
    if (text.contains(QLatin1String("can not be disabled")))
        return QStringLiteral("Auf diesem Key lässt sich die dauernde Nutzerprüfung nicht wieder ausschalten.");
    if (text.contains(QLatin1String("always require uv is not supported")))
        return QStringLiteral("Dieser Key kann die dauernde Nutzerprüfung nicht.");
    if (text.contains(QLatin1String("force change pin is not supported")))
        return QStringLiteral("Dieser Key kann keine erzwungene PIN-Änderung.");
    if (text.contains(QLatin1String("minimum pin length is not supported"))
        || text.contains(QLatin1String("set minimum pin length is not supported")))
        return QStringLiteral("Dieser Key kann die Mindestlänge nicht ändern.");
    if (text.contains(QLatin1String("authenticator configuration is not supported")))
        return QStringLiteral("Dieser Key kann die FIDO-Einstellungen nicht ändern.");
    if (text.contains(QLatin1String("shorter than")))
        return QStringLiteral("Die Mindestlänge lässt sich nur erhöhen.");
    if (text.contains(QLatin1String("longer than 8")))
        return QStringLiteral("Auf diesem Key darf die Mindestlänge höchstens 8 sein, solange PIV an ist.");
    if (text.contains(QLatin1String("biometrics is not supported")))
        return QStringLiteral("Dieser Key hat keinen Fingerabdrucksensor.");
    if (text.contains(QLatin1String("storage full")))
        return QStringLiteral("Der Speicher für Fingerabdrücke ist voll. Zuerst einen entfernen.");
    if (text.contains(QLatin1String("inactivity")))
        return QStringLiteral("Die Aufnahme wurde abgebrochen, weil der Sensor zu lange leer blieb.");
    if (text.contains(QLatin1String("no fingerprint matching")))
        return QStringLiteral("Dieser Fingerabdruck ist nicht mehr auf dem Key.");
    if (text.contains(QLatin1String("change the pin first")) || text.contains(QLatin1String("pin is blocked")))
        return QStringLiteral("Zuerst den PIN ändern. Bis dahin sind die übrigen FIDO-Aktionen gesperrt.");
    if (text.contains(QLatin1String("requires having a pin")))
        return QStringLiteral("Zuerst einen FIDO-PIN setzen.");
    if (text.contains(QLatin1String("failed to add fingerprint")))
        return QStringLiteral("Der Fingerabdruck wurde nicht gespeichert.");
    const QString pin = fidoPinText(false, detail);
    if (pin != detail)
        return pin;
    if (detail.trimmed().isEmpty())
        return QStringLiteral("Die FIDO-Einstellung wurde nicht geändert.");
    return detail.trimmed();
}

QStringList parseCsvFields(const QString &line)
{
    QStringList fields;
    QString field;
    bool quoted = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (quoted) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) {
                    field.append(c);
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field.append(c);
            }
        } else if (c == QLatin1Char('"')) {
            quoted = true;
        } else if (c == QLatin1Char(',')) {
            fields.append(field);
            field.clear();
        } else {
            field.append(c);
        }
    }
    fields.append(field);
    return fields;
}

QList<FidoCredential> parseFidoCredentials(const QString &text)
{
    QList<FidoCredential> credentials;
    const QRegularExpression hex(QStringLiteral("^[0-9a-fA-F]+$"));
    for (QString line : text.split(QRegularExpression(QStringLiteral("\\r?\\n")))) {
        line = line.trimmed();
        if (line.isEmpty())
            continue;
        const QStringList fields = parseCsvFields(line);
        if (fields.size() < 5 || !hex.match(fields.at(0)).hasMatch())
            continue;
        FidoCredential credential;
        credential.id = fields.at(0);
        credential.rpId = fields.at(1);
        credential.userName = fields.at(2);
        credential.displayName = fields.at(3);
        credentials.append(credential);
    }
    return credentials;
}

void wipeRequest(OtpRequest &request)
{
    wipeSecret(request.secret);
    wipeSecret(request.accessCode);
}

bool confirmSerial(QWidget *parent, const QString &serial, const QString &title, const QString &text,
                   const QString &acceptLabel)
{
    if (serial.isEmpty())
        return false;

    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    auto *layout = new QVBoxLayout(&dialog);
    auto *info = new QLabel(text);
    info->setWordWrap(true);
    layout->addWidget(info);

    auto *typed = new QLineEdit;
    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Seriennummer"), typed);
    layout->addLayout(form);

    auto *problem = new QLabel;
    problem->setWordWrap(true);
    layout->addWidget(problem);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(acceptLabel);
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Abbrechen"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (typed->text().trimmed() != serial) {
            problem->setText(QStringLiteral("Die Seriennummer stimmt nicht."));
            return;
        }
        dialog.accept();
    });
    return dialog.exec() == QDialog::Accepted;
}

QString askAccessCode(QWidget *parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Zugriffscode"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *info = new QLabel(QStringLiteral(
        "Dieser Slot ist mit einem Zugriffscode geschützt. Sechs Byte, als 12 Hex-Zeichen. "
        "ykman nimmt den Code nur als Argument. Er steht für die Dauer dieses Aufrufs in der Prozessliste "
        "und wird nicht protokolliert."));
    info->setWordWrap(true);
    layout->addWidget(info);

    auto *code = new QLineEdit;
    code->setEchoMode(QLineEdit::Password);
    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Zugriffscode"), code);
    layout->addLayout(form);

    auto *problem = new QLabel;
    problem->setWordWrap(true);
    layout->addWidget(problem);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Erneut versuchen"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Abbrechen"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        const QRegularExpression hex(QStringLiteral("^[0-9a-fA-F]{12}$"));
        if (!hex.match(code->text().trimmed()).hasMatch()) {
            problem->setText(QStringLiteral("Der Zugriffscode braucht genau 12 Hex-Zeichen."));
            return;
        }
        dialog.accept();
    });

    QString value;
    if (dialog.exec() == QDialog::Accepted)
        value = code->text().trimmed();
    wipeEdit(code);
    return value;
}

void showGeneratedSecret(QWidget *parent, const QString &text)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Einmal anzeigen"));
    dialog.resize(460, 240);
    auto *layout = new QVBoxLayout(&dialog);
    auto *info = new QLabel(QStringLiteral(
        "Diese Werte stehen nur hier. Der Key gibt sie nicht wieder her. Jetzt kopieren."));
    info->setWordWrap(true);
    layout->addWidget(info);
    auto *view = new QPlainTextEdit(text);
    view->setReadOnly(true);
    view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(view, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("Schließen"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::accept);
    dialog.exec();
    view->setPlainText(QString(text.size(), QLatin1Char('0')));
    view->clear();
}

QString otpSuccessText(OtpAction action, int slot)
{
    if (action == OtpAction::Swap)
        return QStringLiteral("Die beiden Slots wurden getauscht.");
    if (action == OtpAction::Delete)
        return QStringLiteral("Slot %1 wurde geleert.").arg(slot);
    return QStringLiteral("Slot %1 wurde programmiert.").arg(slot);
}

QString otpFailureText(const QString &detail)
{
    if (detail.contains(QLatin1String("access code"), Qt::CaseInsensitive))
        return QStringLiteral("Der Slot bleibt geschützt. Der Zugriffscode wurde nicht angenommen.");
    if (detail.contains(QLatin1String("empty slot"), Qt::CaseInsensitive))
        return QStringLiteral("Ein leerer Slot lässt sich nicht löschen.");
    if (detail.contains(QLatin1String("not supported"), Qt::CaseInsensitive))
        return QStringLiteral("Ein Zeichen passt nicht zum gewählten Tastaturlayout.");
    if (detail.contains(QLatin1String("too long"), Qt::CaseInsensitive))
        return QStringLiteral("Das Passwort ist zu lang.");
    if (detail.contains(QLatin1String("hex"), Qt::CaseInsensitive))
        return QStringLiteral("Der Schlüssel oder der Zugriffscode ist kein gültiges Hex.");
    if (detail.contains(QLatin1String("not found"), Qt::CaseInsensitive))
        return QStringLiteral("Der Key war während der Änderung nicht erreichbar.");
    if (detail.trimmed().isEmpty())
        return QStringLiteral("Die OTP-Änderung ist fehlgeschlagen.");
    return detail.trimmed();
}

OtpRequest askOtpProgram(QWidget *parent, int slot, bool overwrite, const QString &serial)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Slot %1 programmieren").arg(slot));
    dialog.setMinimumWidth(460);
    auto *layout = new QVBoxLayout(&dialog);

    auto *info = new QLabel(overwrite
        ? QStringLiteral("Slot %1 ist belegt. Die bisherige Konfiguration wird ersetzt und lässt sich "
                         "nicht wiederherstellen. Zum Bestätigen die Seriennummer eintippen.")
              .arg(slot)
        : QStringLiteral("Slot %1 ist leer. ykman schreibt erst, wenn das hier bestätigt wird.").arg(slot));
    info->setWordWrap(true);
    layout->addWidget(info);

    auto *serialEdit = new QLineEdit;
    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Seriennummer"), serialEdit);
    if (!overwrite) {
        serialEdit->hide();
        if (QWidget *label = form->labelForField(serialEdit))
            label->hide();
    }

    auto *type = new QComboBox;
    type->addItem(QStringLiteral("Yubico OTP"), int(OtpAction::YubiOtp));
    type->addItem(QStringLiteral("Challenge-Response"), int(OtpAction::ChalResp));
    type->addItem(QStringLiteral("Statisches Passwort"), int(OtpAction::StaticPassword));
    type->addItem(QStringLiteral("HOTP"), int(OtpAction::Hotp));
    form->addRow(QStringLiteral("Art"), type);
    layout->addLayout(form);

    auto *yubico = new QLabel(QStringLiteral(
        "Die öffentliche ID kommt aus der Seriennummer. Private ID und Schlüssel erzeugt ykman. "
        "Beide Werte erscheinen danach einmal zum Sichern."));
    yubico->setWordWrap(true);

    auto *touch = new QCheckBox(QStringLiteral("Erst nach einer Berührung rechnen"));
    touch->setChecked(true);
    auto *challenge = new QWidget;
    auto *challengeLayout = new QVBoxLayout(challenge);
    challengeLayout->setContentsMargins(0, 0, 0, 0);
    auto *challengeNote = new QLabel(QStringLiteral(
        "Den Schlüssel erzeugt ykman. Er erscheint danach einmal. "
        "Die Berührung gilt für die Berechnung, nicht für die kurze oder lange Auslösung."));
    challengeNote->setWordWrap(true);
    challengeLayout->addWidget(challengeNote);
    challengeLayout->addWidget(touch);

    auto *password = new QLineEdit;
    auto *repeat = new QLineEdit;
    password->setEchoMode(QLineEdit::Password);
    repeat->setEchoMode(QLineEdit::Password);
    auto *layoutBox = new QComboBox;
    layoutBox->addItem(QStringLiteral("Deutsch"), QStringLiteral("de"));
    layoutBox->addItem(QStringLiteral("Deutsch, ohne Akzenttasten"), QStringLiteral("de:nodeadkeys"));
    layoutBox->addItem(QStringLiteral("Englisch (US)"), QStringLiteral("us"));
    layoutBox->addItem(QStringLiteral("Modhex"), QStringLiteral("modhex"));
    auto *layoutHint = new QLabel;
    layoutHint->setWordWrap(true);
    auto *staticPage = new QWidget;
    auto *staticLayout = new QVBoxLayout(staticPage);
    staticLayout->setContentsMargins(0, 0, 0, 0);
    auto *staticNote = new QLabel(QStringLiteral(
        "Das Passwort geht an die Konsole von ykman und steht nicht in der Befehlszeile. "
        "Höchstens 38 Zeichen. Das Tastaturlayout ist das der Rechner, an denen das Passwort ankommen soll."));
    staticNote->setWordWrap(true);
    auto *staticForm = new QFormLayout;
    staticForm->addRow(QStringLiteral("Passwort"), password);
    staticForm->addRow(QStringLiteral("Wiederholen"), repeat);
    staticForm->addRow(QStringLiteral("Tastatur"), layoutBox);
    staticLayout->addWidget(staticNote);
    staticLayout->addLayout(staticForm);
    staticLayout->addWidget(layoutHint);

    auto *hotpKey = new QLineEdit;
    hotpKey->setEchoMode(QLineEdit::Password);
    auto *digits = new QComboBox;
    digits->addItem(QStringLiteral("6"), 6);
    digits->addItem(QStringLiteral("8"), 8);
    auto *hotpPage = new QWidget;
    auto *hotpLayout = new QVBoxLayout(hotpPage);
    hotpLayout->setContentsMargins(0, 0, 0, 0);
    auto *hotpNote = new QLabel(QStringLiteral(
        "Der Schlüssel ist Hex und geht an die Konsole von ykman, nicht in die Befehlszeile."));
    hotpNote->setWordWrap(true);
    auto *hotpForm = new QFormLayout;
    hotpForm->addRow(QStringLiteral("Schlüssel"), hotpKey);
    hotpForm->addRow(QStringLiteral("Stellen"), digits);
    hotpLayout->addWidget(hotpNote);
    hotpLayout->addLayout(hotpForm);

    auto *pages = new QStackedWidget;
    pages->addWidget(yubico);
    pages->addWidget(challenge);
    pages->addWidget(staticPage);
    pages->addWidget(hotpPage);
    layout->addWidget(pages);

    auto *problem = new QLabel;
    problem->setWordWrap(true);
    layout->addWidget(problem);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Programmieren"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Abbrechen"));
    layout->addWidget(buttons);

    const auto updateHint = [layoutBox, layoutHint]() {
        if (layoutBox->currentData().toString() == QLatin1String("modhex"))
            layoutHint->setText(QStringLiteral("Modhex erlaubt nur cbdefghijklnrtuv."));
        else
            layoutHint->clear();
    };
    updateHint();
    QObject::connect(layoutBox, &QComboBox::currentIndexChanged, &dialog, updateHint);
    QObject::connect(type, &QComboBox::currentIndexChanged, pages, &QStackedWidget::setCurrentIndex);
    QObject::connect(type, &QComboBox::currentIndexChanged, &dialog, [problem]() { problem->clear(); });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (overwrite && serialEdit->text().trimmed() != serial) {
            problem->setText(QStringLiteral("Die Seriennummer stimmt nicht."));
            return;
        }
        const auto action = OtpAction(type->currentData().toInt());
        if (action == OtpAction::StaticPassword) {
            const QString value = password->text();
            if (value.isEmpty()) {
                problem->setText(QStringLiteral("Das Passwort fehlt."));
                return;
            }
            if (value != repeat->text()) {
                problem->setText(QStringLiteral("Die beiden Passwörter stimmen nicht überein."));
                return;
            }
            if (value.size() > 38 || value.contains(QLatin1Char('\n')) || value.contains(QLatin1Char('\r'))) {
                problem->setText(QStringLiteral("Das Passwort ist zu lang oder enthält einen Zeilenumbruch."));
                return;
            }
            if (layoutBox->currentData().toString() == QLatin1String("modhex")) {
                const QString allowed = QStringLiteral("cbdefghijklnrtuv");
                for (const QChar character : value) {
                    if (!allowed.contains(character)) {
                        problem->setText(QStringLiteral("Modhex erlaubt nur cbdefghijklnrtuv."));
                        return;
                    }
                }
            }
        }
        if (action == OtpAction::Hotp) {
            const QString key = hotpKey->text().trimmed();
            const QRegularExpression hex(QStringLiteral("^[0-9a-fA-F]{2,128}$"));
            if (!hex.match(key).hasMatch() || key.size() % 2 != 0) {
                problem->setText(QStringLiteral("Der Schlüssel braucht eine gerade Anzahl Hex-Zeichen."));
                return;
            }
        }
        dialog.accept();
    });

    OtpRequest request;
    if (dialog.exec() == QDialog::Accepted) {
        request.accepted = true;
        request.action = OtpAction(type->currentData().toInt());
        request.slot = slot;
        request.touch = touch->isChecked();
        request.digits = digits->currentData().toInt();
        request.layout = layoutBox->currentData().toString();
        if (request.action == OtpAction::StaticPassword)
            request.secret = password->text();
        else if (request.action == OtpAction::Hotp)
            request.secret = hotpKey->text().trimmed();
    }
    wipeEdit(password);
    wipeEdit(repeat);
    wipeEdit(hotpKey);
    wipeEdit(serialEdit);
    return request;
}

struct OathDraft
{
    bool ok = false;
    QString error;
    QString name;
    QString issuer;
    QString secret;
    QString type;
    QString algorithm = QStringLiteral("sha1");
    int digits = 6;
    int period = 30;
    int counter = 0;

    QString display() const
    {
        return issuer.isEmpty() ? name : issuer + QLatin1Char(':') + name;
    }
};

QString percentDecode(const QString &text)
{
    return QString::fromUtf8(QByteArray::fromPercentEncoding(text.toUtf8()));
}

OathDraft parseOathUri(const QString &uri)
{
    OathDraft draft;
    const QRegularExpression re(QStringLiteral("^otpauth://(totp|hotp)/([^?#]+)\\?([^#]*)$"),
                                QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = re.match(uri.trimmed());
    if (!match.hasMatch()) {
        draft.error = QStringLiteral("Die Adresse muss mit otpauth://totp/ oder otpauth://hotp/ beginnen.");
        return draft;
    }
    draft.type = match.captured(1).toLower();
    QString label = percentDecode(match.captured(2));
    if (label.startsWith(QLatin1Char('/')))
        label.remove(0, 1);
    QString queryIssuer;
    const QStringList parts = match.captured(3).split(QLatin1Char('&'));
    for (const QString &part : parts) {
        const int eq = part.indexOf(QLatin1Char('='));
        const QString key = (eq < 0 ? part : part.left(eq)).toLower();
        const QString value = percentDecode(eq < 0 ? QString() : part.mid(eq + 1));
        if (key == QLatin1String("secret"))
            draft.secret = value;
        else if (key == QLatin1String("issuer"))
            queryIssuer = value;
        else if (key == QLatin1String("algorithm"))
            draft.algorithm = value.toUpper();
        else if (key == QLatin1String("digits"))
            draft.digits = value.toInt();
        else if (key == QLatin1String("period"))
            draft.period = value.toInt();
        else if (key == QLatin1String("counter"))
            draft.counter = value.toInt();
    }
    if (queryIssuer.isEmpty() && label.contains(QLatin1Char(':'))) {
        const int colon = label.indexOf(QLatin1Char(':'));
        draft.issuer = label.left(colon);
        draft.name = label.mid(colon + 1);
    } else {
        draft.issuer = queryIssuer;
        draft.name = label;
    }
    draft.secret.remove(QLatin1Char(' '));
    draft.secret.remove(QLatin1Char('='));
    if (draft.digits == 0)
        draft.digits = 6;
    if (draft.period == 0)
        draft.period = 30;
    if (draft.algorithm.isEmpty())
        draft.algorithm = QStringLiteral("SHA1");
    const QRegularExpression base32(QStringLiteral("^[A-Za-z2-7]+$"));
    if (draft.name.trimmed().isEmpty())
        draft.error = QStringLiteral("Der Kontoname fehlt.");
    else if (!base32.match(draft.secret).hasMatch())
        draft.error = QStringLiteral("Das Geheimnis in der Adresse ist kein Base32.");
    else if (draft.digits < 6 || draft.digits > 8)
        draft.error = QStringLiteral("Die Stellenzahl muss 6, 7 oder 8 sein.");
    else if (draft.algorithm != QLatin1String("SHA1") && draft.algorithm != QLatin1String("SHA256")
             && draft.algorithm != QLatin1String("SHA512"))
        draft.error = QStringLiteral("Der Hash muss SHA1, SHA256 oder SHA512 sein.");
    else if (draft.type == QLatin1String("totp") && draft.period < 1)
        draft.error = QStringLiteral("Die Periode fehlt.");
    if (!draft.error.isEmpty())
        return draft;
    draft.name = draft.name.trimmed();
    draft.issuer = draft.issuer.trimmed();
    draft.algorithm = draft.algorithm.toLower();
    draft.ok = true;
    return draft;
}

bool oathNameTaken(const QList<OathAccount> &accounts, const QString &name)
{
    for (const OathAccount &account : accounts) {
        if (account.name.compare(name, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

bool oathQueryUnique(const QList<OathAccount> &accounts, const QString &query)
{
    int matches = 0;
    const QString needle = query.toLower();
    for (const OathAccount &account : accounts) {
        if (account.name.toLower().contains(needle))
            ++matches;
    }
    return matches == 1;
}

void wipeOath(OathRequest &request)
{
    wipeSecret(request.secret);
    wipeSecret(request.password);
    wipeSecret(request.newPassword);
}

QString oathFailureText(const QString &detail)
{
    const QString text = detail.toLower();
    if (text.contains(QLatin1String("wrong password")))
        return QStringLiteral("Das OATH-Passwort stimmt nicht.");
    if (text.contains(QLatin1String("multiple credentials")))
        return QStringLiteral("Der Name trifft mehrere Konten.");
    if (text.contains(QLatin1String("no credential")))
        return QStringLiteral("Das Konto wurde nicht gefunden.");
    if (text.contains(QLatin1String("base32")))
        return QStringLiteral("Das Geheimnis ist kein gültiges Base32.");
    if (text.contains(QLatin1String("nicht rechtzeitig")))
        return QStringLiteral("Die Zeit ist abgelaufen. Falls der Key blinkt, erneut holen und berühren.");
    if (detail.trimmed().isEmpty())
        return QStringLiteral("Die OATH-Aktion ist fehlgeschlagen.");
    return detail.trimmed();
}

OathRequest askOathAdd(QWidget *parent, const QList<OathAccount> &accounts, const QString &serial)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("OATH-Konto anlegen"));
    dialog.setMinimumWidth(480);
    auto *layout = new QVBoxLayout(&dialog);
    auto *info = new QLabel(QStringLiteral(
        "Die otpauth-Adresse enthält das Geheimnis. Es geht an die Konsole von ykman "
        "und steht nicht in der Befehlszeile. Es wird nicht protokolliert."));
    info->setWordWrap(true);
    layout->addWidget(info);
    auto *uri = new QLineEdit;
    uri->setPlaceholderText(QStringLiteral("otpauth://totp/…"));
    layout->addWidget(uri);
    auto *touch = new QCheckBox(QStringLiteral("Code erst nach einer Berührung"));
    layout->addWidget(touch);
    auto *serialEdit = new QLineEdit;
    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Seriennummer"), serialEdit);
    serialEdit->hide();
    if (QWidget *label = form->labelForField(serialEdit))
        label->hide();
    layout->addLayout(form);
    auto *problem = new QLabel;
    problem->setWordWrap(true);
    layout->addWidget(problem);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Anlegen"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Abbrechen"));
    layout->addWidget(buttons);
    OathDraft draft;
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        draft = parseOathUri(uri->text());
        if (!draft.ok) {
            problem->setText(draft.error);
            wipeSecret(draft.secret);
            return;
        }
        if (oathNameTaken(accounts, draft.display())) {
            serialEdit->show();
            if (QWidget *label = form->labelForField(serialEdit))
                label->show();
            if (serialEdit->text().trimmed() != serial) {
                problem->setText(QStringLiteral(
                    "Dieses Konto gibt es schon. Zum Ersetzen die Seriennummer eintippen."));
                return;
            }
        }
        dialog.accept();
    });

    OathRequest request;
    if (dialog.exec() == QDialog::Accepted && draft.ok) {
        request.action = OathAction::Add;
        request.query = draft.name;
        request.issuer = draft.issuer;
        request.secret = draft.secret;
        request.oathType = draft.type;
        request.algorithm = draft.algorithm;
        request.digits = draft.digits;
        request.period = draft.period;
        request.counter = draft.counter;
        request.touch = touch->isChecked();
    }
    wipeSecret(draft.secret);
    wipeEdit(uri);
    wipeEdit(serialEdit);
    return request;
}

QString askOathRename(QWidget *parent, const QString &current)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Konto umbenennen"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *info = new QLabel(QStringLiteral("Neuer Name. Mit einem Doppelpunkt wird daraus Aussteller:Name."));
    info->setWordWrap(true);
    layout->addWidget(info);
    auto *edit = new QLineEdit(current);
    layout->addWidget(edit);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Umbenennen"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Abbrechen"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (edit->text().trimmed().isEmpty() || edit->text().trimmed() == current)
            return;
        dialog.accept();
    });
    const QString value = dialog.exec() == QDialog::Accepted ? edit->text().trimmed() : QString();
    return value;
}

struct OathPasswordChoice
{
    bool accepted = false;
    bool clear = false;
    QString current;
    QString next;
};

OathPasswordChoice askOathPassword(QWidget *parent, bool unlock, bool protectedNow, const QString &serial)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(unlock ? QStringLiteral("OATH entsperren") : QStringLiteral("OATH-Passwort"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *info = new QLabel(unlock
        ? QStringLiteral("Das Passwort geht an die Konsole von ykman. YubiGUI speichert es nicht auf dem Rechner.")
        : QStringLiteral("Das Passwort geht an die Konsole von ykman und steht nicht in der Befehlszeile. "
                         "Zum Ändern oder Entfernen die Seriennummer eintippen."));
    info->setWordWrap(true);
    layout->addWidget(info);
    auto *current = new QLineEdit;
    auto *next = new QLineEdit;
    auto *repeat = new QLineEdit;
    for (QLineEdit *edit : {current, next, repeat})
        edit->setEchoMode(QLineEdit::Password);
    auto *clear = new QCheckBox(QStringLiteral("Passwort entfernen"));
    auto *serialEdit = new QLineEdit;
    auto *form = new QFormLayout;
    if (protectedNow)
        form->addRow(QStringLiteral("Bisheriges Passwort"), current);
    if (!unlock) {
        form->addRow(QStringLiteral("Neues Passwort"), next);
        form->addRow(QStringLiteral("Wiederholen"), repeat);
        form->addRow(QStringLiteral("Seriennummer"), serialEdit);
    }
    layout->addLayout(form);
    if (!unlock && protectedNow)
        layout->addWidget(clear);
    auto *problem = new QLabel;
    problem->setWordWrap(true);
    layout->addWidget(problem);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(unlock ? QStringLiteral("Entsperren")
                                                          : QStringLiteral("Setzen"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Abbrechen"));
    layout->addWidget(buttons);
    const auto applyClear = [&](bool removing) {
        next->setEnabled(!removing);
        repeat->setEnabled(!removing);
    };
    QObject::connect(clear, &QCheckBox::toggled, &dialog, applyClear);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (protectedNow && current->text().isEmpty()) {
            problem->setText(QStringLiteral("Das bisherige Passwort fehlt."));
            return;
        }
        if (!unlock && serialEdit->text().trimmed() != serial) {
            problem->setText(QStringLiteral("Die Seriennummer stimmt nicht."));
            return;
        }
        if (!unlock && !clear->isChecked()) {
            if (next->text().isEmpty()) {
                problem->setText(QStringLiteral("Das neue Passwort fehlt."));
                return;
            }
            if (next->text() != repeat->text()) {
                problem->setText(QStringLiteral("Die beiden Passwörter stimmen nicht überein."));
                return;
            }
            if (next->text().contains(QLatin1Char('\n')) || next->text().contains(QLatin1Char('\r'))) {
                problem->setText(QStringLiteral("Das Passwort enthält einen Zeilenumbruch."));
                return;
            }
        }
        dialog.accept();
    });

    OathPasswordChoice choice;
    if (dialog.exec() == QDialog::Accepted) {
        choice.accepted = true;
        choice.clear = clear->isChecked();
        choice.current = current->text();
        choice.next = next->text();
    }
    wipeEdit(current);
    wipeEdit(next);
    wipeEdit(repeat);
    wipeEdit(serialEdit);
    return choice;
}

void prepareTable(QTableWidget *table)
{
    table->horizontalHeader()->setStretchLastSection(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setHighlightSections(false);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(32);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setFocusPolicy(Qt::NoFocus);
    table->setShowGrid(false);
    table->setAlternatingRowColors(true);
    table->setFrameShape(QFrame::NoFrame);
}

QLabel *captionLabel(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("caption"));
    return label;
}

QTableWidgetItem *cell(const QString &text)
{
    auto *item = new QTableWidgetItem(text);
    item->setFlags(Qt::ItemIsEnabled);
    return item;
}

bool askYkmanPath(QWidget *parent)
{
    const QString previous = Settings::ykmanOverride();
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Pfad zu ykman"));
    dialog.setMinimumWidth(560);

    auto *layout = new QVBoxLayout(&dialog);
    auto *info = new QLabel(Settings::searchHint());
    info->setObjectName(QStringLiteral("note"));
    info->setWordWrap(true);
    layout->addWidget(info);

    auto *edit = new QLineEdit(previous);
    edit->setPlaceholderText(QStringLiteral("Leer lassen für die automatische Suche"));
    edit->setClearButtonEnabled(true);
    auto *browse = new QPushButton(QStringLiteral("Durchsuchen"));
    auto *row = new QHBoxLayout;
    row->setSpacing(8);
    row->addWidget(edit, 1);
    row->addWidget(browse);
    layout->addLayout(row);

    const QString resolved = Settings::resolveYkman();
    auto *current = new QLabel(resolved.isEmpty()
                                   ? QStringLiteral("Aktuell ist kein ykman gefunden.")
                                   : QStringLiteral("Aktuell: %1").arg(resolved));
    current->setObjectName(QStringLiteral("caption"));
    current->setWordWrap(true);
    current->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(current);

    auto *problem = new QLabel;
    problem->setWordWrap(true);
    layout->addWidget(problem);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("Speichern"));
    buttons->button(QDialogButtonBox::Save)->setObjectName(QStringLiteral("primary"));
    buttons->button(QDialogButtonBox::Save)->setDefault(true);
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Abbrechen"));
    auto *reset = buttons->addButton(QStringLiteral("Zurücksetzen"), QDialogButtonBox::ResetRole);
    reset->setEnabled(!previous.isEmpty());
    layout->addWidget(buttons);

    QObject::connect(browse, &QPushButton::clicked, &dialog, [&]() {
        QString start = edit->text().trimmed();
        if (start.isEmpty())
            start = resolved;
#ifdef Q_OS_WIN
        const QString filter = QStringLiteral("ykman (ykman.exe)");
#else
        const QString filter = QStringLiteral("Alle Dateien (*)");
#endif
        const QString file = QFileDialog::getOpenFileName(
            &dialog, QStringLiteral("ykman auswählen"), start, filter);
        if (!file.isEmpty())
            edit->setText(file);
    });

    bool changed = false;
    const auto store = [&](const QString &path) {
        const QString cleaned = path.trimmed();
        if (!cleaned.isEmpty() && !previous.isEmpty()
            && QFileInfo(cleaned).absoluteFilePath() == QFileInfo(previous).absoluteFilePath()) {
            dialog.accept();
            return;
        }
        buttons->setEnabled(false);
        browse->setEnabled(false);
        problem->setText(cleaned.isEmpty() ? QString() : QStringLiteral("ykman wird geprüft…"));
        QGuiApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        QString error;
        const bool ok = Settings::storeYkman(path, &error);
        buttons->setEnabled(true);
        browse->setEnabled(true);
        if (!ok) {
            problem->setText(error);
            return;
        }
        changed = Settings::ykmanOverride() != previous;
        dialog.accept();
    };

    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() { store(edit->text()); });
    QObject::connect(reset, &QPushButton::clicked, &dialog, [&]() { store(QString()); });

    dialog.exec();
    return changed;
}

void linuxNeeds(const YkmanSnapshot &snap, bool *ykman, bool *pcscd)
{
    const bool generation = QRegularExpression(QStringLiteral("\\bykman\\s+6\\.")).match(snap.version).hasMatch();
    *ykman = snap.program.isEmpty()
             || snap.error.contains(QStringLiteral("Fassung 5"))
             || snap.error.contains(QStringLiteral("No such option"), Qt::CaseInsensitive)
             || (!snap.version.isEmpty() && !generation);
    *pcscd = !Settings::pcscdRunning();
}

void showInstallDialog(QWidget *parent, const QString &commands, bool ykman, bool pcscd)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Fehlende Programme"));
    dialog.setMinimumSize(680, 380);

    auto *layout = new QVBoxLayout(&dialog);
    QString intro = QStringLiteral(
        "Die Befehle unten kopieren und im Terminal ausführen. YubiGUI startet sie nicht selbst.");
    if (ykman) {
        intro += QStringLiteral(
            " ykman 6 liegt nicht als passendes Ubuntu-Paket vor. "
            "Das Paket yubikey-manager ist die Fassung 5 und passt nicht.");
    }
    if (pcscd) {
        intro += QStringLiteral(
            " PIV, OATH und OpenPGP brauchen den Smartcard-Dienst pcscd.");
    }
    auto *info = new QLabel(intro);
    info->setObjectName(QStringLiteral("note"));
    info->setWordWrap(true);
    layout->addWidget(info);

    auto *text = new QPlainTextEdit(commands);
    text->setReadOnly(true);
    text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    text->setMinimumHeight(160);
    layout->addWidget(text, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("Schließen"));
    auto *copy = buttons->addButton(QStringLiteral("Kopieren"), QDialogButtonBox::ActionRole);
    copy->setObjectName(QStringLiteral("primary"));
    layout->addWidget(buttons);

    QObject::connect(copy, &QPushButton::clicked, text, [text, copy]() {
        QGuiApplication::clipboard()->setText(text->toPlainText());
        copy->setText(QStringLiteral("Kopiert"));
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}

void showNote(QLabel *note, const QString &text)
{
    const QString trimmed = text.trimmed();
    note->setText(trimmed);
    note->setVisible(!trimmed.isEmpty());
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_client(new YkmanClient(this))
{
    setWindowTitle(QStringLiteral("YubiGUI"));
    resize(1020, 700);
    setMinimumSize(880, 560);

    m_pages = new QListWidget;
    m_pages->setObjectName(QStringLiteral("pages"));
    m_pages->addItem(QStringLiteral("Übersicht"));
    m_pages->addItem(QStringLiteral("Schnittstellen"));
    m_pages->addItem(QStringLiteral("OTP"));
    m_pages->addItem(QStringLiteral("FIDO"));
    m_pages->addItem(QStringLiteral("OATH"));
    m_pages->addItem(QStringLiteral("PIV"));
    m_pages->addItem(QStringLiteral("OpenPGP"));
    m_pages->setFrameShape(QFrame::NoFrame);
    m_pages->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_pages->setSpacing(2);
    m_pages->setCurrentRow(0);

    m_keys = new QComboBox;
    m_keys->setMinimumWidth(280);
    m_keys->hide();

    m_type = new QLabel(dash());
    m_serial = new QLabel(dash());
    m_firmware = new QLabel(dash());
    m_form = new QLabel(dash());
    m_interfaces = new QLabel(dash());
    m_nfc = new QLabel(dash());
    m_connection = new QLabel(dash());
    m_locked = new QLabel;
    m_type->setObjectName(QStringLiteral("deviceTitle"));
    m_type->setWordWrap(true);
    m_locked->setObjectName(QStringLiteral("warning"));
    m_locked->setWordWrap(true);
    m_locked->hide();
    for (QLabel *label : {m_serial, m_firmware, m_form, m_interfaces, m_nfc, m_connection})
        label->setObjectName(QStringLiteral("value"));
    for (QLabel *label : {m_type, m_serial, m_firmware, m_form, m_interfaces, m_nfc, m_connection})
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_interfaces->setWordWrap(true);

    const auto section = [](const QString &text) {
        auto *label = new QLabel(text);
        label->setObjectName(QStringLiteral("section"));
        return label;
    };
    const auto cardOf = [](QLayout *layout) {
        layout->setContentsMargins(16, 12, 16, 12);
        auto *card = new QWidget;
        card->setObjectName(QStringLiteral("card"));
        card->setAttribute(Qt::WA_StyledBackground, true);
        card->setLayout(layout);
        return card;
    };
    const auto pageOf = [](QLayout *layout) {
        layout->setContentsMargins(20, 16, 20, 16);
        layout->setSpacing(12);
        auto *page = new QWidget;
        page->setLayout(layout);
        return page;
    };
    const auto fact = [](const QString &caption, QLabel *value) {
        auto *box = new QWidget;
        auto *lay = new QVBoxLayout(box);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(1);
        lay->addWidget(captionLabel(caption));
        lay->addWidget(value);
        return box;
    };

    m_apps = new QTableWidget(0, 2);
    prepareTable(m_apps);
    m_apps->setItemDelegate(new SwitchMarkDelegate(m_apps));
    setApplicationColumns(m_apps, false);

    auto *overview = new QVBoxLayout;
    overview->addWidget(section(QStringLiteral("Anwendungen")));
    overview->addWidget(m_apps, 1);
    auto *overviewPage = pageOf(overview);

    m_slots = new QTableWidget(0, 3);
    m_slots->setHorizontalHeaderLabels(
        {QStringLiteral("Slot"), QStringLiteral("Auslösung"), QStringLiteral("Zustand")});
    m_slots->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_slots->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    prepareTable(m_slots);

    m_otpNote = new QLabel;
    m_otpNote->setObjectName(QStringLiteral("note"));
    m_otpNote->setWordWrap(true);
    m_otpNote->hide();

    m_otpSlot = new QComboBox;
    m_otpSlot->addItem(QStringLiteral("Slot 1, kurze Berührung"), 1);
    m_otpSlot->addItem(QStringLiteral("Slot 2, lange Berührung"), 2);
    m_programOtp = new QPushButton(QStringLiteral("Programmieren"));
    m_deleteOtp = new QPushButton(QStringLiteral("Löschen"));
    m_deleteOtp->setObjectName(QStringLiteral("danger"));
    m_swapOtp = new QPushButton(QStringLiteral("Tauschen"));
    auto *otpActions = new QHBoxLayout;
    otpActions->setSpacing(8);
    otpActions->addWidget(m_otpSlot);
    otpActions->addWidget(m_programOtp);
    otpActions->addWidget(m_deleteOtp);
    otpActions->addWidget(m_swapOtp);
    otpActions->addStretch(1);

    auto *otpLayout = new QVBoxLayout;
    otpLayout->addWidget(m_otpNote);
    otpLayout->addWidget(m_slots, 1);
    otpLayout->addLayout(otpActions);
    auto *otpPage = pageOf(otpLayout);

    m_fidoNote = new QLabel;
    m_fidoNote->setObjectName(QStringLiteral("note"));
    m_fidoNote->setWordWrap(true);
    m_fidoNote->hide();
    m_fidoAaguid = new QLabel(dash());
    m_fidoPin = new QLabel(dash());
    m_fidoPinLength = new QLabel(dash());
    m_fidoUv = new QLabel(dash());
    m_fidoStorage = new QLabel(dash());
    m_fidoFingerprints = new QLabel(dash());
    for (QLabel *label : {m_fidoAaguid, m_fidoPin, m_fidoPinLength, m_fidoUv, m_fidoStorage, m_fidoFingerprints})
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_fidoForm = new QFormLayout;
    m_fidoForm->setHorizontalSpacing(28);
    m_fidoForm->setVerticalSpacing(8);
    m_fidoForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    m_fidoForm->addRow(QStringLiteral("AAGUID"), m_fidoAaguid);
    m_fidoForm->addRow(QStringLiteral("PIN"), m_fidoPin);
    m_fidoForm->addRow(QStringLiteral("Mindestlänge"), m_fidoPinLength);
    m_fidoForm->addRow(QStringLiteral("Immer Nutzerprüfung"), m_fidoUv);
    m_fidoForm->addRow(QStringLiteral("Freier Speicher"), m_fidoStorage);
    m_fidoForm->addRow(QStringLiteral("Fingerabdrücke"), m_fidoFingerprints);
    m_changeFidoPin = new QPushButton(QStringLiteral("PIN ändern"));
    m_changeFidoPin->setEnabled(false);
    m_fidoMinLength = new QPushButton(QStringLiteral("Mindestlänge"));
    m_fidoMinLength->setEnabled(false);
    m_fidoMinLength->setToolTip(QStringLiteral(
        "Nur erhöhen. Kürzer geht nur über einen FIDO-Reset, und der löscht die Anmeldedaten."));
    m_fidoForcePin = new QPushButton(QStringLiteral("PIN erzwingen"));
    m_fidoForcePin->setEnabled(false);
    m_fidoForcePin->setToolTip(QStringLiteral(
        "Der PIN gilt danach als gesperrt, bis er geändert wird."));
    m_fidoAlwaysUv = new QPushButton(QStringLiteral("Prüfung umschalten"));
    m_fidoAlwaysUv->setEnabled(false);
    m_fidoAlwaysUv->hide();
    m_resetFido = new QPushButton(QStringLiteral("FIDO zurücksetzen"));
    m_resetFido->setObjectName(QStringLiteral("danger"));
    m_resetFido->setEnabled(false);
    m_fidoCredentials = new QTableWidget(0, 4);
    m_fidoCredentials->setHorizontalHeaderLabels(
        {QStringLiteral("Dienst"), QStringLiteral("Benutzer"), QStringLiteral("Anzeigename"),
         QStringLiteral("Kennung")});
    prepareTable(m_fidoCredentials);
    m_fidoCredentials->setSelectionMode(QAbstractItemView::SingleSelection);
    m_fidoCredentials->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fidoCredentials->setFocusPolicy(Qt::StrongFocus);
    m_fidoCredentials->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_fidoCredentials->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_fidoCredentials->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_fidoCredentials->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_fidoList = new QPushButton(QStringLiteral("Anmeldedaten"));
    m_fidoList->setEnabled(false);
    m_fidoDelete = new QPushButton(QStringLiteral("Anmeldedaten löschen"));
    m_fidoDelete->setObjectName(QStringLiteral("danger"));
    m_fidoDelete->setEnabled(false);
    m_sshGenerate = new QPushButton(QStringLiteral("SSH-Schlüssel"));
    m_sshGenerate->setEnabled(false);
    m_sshDownload = new QPushButton(QStringLiteral("Stubs holen"));
    m_sshDownload->setEnabled(false);
    auto *fidoButtons = new QHBoxLayout;
    fidoButtons->setSpacing(8);
    fidoButtons->addWidget(m_changeFidoPin);
    fidoButtons->addWidget(m_fidoMinLength);
    fidoButtons->addWidget(m_fidoForcePin);
    fidoButtons->addWidget(m_fidoAlwaysUv);
    fidoButtons->addStretch(1);
    m_fidoPrints = new QTableWidget(0, 2);
    m_fidoPrints->setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Kennung")});
    prepareTable(m_fidoPrints);
    m_fidoPrints->setSelectionMode(QAbstractItemView::SingleSelection);
    m_fidoPrints->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fidoPrints->setFocusPolicy(Qt::StrongFocus);
    m_fidoPrints->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_fidoPrints->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_fidoPrintList = new QPushButton(QStringLiteral("Liste"));
    m_fidoPrintList->setEnabled(false);
    m_fidoPrintAdd = new QPushButton(QStringLiteral("Hinzufügen"));
    m_fidoPrintAdd->setEnabled(false);
    m_fidoPrintRename = new QPushButton(QStringLiteral("Umbenennen"));
    m_fidoPrintRename->setEnabled(false);
    m_fidoPrintDelete = new QPushButton(QStringLiteral("Löschen"));
    m_fidoPrintDelete->setObjectName(QStringLiteral("danger"));
    m_fidoPrintDelete->setEnabled(false);
    auto *printButtons = new QHBoxLayout;
    printButtons->setSpacing(8);
    printButtons->addWidget(m_fidoPrintList);
    printButtons->addWidget(m_fidoPrintAdd);
    printButtons->addWidget(m_fidoPrintRename);
    printButtons->addWidget(m_fidoPrintDelete);
    printButtons->addStretch(1);
    m_fidoPrintWrap = new QWidget;
    auto *printLayout = new QVBoxLayout(m_fidoPrintWrap);
    printLayout->setContentsMargins(0, 0, 0, 0);
    printLayout->setSpacing(8);
    printLayout->addWidget(section(QStringLiteral("Gespeicherte Fingerabdrücke")));
    printLayout->addWidget(m_fidoPrints, 1);
    printLayout->addLayout(printButtons);
    m_fidoPrintWrap->hide();
    auto *fidoUse = new QHBoxLayout;
    fidoUse->setSpacing(8);
    fidoUse->addWidget(m_fidoList);
    fidoUse->addWidget(m_sshGenerate);
    fidoUse->addWidget(m_sshDownload);
    fidoUse->addStretch(1);
    auto *fidoDanger = new QHBoxLayout;
    fidoDanger->setSpacing(8);
    fidoDanger->addWidget(m_fidoDelete);
    fidoDanger->addWidget(m_resetFido);
    fidoDanger->addStretch(1);
    auto *fidoLayout = new QVBoxLayout;
    fidoLayout->addWidget(m_fidoNote);
    fidoLayout->addWidget(cardOf(m_fidoForm));
    fidoLayout->addLayout(fidoButtons);
    fidoLayout->addWidget(m_fidoPrintWrap, 1);
    fidoLayout->addWidget(section(QStringLiteral("Anmeldedaten")));
    fidoLayout->addLayout(fidoUse);
    fidoLayout->addWidget(m_fidoCredentials, 1);
    fidoLayout->addLayout(fidoDanger);
    auto *fidoPage = pageOf(fidoLayout);

    m_checkLayout = new QGridLayout;
    m_checkLayout->setColumnStretch(0, 1);
    m_checkLayout->setHorizontalSpacing(24);
    m_checkLayout->setVerticalSpacing(8);
    m_apply = new QPushButton(QStringLiteral("Übernehmen"));
    m_apply->setObjectName(QStringLiteral("primary"));
    m_apply->setEnabled(false);
    auto *interfaceLayout = new QVBoxLayout;
    interfaceLayout->addWidget(cardOf(m_checkLayout));
    interfaceLayout->addWidget(m_apply, 0, Qt::AlignLeft);
    interfaceLayout->addStretch(1);
    auto *interfacePage = pageOf(interfaceLayout);

    m_oathNote = new QLabel;
    m_oathNote->setObjectName(QStringLiteral("note"));
    m_oathNote->setWordWrap(true);
    m_oathNote->hide();
    m_oathAccounts = new QTableWidget(0, 4);
    m_oathAccounts->setHorizontalHeaderLabels(
        {QStringLiteral("Konto"), QStringLiteral("Art"), QStringLiteral("Periode"), QStringLiteral("Code")});
    m_oathAccounts->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_oathAccounts->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_oathAccounts->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    prepareTable(m_oathAccounts);
    m_oathAccounts->setSelectionMode(QAbstractItemView::SingleSelection);
    m_oathAccounts->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_oathAccounts->setFocusPolicy(Qt::StrongFocus);
    m_oathCodes = new QPushButton(QStringLiteral("Codes"));
    m_oathOne = new QPushButton(QStringLiteral("Diesen Code"));
    m_oathCopy = new QPushButton(QStringLiteral("Kopieren"));
    m_oathAdd = new QPushButton(QStringLiteral("Anlegen"));
    m_oathRename = new QPushButton(QStringLiteral("Umbenennen"));
    m_oathDelete = new QPushButton(QStringLiteral("Löschen"));
    m_oathDelete->setObjectName(QStringLiteral("danger"));
    m_oathPassword = new QPushButton(QStringLiteral("Passwort"));
    m_oathCodeTimer = new QTimer(this);
    m_oathCodeTimer->setSingleShot(true);
    auto *oathRow = new QHBoxLayout;
    oathRow->setSpacing(8);
    oathRow->addWidget(m_oathCodes);
    oathRow->addWidget(m_oathOne);
    oathRow->addWidget(m_oathCopy);
    oathRow->addStretch(1);
    auto *oathManage = new QHBoxLayout;
    oathManage->setSpacing(8);
    oathManage->addWidget(m_oathAdd);
    oathManage->addWidget(m_oathRename);
    oathManage->addWidget(m_oathDelete);
    oathManage->addWidget(m_oathPassword);
    oathManage->addStretch(1);
    auto *oathLayout = new QVBoxLayout;
    oathLayout->addWidget(m_oathNote);
    oathLayout->addWidget(m_oathAccounts, 1);
    oathLayout->addLayout(oathRow);
    oathLayout->addLayout(oathManage);
    auto *oathPage = pageOf(oathLayout);

    m_pivNote = new QLabel;
    m_pivNote->setObjectName(QStringLiteral("note"));
    m_pivNote->setWordWrap(true);
    m_pivNote->hide();
    m_pivVersion = new QLabel(dash());
    m_pivPin = new QLabel(dash());
    m_pivPuk = new QLabel(dash());
    m_pivManagement = new QLabel(dash());
    m_pivBio = new QLabel(dash());
    for (QLabel *label : {m_pivVersion, m_pivPin, m_pivPuk, m_pivManagement, m_pivBio})
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_pivForm = new QFormLayout;
    m_pivForm->setHorizontalSpacing(28);
    m_pivForm->setVerticalSpacing(8);
    m_pivForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    m_pivForm->addRow(QStringLiteral("Version"), m_pivVersion);
    m_pivForm->addRow(QStringLiteral("PIN"), m_pivPin);
    m_pivForm->addRow(QStringLiteral("PUK"), m_pivPuk);
    m_pivForm->addRow(QStringLiteral("Management-Key"), m_pivManagement);
    m_pivForm->addRow(QStringLiteral("Biometrie"), m_pivBio);
    m_pivSlots = new QTableWidget(0, 3);
    m_pivSlots->setHorizontalHeaderLabels(
        {QStringLiteral("Slot"), QStringLiteral("Subject"), QStringLiteral("Ablauf")});
    prepareTable(m_pivSlots);
    m_pivSlots->setSelectionMode(QAbstractItemView::SingleSelection);
    m_pivSlots->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pivSlots->setFocusPolicy(Qt::StrongFocus);
    m_pivSlots->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_pivSlots->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_pivSlots->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_changePivPin = new QPushButton(QStringLiteral("PIN ändern"));
    m_changePivPuk = new QPushButton(QStringLiteral("PUK ändern"));
    m_unblockPiv = new QPushButton(QStringLiteral("PIN entsperren"));
    m_pivGenerate = new QPushButton(QStringLiteral("Schlüssel erzeugen"));
    m_pivImportKey = new QPushButton(QStringLiteral("Schlüssel importieren"));
    m_pivExport = new QPushButton(QStringLiteral("Öffentlichen Schlüssel"));
    m_pivExportCert = new QPushButton(QStringLiteral("Zertifikat exportieren"));
    m_pivRequest = new QPushButton(QStringLiteral("Zertifikatsanfrage"));
    m_pivImport = new QPushButton(QStringLiteral("Zertifikat importieren"));
    m_pivSign = new QPushButton(QStringLiteral("Selbst signieren"));
    m_pivDeleteCert = new QPushButton(QStringLiteral("Zertifikat löschen"));
    m_pivDeleteCert->setObjectName(QStringLiteral("danger"));
    m_pivManagementButton = new QPushButton(QStringLiteral("Management-Key"));
    m_pivReset = new QPushButton(QStringLiteral("PIV zurücksetzen"));
    m_pivReset->setObjectName(QStringLiteral("danger"));
    for (QPushButton *button : {m_changePivPin, m_changePivPuk, m_unblockPiv, m_pivGenerate, m_pivImportKey,
                                m_pivExport, m_pivExportCert, m_pivRequest, m_pivImport, m_pivSign,
                                m_pivDeleteCert, m_pivManagementButton, m_pivReset})
        button->setEnabled(false);
    auto *pivButtons = new QHBoxLayout;
    pivButtons->setSpacing(8);
    pivButtons->addWidget(m_changePivPin);
    pivButtons->addWidget(m_changePivPuk);
    pivButtons->addWidget(m_unblockPiv);
    pivButtons->addStretch(1);
    auto *pivWrite = new QHBoxLayout;
    pivWrite->setSpacing(8);
    pivWrite->addWidget(m_pivGenerate);
    pivWrite->addWidget(m_pivImportKey);
    pivWrite->addWidget(m_pivExport);
    pivWrite->addStretch(1);
    auto *pivCerts = new QHBoxLayout;
    pivCerts->setSpacing(8);
    pivCerts->addWidget(m_pivExportCert);
    pivCerts->addWidget(m_pivRequest);
    pivCerts->addWidget(m_pivImport);
    pivCerts->addWidget(m_pivSign);
    pivCerts->addWidget(m_pivDeleteCert);
    pivCerts->addStretch(1);
    auto *pivDanger = new QHBoxLayout;
    pivDanger->setSpacing(8);
    pivDanger->addWidget(m_pivManagementButton);
    pivDanger->addWidget(m_pivReset);
    pivDanger->addStretch(1);
    auto *pivLayout = new QVBoxLayout;
    pivLayout->addWidget(m_pivNote);
    pivLayout->addWidget(cardOf(m_pivForm));
    pivLayout->addWidget(section(QStringLiteral("Zertifikate")));
    pivLayout->addWidget(m_pivSlots, 1);
    pivLayout->addLayout(pivButtons);
    pivLayout->addLayout(pivWrite);
    pivLayout->addLayout(pivCerts);
    pivLayout->addLayout(pivDanger);
    auto *pivPage = pageOf(pivLayout);

    m_openPgpNote = new QLabel;
    m_openPgpNote->setObjectName(QStringLiteral("note"));
    m_openPgpNote->setWordWrap(true);
    m_openPgpNote->hide();
    m_openPgpVersion = new QLabel(dash());
    m_openPgpApplication = new QLabel(dash());
    m_openPgpPinState = new QLabel(dash());
    m_openPgpResetState = new QLabel(dash());
    m_openPgpAdminState = new QLabel(dash());
    m_openPgpSignature = new QLabel(dash());
    m_openPgpKdf = new QLabel(dash());
    for (QLabel *label : {m_openPgpVersion, m_openPgpApplication, m_openPgpPinState, m_openPgpResetState,
                          m_openPgpAdminState, m_openPgpSignature, m_openPgpKdf})
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_openPgpForm = new QFormLayout;
    m_openPgpForm->setHorizontalSpacing(28);
    m_openPgpForm->setVerticalSpacing(8);
    m_openPgpForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    m_openPgpForm->addRow(QStringLiteral("OpenPGP"), m_openPgpVersion);
    m_openPgpForm->addRow(QStringLiteral("Anwendung"), m_openPgpApplication);
    m_openPgpForm->addRow(QStringLiteral("Benutzer-PIN"), m_openPgpPinState);
    m_openPgpForm->addRow(QStringLiteral("Reset-Code"), m_openPgpResetState);
    m_openPgpForm->addRow(QStringLiteral("Admin-PIN"), m_openPgpAdminState);
    m_openPgpForm->addRow(QStringLiteral("Signatur-PIN"), m_openPgpSignature);
    m_openPgpForm->addRow(QStringLiteral("KDF"), m_openPgpKdf);
    m_openPgpKeys = new QTableWidget(0, 3);
    m_openPgpKeys->setHorizontalHeaderLabels(
        {QStringLiteral("Schlüssel"), QStringLiteral("Fingerabdruck"), QStringLiteral("Berührung")});
    prepareTable(m_openPgpKeys);
    m_openPgpKeys->setSelectionMode(QAbstractItemView::SingleSelection);
    m_openPgpKeys->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_openPgpKeys->setFocusPolicy(Qt::StrongFocus);
    m_openPgpKeys->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_openPgpKeys->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_openPgpKeys->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_changeOpenPgpPin = new QPushButton(QStringLiteral("Benutzer-PIN"));
    m_changeOpenPgpAdmin = new QPushButton(QStringLiteral("Admin-PIN"));
    m_setOpenPgpReset = new QPushButton(QStringLiteral("Reset-Code"));
    m_setOpenPgpSignature = new QPushButton(QStringLiteral("Signatur-PIN"));
    m_setOpenPgpTouch = new QPushButton(QStringLiteral("Berührung"));
    m_resetOpenPgp = new QPushButton(QStringLiteral("OpenPGP zurücksetzen"));
    m_resetOpenPgp->setObjectName(QStringLiteral("danger"));
    for (QPushButton *button : {m_changeOpenPgpPin, m_changeOpenPgpAdmin, m_setOpenPgpReset,
                                m_setOpenPgpSignature, m_setOpenPgpTouch, m_resetOpenPgp})
        button->setEnabled(false);
    auto *openPgpButtons = new QHBoxLayout;
    openPgpButtons->setSpacing(8);
    openPgpButtons->addWidget(m_changeOpenPgpPin);
    openPgpButtons->addWidget(m_changeOpenPgpAdmin);
    openPgpButtons->addWidget(m_setOpenPgpReset);
    openPgpButtons->addWidget(m_setOpenPgpSignature);
    openPgpButtons->addWidget(m_setOpenPgpTouch);
    openPgpButtons->addStretch(1);
    auto *openPgpDanger = new QHBoxLayout;
    openPgpDanger->setSpacing(8);
    openPgpDanger->addWidget(m_resetOpenPgp);
    openPgpDanger->addStretch(1);
    auto *openPgpLayout = new QVBoxLayout;
    openPgpLayout->addWidget(m_openPgpNote);
    openPgpLayout->addWidget(cardOf(m_openPgpForm));
    openPgpLayout->addWidget(section(QStringLiteral("Schlüssel")));
    openPgpLayout->addWidget(m_openPgpKeys, 1);
    openPgpLayout->addLayout(openPgpButtons);
    openPgpLayout->addLayout(openPgpDanger);
    auto *openPgpPage = pageOf(openPgpLayout);

    auto *stack = new QStackedWidget;
    stack->addWidget(overviewPage);
    stack->addWidget(interfacePage);
    stack->addWidget(otpPage);
    stack->addWidget(fidoPage);
    stack->addWidget(oathPage);
    stack->addWidget(pivPage);
    stack->addWidget(openPgpPage);

    m_refresh = new QPushButton(QStringLiteral("Aktualisieren"));
    m_refresh->setObjectName(QStringLiteral("primary"));
    m_ykmanPath = new QPushButton(QStringLiteral("ykman"));
    m_ykmanPath->setToolTip(QStringLiteral("Pfad zu ykman festlegen"));
    m_install = new QPushButton(QStringLiteral("Pakete"));
    m_install->setToolTip(QStringLiteral("Befehle für die fehlenden Programme kopieren"));
    m_install->hide();
    m_help = new QPushButton(QStringLiteral("?"));
    m_help->setObjectName(QStringLiteral("help"));
    m_help->setToolTip(QStringLiteral("Hilfe zur Übersicht"));
    m_help->setShortcut(QKeySequence::HelpContents);
    m_help->setCursor(Qt::PointingHandCursor);
    m_bridgeLabel = new QLabel;
    m_bridgeLabel->setObjectName(QStringLiteral("bridge"));
    statusBar()->addPermanentWidget(m_bridgeLabel);

    auto *facts = new QGridLayout;
    facts->setContentsMargins(0, 0, 0, 0);
    facts->setHorizontalSpacing(28);
    facts->setVerticalSpacing(10);
    facts->addWidget(fact(QStringLiteral("Seriennummer"), m_serial), 0, 0);
    facts->addWidget(fact(QStringLiteral("Firmware"), m_firmware), 0, 1);
    facts->addWidget(fact(QStringLiteral("Formfaktor"), m_form), 0, 2);
    facts->addWidget(fact(QStringLiteral("USB-Schnittstellen"), m_interfaces), 1, 0);
    facts->addWidget(fact(QStringLiteral("NFC"), m_nfc), 1, 1);
    facts->addWidget(fact(QStringLiteral("Verbindung"), m_connection), 1, 2);
    facts->setColumnStretch(2, 1);

    auto *titleRow = new QHBoxLayout;
    titleRow->setSpacing(16);
    titleRow->addWidget(m_type, 1);
    titleRow->addWidget(m_install, 0, Qt::AlignTop);
    titleRow->addWidget(m_ykmanPath, 0, Qt::AlignTop);
    titleRow->addWidget(m_refresh, 0, Qt::AlignTop);
    titleRow->addWidget(m_help, 0, Qt::AlignTop);

    auto *header = new QWidget;
    header->setObjectName(QStringLiteral("header"));
    header->setAttribute(Qt::WA_StyledBackground, true);
    auto *headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(22, 16, 22, 14);
    headerLayout->setSpacing(12);
    headerLayout->addLayout(titleRow);
    headerLayout->addLayout(facts);
    headerLayout->addWidget(m_locked);
    headerLayout->addWidget(m_keys, 0, Qt::AlignLeft);

    auto *nav = new QWidget;
    nav->setObjectName(QStringLiteral("nav"));
    nav->setAttribute(Qt::WA_StyledBackground, true);
    nav->setFixedWidth(188);
    auto *brand = new QLabel(QStringLiteral("YubiGUI"));
    brand->setObjectName(QStringLiteral("brand"));
    auto *navLayout = new QVBoxLayout(nav);
    navLayout->setContentsMargins(0, 0, 0, 0);
    navLayout->setSpacing(0);
    navLayout->addWidget(brand);
    navLayout->addWidget(m_pages, 1);

    auto *right = new QVBoxLayout;
    right->setContentsMargins(0, 0, 0, 0);
    right->setSpacing(0);
    right->addWidget(header);
    right->addWidget(stack, 1);

    auto *body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    body->addWidget(nav);
    body->addLayout(right, 1);

    auto *central = new QWidget;
    central->setObjectName(QStringLiteral("central"));
    central->setAttribute(Qt::WA_StyledBackground, true);
    central->setLayout(body);
    setCentralWidget(central);

    connect(m_pages, &QListWidget::currentRowChanged, stack, &QStackedWidget::setCurrentIndex);
    connect(m_pages, &QListWidget::currentRowChanged, this, [this](int row) {
        if (QListWidgetItem *item = m_pages->item(row))
            m_help->setToolTip(QStringLiteral("Hilfe zu %1").arg(item->text()));
    });
    connect(m_help, &QPushButton::clicked, this, &MainWindow::showHelp);
    connect(m_refresh, &QPushButton::clicked, this, &MainWindow::refresh);
    connect(m_ykmanPath, &QPushButton::clicked, this, &MainWindow::chooseYkmanPath);
    connect(m_install, &QPushButton::clicked, this, [this]() {
        bool ykman = false;
        bool pcscd = false;
        linuxNeeds(m_client->snapshot(), &ykman, &pcscd);
        const QString commands = Settings::linuxInstallText(ykman, pcscd);
        if (!commands.isEmpty())
            showInstallDialog(this, commands, ykman, pcscd);
    });
    connect(m_apply, &QPushButton::clicked, this, [this]() { applyInterfaces(QString()); });
    connect(m_changeFidoPin, &QPushButton::clicked, this, [this]() { changeFidoPinClicked(false); });
    connect(m_fidoMinLength, &QPushButton::clicked, this, [this]() { setFidoMinLengthClicked(false); });
    connect(m_fidoForcePin, &QPushButton::clicked, this, [this]() { forceFidoPinClicked(false); });
    connect(m_fidoAlwaysUv, &QPushButton::clicked, this, [this]() { toggleFidoUvClicked(false); });
    connect(m_resetFido, &QPushButton::clicked, this, [this]() { resetFidoClicked(false); });
    connect(m_fidoList, &QPushButton::clicked, this, [this]() { listFidoCredentialsClicked(false); });
    connect(m_sshGenerate, &QPushButton::clicked, this, &MainWindow::generateSshClicked);
    connect(m_sshDownload, &QPushButton::clicked, this, &MainWindow::downloadSshClicked);
    connect(m_client, &YkmanClient::sshFinished, this, &MainWindow::finishSsh);
    connect(m_fidoDelete, &QPushButton::clicked, this, [this]() { deleteFidoCredentialClicked(false); });
    connect(m_fidoCredentials, &QTableWidget::itemSelectionChanged, this, [this]() {
        applyFidoCredentials(m_client->busy());
    });
    connect(m_fidoPrints, &QTableWidget::itemSelectionChanged, this, [this]() {
        applyFidoConfig(m_client->busy());
    });
    connect(m_fidoPrintList, &QPushButton::clicked, this, [this]() { listFidoPrintsClicked(false); });
    connect(m_fidoPrintAdd, &QPushButton::clicked, this, [this]() { addFidoPrintClicked(false); });
    connect(m_fidoPrintRename, &QPushButton::clicked, this, [this]() { renameFidoPrintClicked(false); });
    connect(m_fidoPrintDelete, &QPushButton::clicked, this, [this]() { deleteFidoPrintClicked(false); });
    connect(m_client, &YkmanClient::fidoConfigFinished, this, &MainWindow::finishFidoConfig);
    connect(m_client, &YkmanClient::fidoCredentialsFinished, this, &MainWindow::finishFidoCredentials);
    connect(m_client, &YkmanClient::fidoPinFinished, this, &MainWindow::finishFidoPin);
    connect(m_client, &YkmanClient::fidoResetFinished, this, &MainWindow::finishFidoReset);
    connect(m_otpSlot, &QComboBox::currentIndexChanged, this, [this](int) {
        applyOtpActions(m_client->busy());
    });
    connect(m_programOtp, &QPushButton::clicked, this, &MainWindow::programOtpClicked);
    connect(m_deleteOtp, &QPushButton::clicked, this, &MainWindow::deleteOtpClicked);
    connect(m_swapOtp, &QPushButton::clicked, this, &MainWindow::swapOtpClicked);
    connect(m_client, &YkmanClient::otpFinished, this, &MainWindow::finishOtp);
    connect(m_oathAccounts, &QTableWidget::itemSelectionChanged, this, [this]() {
        applyOathActions(m_client->busy());
    });
    connect(m_oathCodes, &QPushButton::clicked, this, &MainWindow::oathCodesClicked);
    connect(m_oathOne, &QPushButton::clicked, this, &MainWindow::oathOneClicked);
    connect(m_oathCopy, &QPushButton::clicked, this, &MainWindow::oathCopyClicked);
    connect(m_oathAdd, &QPushButton::clicked, this, &MainWindow::oathAddClicked);
    connect(m_oathRename, &QPushButton::clicked, this, &MainWindow::oathRenameClicked);
    connect(m_oathDelete, &QPushButton::clicked, this, &MainWindow::oathDeleteClicked);
    connect(m_oathPassword, &QPushButton::clicked, this, &MainWindow::oathPasswordClicked);
    connect(m_oathCodeTimer, &QTimer::timeout, this, &MainWindow::clearOathCodes);
    connect(m_client, &YkmanClient::oathFinished, this, &MainWindow::finishOath);
    connect(m_changePivPin, &QPushButton::clicked, this, [this]() {
        changePivClicked(int(PivAction::ChangePin));
    });
    connect(m_changePivPuk, &QPushButton::clicked, this, [this]() {
        changePivClicked(int(PivAction::ChangePuk));
    });
    connect(m_unblockPiv, &QPushButton::clicked, this, [this]() {
        changePivClicked(int(PivAction::UnblockPin));
    });
    connect(m_pivSlots, &QTableWidget::itemSelectionChanged, this, [this]() {
        applyPivActions(m_client->busy());
    });
    connect(m_pivGenerate, &QPushButton::clicked, this, &MainWindow::generatePivKeyClicked);
    connect(m_pivImportKey, &QPushButton::clicked, this, &MainWindow::importPivKeyClicked);
    connect(m_pivExport, &QPushButton::clicked, this, &MainWindow::exportPivPublicClicked);
    connect(m_pivExportCert, &QPushButton::clicked, this, &MainWindow::exportPivCertClicked);
    connect(m_pivRequest, &QPushButton::clicked, this, &MainWindow::requestPivCertClicked);
    connect(m_pivImport, &QPushButton::clicked, this, &MainWindow::importPivCertClicked);
    connect(m_pivSign, &QPushButton::clicked, this, &MainWindow::signPivCertClicked);
    connect(m_pivDeleteCert, &QPushButton::clicked, this, &MainWindow::deletePivCertClicked);
    connect(m_pivManagementButton, &QPushButton::clicked, this, &MainWindow::changePivManagementClicked);
    connect(m_pivReset, &QPushButton::clicked, this, &MainWindow::resetPivClicked);
    connect(m_client, &YkmanClient::pivFinished, this, &MainWindow::finishPiv);
    connect(m_changeOpenPgpPin, &QPushButton::clicked, this, &MainWindow::changeOpenPgpPinClicked);
    connect(m_changeOpenPgpAdmin, &QPushButton::clicked, this, &MainWindow::changeOpenPgpAdminClicked);
    connect(m_setOpenPgpReset, &QPushButton::clicked, this, &MainWindow::setOpenPgpResetClicked);
    connect(m_setOpenPgpSignature, &QPushButton::clicked, this, &MainWindow::setOpenPgpSignatureClicked);
    connect(m_setOpenPgpTouch, &QPushButton::clicked, this, &MainWindow::setOpenPgpTouchClicked);
    connect(m_resetOpenPgp, &QPushButton::clicked, this, &MainWindow::resetOpenPgpClicked);
    connect(m_client, &YkmanClient::openpgpFinished, this, &MainWindow::finishOpenPgp);
    connect(m_client, &YkmanClient::lockCodeNeeded, this, [this](const QString &detail) {
        m_awaitingReconnect = false;
        setBusy(false);
        statusBar()->showMessage(detail);
        QString code = askLockCode();
        if (code.isEmpty()) {
            revertInterfaces();
            return;
        }
        applyInterfaces(code);
        code.fill(QLatin1Char('0'));
        code.clear();
    });
    connect(m_keys, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0 || m_client->busy())
            return;
        const QString serial = m_keys->itemData(index).toString();
        if (serial != m_selectedSerial)
            wipeSecret(m_oathSession);
        m_selectedSerial = serial;
        refresh();
    });
    connect(m_client, &YkmanClient::refreshFinished, this, &MainWindow::showSnapshot);

    refresh();
}

void MainWindow::showHelp()
{
    const int row = m_pages->currentRow();
    const QString page = (row >= 0 && m_pages->item(row)) ? m_pages->item(row)->text()
                                                          : QStringLiteral("YubiGUI");
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Hilfe zu %1").arg(page));
    dialog.setWindowFlags(dialog.windowFlags() & ~Qt::WindowContextHelpButtonHint);
    dialog.setMinimumWidth(520);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(12);
    auto *info = new QLabel(pageHelp());
    info->setObjectName(QStringLiteral("note"));
    info->setWordWrap(true);
    info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    info->setMinimumWidth(480);
    layout->addWidget(info);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("Schließen"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.adjustSize();
    dialog.exec();
}

QString MainWindow::pageHelp() const
{
    const YkmanSnapshot snap = m_client->snapshot();
    const DeviceRecord *device = nullptr;
    for (const DeviceRecord &item : snap.devices) {
        if (!m_selectedSerial.isEmpty() && item.serial == m_selectedSerial) {
            device = &item;
            break;
        }
    }

    switch (m_pages->currentRow()) {
    case 0:
        return QStringLiteral(
            "Die Übersicht listet die Anwendungen auf dem Key. "
            "Ein Häkchen heißt eingeschaltet, ein Kreuz ausgeschaltet. "
            "Das gilt für USB und, wenn der Key NFC hat, auch für NFC. "
            "Liegt der Key auf einem Lesegerät, steht unter Verbindung „Lesegerät“. "
            "Der Inhalt einer Anwendung steht auf ihrer eigenen Seite.");
    case 1:
        if (device && !device->nfcCapable) {
            return QStringLiteral(
                "Dieser Key hat kein NFC. Die Schalter gelten für USB. "
                "Ändert sich eine USB-Schnittstelle, verbindet sich der Key kurz neu. "
                "Ein vorhandener Lock-Code wird abgefragt. Ein neuer wird nicht erzeugt.");
        }
        if (device && onReader(*device)) {
            return QStringLiteral(
                "Diese Schalter gelten für USB und für NFC. Der Key liegt auf dem Lesegerät, "
                "geschrieben wird über diese Verbindung. Ändert sich eine USB-Schnittstelle, "
                "gilt das beim nächsten Einstecken. Ein vorhandener Lock-Code wird abgefragt. "
                "Ein neuer wird nicht erzeugt.");
        }
        return QStringLiteral(
            "Diese Schalter gelten für USB und für NFC. Geschrieben wird über die bestehende Verbindung. "
            "Ändert sich eine USB-Schnittstelle, verbindet sich der Key kurz neu. "
            "Ein vorhandener Lock-Code wird abgefragt. Ein neuer wird nicht erzeugt.");
    case 2:
        return QStringLiteral(
            "Sichtbar ist nur, ob ein Slot belegt ist. Den Inhalt gibt der Key nicht heraus. "
            "Programmieren, Löschen und Tauschen fragen vorher. Ein belegter Slot und das Tauschen "
            "verlangen die Seriennummer.");
    case 3: {
        QString note = QStringLiteral(
            "Der PIN geht an die Konsole von ykman und steht nicht in der Befehlszeile. "
            "Falls Windows den Zugriff ablehnt, wird nur dieser Aufruf erhöht. "
            "Blinkt der Key, dann berühren. "
            "Auffindbare Anmeldedaten erscheinen erst nach dem PIN. "
            "Löschen gilt für einen Eintrag und verlangt die Seriennummer. "
            "Mindestlänge, erzwungene PIN-Änderung und die dauernde Nutzerprüfung gehen ebenfalls an die Konsole. "
            "Die Mindestlänge lässt sich nur erhöhen und verlangt die Seriennummer. "
            "Hat der Key einen Sensor, erscheinen die Fingerabdrücke erst nach dem PIN. "
            "Löschen gilt für einen Abdruck und verlangt die Seriennummer. "
            "Ein SSH-Schlüssel legt auf dem PC nur eine Stub-Datei und den öffentlichen Schlüssel an. "
            "Der private Schlüssel bleibt auf dem YubiKey. Stubs holen schreibt residente Schlüssel neu.");
        if (device && onReader(*device))
            note += QStringLiteral(
                " Liegt der Key auf dem Lesegerät, die Anmeldedaten per USB lesen. "
                "Die Freigabe vom Auflegen ist schnell verbraucht, danach lehnt der Key das Auslesen ab.");
        const FidoRecord &fido = snap.fido;
        const bool haveFido = fido.error.isEmpty() && (!fido.aaguid.isEmpty() || !fido.pin.isEmpty());
        if (fido.error.isEmpty() && fido.resetBlocked) {
            note += QStringLiteral(
                " Der FIDO-Reset ist gesperrt, solange PIV eingerichtet ist. "
                "Ein vollständiger Werksreset würde den ganzen Key leeren.");
        } else if (haveFido) {
            note += QStringLiteral(
                " Der Reset löscht alle FIDO2- und U2F-Anmeldedaten und verlangt die Seriennummer.");
        }
        return note;
    }
    case 4: {
        const OathRecord &oath = snap.oath;
        QString note = QStringLiteral(
            "Codes nur auf Wunsch und nur kurz. Sie landen nicht von selbst in der Zwischenablage. "
            "Das Passwort geht an die Konsole von ykman. Löschen verlangt die Seriennummer.");
        if (oath.error.isEmpty() && oath.passwordProtected && !oath.loaded) {
            note.prepend(QStringLiteral(
                "Die Konten sind mit einem Passwort geschützt. Es geht an die Konsole von ykman. "
                "YubiGUI speichert es nicht auf dem Rechner. "));
        }
        if (oath.error.isEmpty() && oath.loaded && oath.accounts.isEmpty())
            note.prepend(QStringLiteral("Keine OATH-Konten. "));
        if (oath.error.isEmpty() && oath.passwordProtected && oath.passwordRemembered)
            note += QStringLiteral(
                " ykman hat das Passwort auf diesem Rechner gespeichert. YubiGUI speichert keins.");
        return note;
    }
    case 5: {
        const PivRecord &piv = snap.piv;
        QString note = QStringLiteral(
            "PIN und PUK gehen an die Konsole von ykman und stehen nicht in der Befehlszeile. "
            "Ein Fehlversuch verbraucht einen Versuch. Ist die PIN gesperrt, entsperrt sie nur noch der PUK. "
            "Ist der PUK aufgebraucht, bleibt PIV gesperrt. "
            "Der private Schlüssel bleibt auf dem YubiKey. Export schreibt den öffentlichen Schlüssel "
            "als PEM oder OpenSSH und das Zertifikat als PEM oder DER. "
            "Eine Zertifikatsanfrage geht an eine CA. "
            "Ein vorhandener privater Schlüssel lässt sich in einen Slot legen; die Datei bleibt auf dem Rechner. "
            "Eine Stub-Datei legt PIV nicht an.");
        if (!piv.biometrics.isEmpty())
            note += QStringLiteral(
                " Die PIV-Biometrie zeigt ykman nur an. Anlegen und Löschen bietet dieser Befehl nicht.");
        if (piv.error.isEmpty()) {
            const bool known = piv.pinAttempts >= 0 || !piv.version.isEmpty();
            bool anyCert = false;
            for (const PivSlotInfo &slot : piv.entries)
                anyCert = anyCert || !slot.subject.isEmpty();
            if (known && !anyCert)
                note += QStringLiteral(" Keine Zertifikate in den Slots.");
            if (piv.resetBlocked)
                note += QStringLiteral(" Der PIV-Reset ist gesperrt, solange FIDO eingerichtet ist.");
        }
        return note;
    }
    case 6: {
        const OpenPgpRecord &record = snap.openpgp;
        QString note = QStringLiteral(
            "Benutzer-PIN, Admin-PIN und Reset-Code gehen an die Konsole von ykman und stehen nicht in der Befehlszeile. "
            "Ein Fehlversuch verbraucht einen Versuch. "
            "Eine feste Berührung lässt sich später nicht wieder lockern. "
            "Steht der Reset-Code auf 0, ist keiner gesetzt oder er ist gesperrt. "
            "Ist die Benutzer-PIN gesperrt, nimmt ykman den Admin-PIN dafür nur als Argument. "
            "YubiGUI schickt ihn deshalb nicht. "
            "Der Reset braucht keinen PIN. Er löscht die Schlüssel und Zertifikate der Karte "
            "und setzt die PINs auf die Werkseinstellung. Der Beglaubigungsschlüssel bleibt. "
            "Er verlangt die Seriennummer.");
        if (record.error.isEmpty() && record.pinAttempts >= 0 && record.keys.isEmpty())
            note += QStringLiteral(" Keine Schlüssel auf der Karte.");
        return note;
    }
    default:
        return QString();
    }
}

MainWindow::~MainWindow()
{
    wipeSecret(m_oathSession);
    wipeSecret(m_oathPendingPassword);
}

void MainWindow::offerLinuxInstall(const YkmanSnapshot &snap)
{
#ifndef Q_OS_LINUX
    Q_UNUSED(snap);
    m_install->hide();
#else
    bool ykman = false;
    bool pcscd = false;
    linuxNeeds(snap, &ykman, &pcscd);
    if (!ykman && !pcscd) {
        m_install->hide();
        return;
    }
    m_install->show();
    const QString key = (ykman ? QStringLiteral("y") : QString())
                        + (pcscd ? QStringLiteral("p") : QString());
    if (m_offeredInstall == key)
        return;
    m_offeredInstall = key;
    const QString commands = Settings::linuxInstallText(ykman, pcscd);
    QTimer::singleShot(0, this, [this, commands, ykman, pcscd]() {
        if (!commands.isEmpty())
            showInstallDialog(this, commands, ykman, pcscd);
    });
#endif
}

void MainWindow::refresh()
{
    if (m_client->busy())
        return;
    setBusy(true);
    statusBar()->clearMessage();
    m_client->refresh(m_selectedSerial);
}

void MainWindow::updateYkmanButton()
{
    const bool missing = m_client->snapshot().program.isEmpty();
    m_ykmanPath->setText(missing ? QStringLiteral("Pfad wählen") : QStringLiteral("ykman"));
    const QString name = missing ? QStringLiteral("primary") : QString();
    if (m_ykmanPath->objectName() == name)
        return;
    m_ykmanPath->setObjectName(name);
    m_ykmanPath->style()->unpolish(m_ykmanPath);
    m_ykmanPath->style()->polish(m_ykmanPath);
}

void MainWindow::chooseYkmanPath()
{
    if (m_client->busy())
        return;
    if (askYkmanPath(this))
        refresh();
}

void MainWindow::setBusy(bool busy)
{
    m_refresh->setEnabled(!busy);
    m_ykmanPath->setEnabled(!busy);
    m_keys->setEnabled(!busy);
    if (busy) {
        m_apply->setEnabled(false);
        m_changeFidoPin->setEnabled(false);
    }
    applyFidoReset(busy);
    applyFidoCredentials(busy);
    applyFidoConfig(busy);
    for (const InterfaceToggle &toggle : m_toggles) {
        if (toggle.box)
            toggle.box->setEnabled(!busy);
    }
    applyOtpActions(busy);
    applyOathActions(busy);
    applyPivActions(busy);
    applyOpenPgpActions(busy);
}

void MainWindow::showEmpty(const QString &message)
{
    m_type->setText(message);
    m_serial->setText(dash());
    m_firmware->setText(dash());
    m_form->setText(dash());
    m_interfaces->setText(dash());
    m_nfc->setText(dash());
    m_connection->setText(dash());
    m_locked->clear();
    m_locked->setVisible(false);
    setApplicationColumns(m_apps, false);
    m_apps->setRowCount(0);
    m_slots->setRowCount(0);
    m_otpReadable = false;
    m_slotFilled[1] = false;
    m_slotFilled[2] = false;
    applyOtpActions(false);
    clearInterfaces();
    wipeSecret(m_oathSession);
    wipeSecret(m_oathPendingPassword);
    m_oathRelist = false;
    showOath(OathRecord{});
    showPiv(PivRecord{});
    showOpenPgp(OpenPgpRecord{});
    showFido(FidoRecord{}, false);
    showNote(m_fidoNote, message);
}

void MainWindow::showDevice(const DeviceRecord &device)
{
    m_type->setText(device.name.isEmpty() ? dash() : device.name);
    m_serial->setText(device.serial.isEmpty() ? dash() : device.serial);
    m_firmware->setText(device.version.isEmpty() ? dash() : device.version);
    m_form->setText(device.formFactor.isEmpty() ? dash() : device.formFactor);
    m_interfaces->setText(device.usbInterfaces.isEmpty() ? dash()
                                                         : device.usbInterfaces.join(QStringLiteral(", ")));
    m_connection->setText(connectionLabel(device));
    m_nfc->setText(!device.nfcCapable ? QStringLiteral("Nicht vorhanden")
                                      : device.nfcTransportEnabled ? QStringLiteral("Ein")
                                                                    : QStringLiteral("Aus"));
    m_locked->setText(device.locked ? QStringLiteral("Die Konfiguration ist mit einem Lock-Code gesperrt.")
                                    : QString());
    m_locked->setVisible(device.locked);

    setApplicationColumns(m_apps, device.nfcCapable);

    m_apps->setRowCount(device.applications.size());
    for (int row = 0; row < device.applications.size(); ++row) {
        const UsbApp &app = device.applications.at(row);
        m_apps->setItem(row, 0, cell(app.name));
        m_apps->setItem(row, 1, stateCell(app.usb));
        if (device.nfcCapable)
            m_apps->setItem(row, 2, stateCell(app.nfc));
    }
}

void MainWindow::showOtp(const OtpRecord &otp)
{
    m_slots->setRowCount(0);
    m_slotFilled[1] = false;
    m_slotFilled[2] = false;
    if (!otp.error.isEmpty()) {
        m_otpReadable = false;
        showNote(m_otpNote, otp.error);
        applyOtpActions(m_client->busy());
        return;
    }
    showNote(m_otpNote, QString());

    m_slots->setRowCount(otp.otpSlots.size());
    for (int row = 0; row < otp.otpSlots.size(); ++row) {
        const OtpSlot &slot = otp.otpSlots.at(row);
        const QString trigger = slot.slot == 1 ? QStringLiteral("kurze Berührung")
                                               : QStringLiteral("lange Berührung");
        m_slots->setItem(row, 0, cell(QString::number(slot.slot)));
        m_slots->setItem(row, 1, cell(trigger));
        m_slots->setItem(row, 2, cell(slotState(slot)));
        if (slot.slot == 1 || slot.slot == 2)
            m_slotFilled[slot.slot] = slot.configured;
    }
    m_otpReadable = !otp.otpSlots.isEmpty();
    applyOtpActions(m_client->busy());
}

void MainWindow::applyOtpActions(bool busy)
{
    if (!m_otpSlot)
        return;
    const int slot = m_otpSlot->currentData().toInt();
    const bool ready = !busy && m_otpReadable && !m_selectedSerial.isEmpty();
    m_otpSlot->setEnabled(ready);
    m_programOtp->setEnabled(ready);
    m_swapOtp->setEnabled(ready);
    m_deleteOtp->setEnabled(ready && (slot == 1 || slot == 2) && m_slotFilled[slot]);
}

void MainWindow::programOtpClicked()
{
    if (m_client->busy() || m_selectedSerial.isEmpty())
        return;
    const int slot = m_otpSlot->currentData().toInt();
    if (slot < 1 || slot > 2)
        return;

    OtpRequest request = askOtpProgram(this, slot, m_slotFilled[slot], m_selectedSerial);
    if (!request.accepted) {
        wipeRequest(request);
        return;
    }

    m_pendingOtp = request;
    wipeRequest(request);
    setBusy(true);
    statusBar()->showMessage(QStringLiteral("Der Slot wird geschrieben."));
    m_client->runOtp(m_selectedSerial, m_pendingOtp);
}

void MainWindow::deleteOtpClicked()
{
    if (m_client->busy() || m_selectedSerial.isEmpty())
        return;
    const int slot = m_otpSlot->currentData().toInt();
    if ((slot != 1 && slot != 2) || !m_slotFilled[slot])
        return;
    if (!confirmSerial(this, m_selectedSerial, QStringLiteral("Slot leeren"),
                       QStringLiteral("Slot %1 wird geleert. Die Konfiguration ist danach weg "
                                      "und lässt sich nicht wiederherstellen. Zum Bestätigen die Seriennummer eintippen.")
                           .arg(slot),
                       QStringLiteral("Löschen")))
        return;

    m_pendingOtp = {};
    m_pendingOtp.action = OtpAction::Delete;
    m_pendingOtp.slot = slot;
    setBusy(true);
    statusBar()->showMessage(QStringLiteral("Der Slot wird geleert."));
    m_client->runOtp(m_selectedSerial, m_pendingOtp);
}

void MainWindow::swapOtpClicked()
{
    if (m_client->busy() || m_selectedSerial.isEmpty())
        return;
    if (!confirmSerial(this, m_selectedSerial, QStringLiteral("Slots tauschen"),
                       QStringLiteral("Die beiden Slots tauschen den Platz. Zum Bestätigen die Seriennummer eintippen."),
                       QStringLiteral("Tauschen")))
        return;

    m_pendingOtp = {};
    m_pendingOtp.action = OtpAction::Swap;
    setBusy(true);
    statusBar()->showMessage(QStringLiteral("Die Slots werden getauscht."));
    m_client->runOtp(m_selectedSerial, m_pendingOtp);
}

void MainWindow::finishOtp(bool ok, const QString &detail, const QString &revealed)
{
    const OtpAction action = m_pendingOtp.action;
    const int slot = m_pendingOtp.slot;
    if (!ok && m_pendingOtp.accessCode.isEmpty()
        && detail.contains(QLatin1String("access code"), Qt::CaseInsensitive)) {
        QString code = askAccessCode(this);
        if (code.isEmpty()) {
            wipeRequest(m_pendingOtp);
            setBusy(false);
            statusBar()->showMessage(QStringLiteral("Abgebrochen. Der Slot bleibt unverändert."));
            return;
        }
        m_pendingOtp.accessCode = code;
        wipeSecret(code);
        setBusy(true);
        statusBar()->showMessage(QStringLiteral("Der Slot wird geschrieben."));
        m_client->runOtp(m_selectedSerial, m_pendingOtp);
        return;
    }

    const bool hadSecret = !revealed.trimmed().isEmpty();
    QString shown = revealed;
    if (ok && hadSecret)
        showGeneratedSecret(this, shown);
    wipeSecret(shown);
    wipeRequest(m_pendingOtp);

    m_pendingStatus = ok ? otpSuccessText(action, slot) : otpFailureText(detail);
    if (ok && !hadSecret && (action == OtpAction::YubiOtp || action == OtpAction::ChalResp))
        m_pendingStatus += QStringLiteral(" ykman hat den erzeugten Schlüssel nicht mit ausgegeben.");
    refresh();
}

void MainWindow::showFido(const FidoRecord &fido, bool reader)
{
    const auto showPrints = [this](bool visible) {
        m_fidoFingerprints->setVisible(visible);
        if (QWidget *label = m_fidoForm->labelForField(m_fidoFingerprints))
            label->setVisible(visible);
    };

    if (!fido.error.isEmpty()) {
        m_fidoAaguid->setText(dash());
        m_fidoPin->setText(dash());
        m_fidoPinLength->setText(dash());
        m_fidoUv->setText(dash());
        m_fidoStorage->setText(dash());
        m_fidoFingerprints->setText(dash());
        showPrints(false);
        showNote(m_fidoNote, fido.error);
        m_changeFidoPin->setText(QStringLiteral("PIN ändern"));
        m_changeFidoPin->setEnabled(false);
        m_fidoResetReady = false;
        m_fidoListReady = false;
        m_fidoConfigReady = false;
        m_fidoMinReady = false;
        m_fidoForceReady = false;
        m_fidoUvReady = false;
        m_fidoPrintReady = false;
        if (m_fidoAlwaysUv)
            m_fidoAlwaysUv->hide();
        if (m_fidoPrintWrap)
            m_fidoPrintWrap->hide();
        clearFidoCredentials();
        clearFidoPrints();
        applyFidoReset(m_client->busy());
        applyFidoCredentials(m_client->busy());
        applyFidoConfig(m_client->busy());
        return;
    }

    m_fidoAaguid->setText(fido.aaguid.isEmpty() ? dash() : fido.aaguid);
    if (fido.forcePinChange) {
        if (fido.attemptsRemaining == 1)
            m_fidoPin->setText(QStringLiteral("Muss geändert werden, 1 Versuch übrig"));
        else if (fido.attemptsRemaining > 1)
            m_fidoPin->setText(QStringLiteral("Muss geändert werden, %1 Versuche übrig").arg(fido.attemptsRemaining));
        else
            m_fidoPin->setText(QStringLiteral("Muss vor der nächsten Nutzung geändert werden"));
    } else {
        m_fidoPin->setText(pinLabel(fido.pin));
    }
    m_fidoPinLength->setText(fido.minimumPinLength.isEmpty() ? dash() : fido.minimumPinLength);
    m_fidoUv->setText(onOff(fido.alwaysUv));
    m_fidoStorage->setText(fido.storageRemaining.isEmpty() ? dash() : fido.storageRemaining);
    m_fidoFingerprints->setText(fingerprintLabel(fido.fingerprints));
    showPrints(!fido.fingerprints.isEmpty());
    const bool haveFido = !fido.aaguid.isEmpty() || !fido.pin.isEmpty();
    QString note;
    if (fido.resetBlocked)
        note = QStringLiteral(
            "Der FIDO-Reset ist gesperrt, solange PIV eingerichtet ist. "
            "Ein vollständiger Werksreset würde den ganzen Key leeren.");
    if (reader) {
        if (!note.isEmpty())
            note += QLatin1Char(' ');
        note += QStringLiteral(
            "Anmeldedaten über das Lesegerät lehnt der Key ab, sobald die Freigabe vom Auflegen verbraucht ist. Dafür den Key per USB anstecken.");
    }
    if (fido.forcePinChange) {
        if (!note.isEmpty())
            note += QLatin1Char(' ');
        note += QStringLiteral(
            "Der PIN muss geändert werden, bevor Fingerabdrücke, Anmeldedaten oder die übrigen Einstellungen gehen.");
    }
    showNote(m_fidoNote, note);
    const bool canChange = fido.minPinLength > 0 && (!fido.pinSet || fido.attemptsRemaining > 0);
    m_changeFidoPin->setText(fido.pinSet ? QStringLiteral("PIN ändern") : QStringLiteral("PIN setzen"));
    m_changeFidoPin->setEnabled(canChange && !m_client->busy());
    const bool pinUsable = haveFido && fido.pinSet && fido.attemptsRemaining > 0 && !fido.forcePinChange;
    m_fidoResetReady = haveFido && !fido.resetBlocked;
    m_fidoListReady = pinUsable;
    m_fidoConfigReady = pinUsable;
    int lengthCeiling = 63;
    for (const DeviceRecord &device : m_client->snapshot().devices) {
        if (device.serial == m_selectedSerial
            && device.formFactor.contains(QLatin1String("Bio"), Qt::CaseInsensitive)
            && m_client->snapshot().piv.error.isEmpty()
            && !m_client->snapshot().piv.version.isEmpty())
            lengthCeiling = 8;
    }
    m_fidoMinReady = haveFido && !fido.forcePinChange && fido.minPinLength > 0
                     && fido.minPinLength < lengthCeiling
                     && (!fido.pinSet || fido.attemptsRemaining > 0);
    m_fidoForceReady = pinUsable;
    const bool uvKnown = fido.alwaysUv.compare(QLatin1String("on"), Qt::CaseInsensitive) == 0
                         || fido.alwaysUv.compare(QLatin1String("off"), Qt::CaseInsensitive) == 0;
    m_fidoUvReady = haveFido && uvKnown && !fido.forcePinChange && (!fido.pinSet || fido.attemptsRemaining > 0);
    m_fidoAlwaysUv->setVisible(uvKnown);
    m_fidoAlwaysUv->setToolTip(fido.alwaysUv.compare(QLatin1String("on"), Qt::CaseInsensitive) == 0
                                   ? QStringLiteral("Schaltet die dauernde Nutzerprüfung aus.")
                                   : QStringLiteral(
                                         "Verlangt danach bei jeder FIDO-Anmeldung den PIN oder einen Fingerabdruck."));
    m_fidoPrintReady = pinUsable && !fido.fingerprints.isEmpty();
    m_fidoPrintWrap->setVisible(!fido.fingerprints.isEmpty());
    if (fido.fingerprints.isEmpty())
        clearFidoPrints();
    if (!haveFido)
        clearFidoCredentials();
    applyFidoReset(m_client->busy());
    applyFidoCredentials(m_client->busy());
    applyFidoConfig(m_client->busy());
}

void MainWindow::showSnapshot()
{
    setBusy(false);
    const YkmanSnapshot snap = m_client->snapshot();
    updateYkmanButton();
    offerLinuxInstall(snap);
    m_bridgeLabel->setText(snap.program.isEmpty()
                               ? QString()
                               : (snap.version.isEmpty() ? snap.program : snap.version));

    if (snap.program.isEmpty()) {
        const QString message = Settings::missingYkmanText();
        showEmpty(message);
        statusBar()->showMessage(message);
        return;
    }

    const bool missing = snap.devices.isEmpty()
                         || snap.error.contains(QStringLiteral("not found"), Qt::CaseInsensitive);
    if (m_awaitingReconnect && missing) {
        m_awaitingReconnect = false;
        statusBar()->showMessage(QStringLiteral("Der Key verbindet sich neu."));
        QTimer::singleShot(1500, this, &MainWindow::refresh);
        return;
    }
    m_awaitingReconnect = false;

    if (!snap.error.isEmpty()) {
        const bool wrongYkman = snap.error.contains(QStringLiteral("Fassung 5"));
        showEmpty(wrongYkman ? snap.error : QStringLiteral("Der Key konnte nicht gelesen werden."));
        statusBar()->showMessage(snap.error);
        return;
    }

    m_keys->blockSignals(true);
    m_keys->clear();
    for (const DeviceRecord &device : snap.devices) {
        const QString label = device.serial.isEmpty()
                                  ? device.name
                                  : QStringLiteral("%1  ·  %2").arg(device.name, device.serial);
        m_keys->addItem(label, device.serial);
    }
    const int selected = m_keys->findData(m_selectedSerial);
    if (selected >= 0)
        m_keys->setCurrentIndex(selected);
    m_keys->setVisible(snap.devices.size() > 1);
    m_keys->blockSignals(false);

    if (snap.devices.isEmpty()) {
        m_selectedSerial.clear();
        showEmpty(QStringLiteral("Kein YubiKey. Einstecken oder auf das Lesegerät legen."));
        return;
    }

    const DeviceRecord *shown = &snap.devices.first();
    for (const DeviceRecord &device : snap.devices) {
        if (!m_selectedSerial.isEmpty() && device.serial == m_selectedSerial)
            shown = &device;
    }
    m_selectedSerial = shown->serial;
    showDevice(*shown);
    showInterfaces(*shown);
    showOtp(snap.otp);
    showFido(snap.fido, onReader(*shown));
    showOath(snap.oath);
    showPiv(snap.piv);
    showOpenPgp(snap.openpgp);
    if (m_oathRelist && snap.oath.passwordProtected && !m_oathSession.isEmpty() && snap.oath.error.isEmpty()) {
        m_oathRelist = false;
        OathRequest request;
        request.action = OathAction::List;
        request.password = m_oathSession;
        startOath(request);
    } else {
        m_oathRelist = false;
    }
    if (!m_pendingStatus.isEmpty()) {
        statusBar()->showMessage(m_pendingStatus);
        m_pendingStatus.clear();
    } else {
        statusBar()->showMessage(connectedMessage(*shown));
    }
}

void MainWindow::changeFidoPinClicked(bool elevated)
{
    if (m_client->busy())
        return;
    const FidoRecord fido = m_client->snapshot().fido;
    if (fido.minPinLength < 1 || (fido.pinSet && fido.attemptsRemaining < 1))
        return;

    PinChoice choice = askFidoPin(this, fido);
    if (!choice.accepted) {
        wipeSecret(choice.current);
        wipeSecret(choice.next);
        return;
    }

    setBusy(true);
    statusBar()->showMessage(QStringLiteral("Falls der Key blinkt, jetzt berühren."));
    m_client->changeFidoPin(m_selectedSerial, choice.current, choice.next, elevated);
    wipeSecret(choice.current);
    wipeSecret(choice.next);
}

void MainWindow::finishFidoPin(bool ok, const QString &detail)
{
    if (!ok && fidoAccessDenied(detail)) {
        setBusy(false);
        QMessageBox box(this);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(QStringLiteral("Erhöhte Sitzung"));
        box.setText(QStringLiteral(
            "Windows hat den FIDO-Zugriff abgelehnt. Nur dieser Aufruf wird erhöht neu gestartet. "
            "Den PIN bitte noch einmal eingeben. Er geht nicht auf die Befehlszeile."));
        auto *accept = box.addButton(QStringLiteral("Erhöhen"), QMessageBox::AcceptRole);
        box.addButton(QStringLiteral("Abbrechen"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() == accept)
            changeFidoPinClicked(true);
        else
            statusBar()->showMessage(QStringLiteral("Die erhöhte Sitzung wurde abgebrochen."));
        return;
    }

    m_pendingStatus = fidoPinText(ok, detail);
    refresh();
}

void MainWindow::applyFidoReset(bool busy)
{
    if (!m_resetFido)
        return;
    m_resetFido->setEnabled(!busy && m_fidoResetReady);
}

void MainWindow::resetFidoClicked(bool elevated)
{
    if (m_client->busy() || m_selectedSerial.isEmpty() || !m_fidoResetReady)
        return;
    bool reader = false;
    for (const DeviceRecord &device : m_client->snapshot().devices) {
        if (device.serial == m_selectedSerial)
            reader = onReader(device);
    }
    const QString reseat = reader
                               ? QStringLiteral("Danach den Key vom Lesegerät nehmen und gleich wieder auflegen.")
                               : QStringLiteral("Danach den Key abziehen und gleich wieder einstecken.");
    const QString during = reader ? QStringLiteral("nicht vom Lesegerät nehmen")
                                  : QStringLiteral("nicht abziehen");
    if (!confirmSerial(this, m_selectedSerial, QStringLiteral("FIDO zurücksetzen"),
                       QStringLiteral("Der Reset löscht alle FIDO2- und U2F-Anmeldedaten und setzt den PIN zurück. "
                                      "OTP, OATH, PIV und OpenPGP bleiben.\n\n"
                                      "%1 Sobald er blinkt, "
                                      "den Kontakt berühren. Bleibt das Licht an, gedrückt halten. Während der Reset "
                                      "läuft, den Key %2.\n\n"
                                      "Zum Bestätigen die Seriennummer eintippen.")
                           .arg(reseat, during),
                       QStringLiteral("Zurücksetzen")))
        return;

    setBusy(true);
    statusBar()->showMessage(reader
                                 ? QStringLiteral(
                                       "Key jetzt vom Lesegerät nehmen und wieder auflegen. Danach berühren. "
                                       "Bleibt das Licht an, den Kontakt halten.")
                                 : QStringLiteral(
                                       "Key jetzt abziehen und wieder einstecken. Danach berühren. "
                                       "Bleibt das Licht an, den Kontakt halten."));
    m_client->resetFido(m_selectedSerial, elevated);
}

void MainWindow::finishFidoReset(bool ok, const QString &detail)
{
    if (!ok && fidoAccessDenied(detail)) {
        setBusy(false);
        QMessageBox box(this);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(QStringLiteral("Erhöhte Sitzung"));
        box.setText(QStringLiteral(
            "Windows hat den FIDO-Zugriff abgelehnt. Nur dieser Aufruf wird erhöht neu gestartet. "
            "Die Seriennummer wird noch einmal verlangt. Danach den Key wieder abziehen oder auf das Lesegerät legen."));
        auto *accept = box.addButton(QStringLiteral("Erhöhen"), QMessageBox::AcceptRole);
        box.addButton(QStringLiteral("Abbrechen"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() == accept)
            resetFidoClicked(true);
        else
            statusBar()->showMessage(QStringLiteral("Die erhöhte Sitzung wurde abgebrochen."));
        return;
    }

    m_pendingStatus = fidoResetText(ok, detail);
    refresh();
}

void MainWindow::applyFidoCredentials(bool busy)
{
    if (!m_fidoList)
        return;
    m_fidoList->setEnabled(!busy && m_fidoListReady);
    const bool sshReady = !Settings::resolveSshKeygen().isEmpty();
    const QString sshTip = sshReady
                               ? QStringLiteral("Auf dem PC liegen danach nur die Stub-Datei und der öffentliche Schlüssel.")
                               : Settings::missingSshKeygenText();
    if (m_sshGenerate) {
        m_sshGenerate->setEnabled(!busy && m_fidoListReady && sshReady);
        m_sshGenerate->setToolTip(sshTip);
    }
    if (m_sshDownload) {
        m_sshDownload->setEnabled(!busy && m_fidoListReady && sshReady);
        m_sshDownload->setToolTip(sshReady
                                      ? QStringLiteral("Schreibt die Stub-Dateien residenter SSH-Schlüssel neu.")
                                      : sshTip);
    }
    bool selected = false;
    if (m_fidoCredentials && m_fidoCredentials->selectionModel())
        selected = !m_fidoCredentials->selectionModel()->selectedRows().isEmpty();
    m_fidoDelete->setEnabled(!busy && m_fidoListReady && m_fidoCredentialsLoaded && selected);
}

void MainWindow::applyFidoConfig(bool busy)
{
    if (!m_fidoMinLength)
        return;
    m_fidoMinLength->setEnabled(!busy && m_fidoMinReady);
    m_fidoForcePin->setEnabled(!busy && m_fidoForceReady);
    m_fidoAlwaysUv->setEnabled(!busy && m_fidoUvReady);
    m_fidoPrintList->setEnabled(!busy && m_fidoPrintReady);
    m_fidoPrintAdd->setEnabled(!busy && m_fidoPrintReady);
    bool selected = false;
    if (m_fidoPrints && m_fidoPrints->selectionModel())
        selected = !m_fidoPrints->selectionModel()->selectedRows().isEmpty();
    m_fidoPrintRename->setEnabled(!busy && m_fidoPrintReady && m_fidoPrintsLoaded && selected);
    m_fidoPrintDelete->setEnabled(!busy && m_fidoPrintReady && m_fidoPrintsLoaded && selected);
}

void MainWindow::clearFidoPrints()
{
    m_fidoPrintsLoaded = false;
    m_fidoConfigName.clear();
    m_fidoConfigId.clear();
    if (m_fidoPrints)
        m_fidoPrints->setRowCount(0);
}

QString MainWindow::selectedFidoPrint(QString *name) const
{
    if (!m_fidoPrints || !m_fidoPrints->selectionModel())
        return {};
    const QModelIndexList rows = m_fidoPrints->selectionModel()->selectedRows();
    if (rows.isEmpty())
        return {};
    const QTableWidgetItem *item = m_fidoPrints->item(rows.first().row(), 1);
    if (!item || !fidoTemplateId(item->text()))
        return {};
    if (name) {
        const QTableWidgetItem *label = m_fidoPrints->item(rows.first().row(), 0);
        *name = label ? label->text() : QString();
        if (*name == dash())
            name->clear();
    }
    return item->text();
}

void MainWindow::startFidoConfig(bool elevated)
{
    if (m_client->busy() || m_selectedSerial.isEmpty())
        return;
    const FidoRecord fido = m_client->snapshot().fido;
    QString title = QStringLiteral("FIDO");
    QString extra;
    QString accept = QStringLiteral("Weiter");
    QString status = QStringLiteral("Falls der Key blinkt, jetzt berühren.");
    switch (m_fidoConfigAction) {
    case FidoConfigAction::ToggleAlwaysUv:
        title = QStringLiteral("Nutzerprüfung");
        extra = QStringLiteral("Der PIN schaltet die Einstellung um.");
        accept = QStringLiteral("Umschalten");
        status = QStringLiteral("Die Nutzerprüfung wird umgeschaltet. Falls der Key blinkt, jetzt berühren.");
        break;
    case FidoConfigAction::SetMinLength:
        title = QStringLiteral("Mindestlänge");
        extra = QStringLiteral("Der PIN setzt die neue Mindestlänge.");
        accept = QStringLiteral("Setzen");
        status = QStringLiteral("Die Mindestlänge wird gesetzt. Falls der Key blinkt, jetzt berühren.");
        break;
    case FidoConfigAction::ForceChange:
        title = QStringLiteral("PIN erzwingen");
        extra = QStringLiteral("Der bisherige PIN bestätigt die Sperre.");
        accept = QStringLiteral("Erzwingen");
        status = QStringLiteral("Die PIN-Änderung wird erzwungen. Falls der Key blinkt, jetzt berühren.");
        break;
    case FidoConfigAction::FingerprintsList:
        title = QStringLiteral("Fingerabdrücke");
        extra = QStringLiteral("Die Liste wird erst danach gelesen.");
        accept = QStringLiteral("Anzeigen");
        status = QStringLiteral("Die Fingerabdrücke werden gelesen. Falls der Key blinkt, jetzt berühren.");
        break;
    case FidoConfigAction::FingerprintsAdd:
        title = QStringLiteral("Fingerabdruck hinzufügen");
        extra = QStringLiteral("Danach den Finger auf den Sensor legen, bis die Aufnahme fertig ist.");
        accept = QStringLiteral("Aufnehmen");
        status = QStringLiteral(
            "Den Finger auf den Sensor legen und liegen lassen. ykman braucht mehrere Aufnahmen.");
        break;
    case FidoConfigAction::FingerprintsRename:
        title = QStringLiteral("Fingerabdruck umbenennen");
        extra = QStringLiteral("Der PIN ändert den Namen.");
        accept = QStringLiteral("Umbenennen");
        status = QStringLiteral("Der Name wird geändert. Falls der Key blinkt, jetzt berühren.");
        break;
    case FidoConfigAction::FingerprintsDelete:
        title = QStringLiteral("Fingerabdruck löschen");
        extra = QStringLiteral("Der PIN gilt für diesen einen Abdruck.");
        accept = QStringLiteral("Löschen bestätigen");
        status = QStringLiteral("Der Fingerabdruck wird gelöscht. Falls der Key blinkt, jetzt berühren.");
        break;
    }

    QString pin;
    if (fido.pinSet) {
        pin = askFidoVerifyPin(this, fido, title, extra, accept);
        if (pin.isEmpty()) {
            m_fidoConfigName.clear();
            m_fidoConfigId.clear();
            return;
        }
    }
    FidoConfigRequest request;
    request.action = m_fidoConfigAction;
    request.pin = pin;
    request.name = m_fidoConfigName;
    request.templateId = m_fidoConfigId;
    request.length = m_fidoConfigLength;
    setBusy(true);
    statusBar()->showMessage(status);
    m_client->runFidoConfig(m_selectedSerial, request, elevated);
    wipeSecret(pin);
    wipeSecret(request.pin);
}

void MainWindow::setFidoMinLengthClicked(bool elevated)
{
    if (!m_fidoMinReady || m_client->busy() || m_selectedSerial.isEmpty())
        return;
    const FidoRecord fido = m_client->snapshot().fido;
    int ceiling = 63;
    for (const DeviceRecord &device : m_client->snapshot().devices) {
        if (device.serial == m_selectedSerial
            && device.formFactor.contains(QLatin1String("Bio"), Qt::CaseInsensitive)
            && m_client->snapshot().piv.error.isEmpty()
            && !m_client->snapshot().piv.version.isEmpty())
            ceiling = 8;
    }
    if (!elevated) {
        const int length = askFidoMinLength(this, fido.minPinLength, ceiling);
        if (length < fido.minPinLength + 1 || length > ceiling)
            return;
        if (!confirmSerial(this, m_selectedSerial, QStringLiteral("Mindestlänge"),
                           QStringLiteral("Die neue Mindestlänge ist %1. Kürzer geht danach nur über einen FIDO-Reset, "
                                          "und der löscht die Anmeldedaten.\n\n"
                                          "Zum Bestätigen die Seriennummer eintippen.")
                               .arg(length),
                           QStringLiteral("Setzen")))
            return;
        m_fidoConfigLength = length;
    }
    m_fidoConfigAction = FidoConfigAction::SetMinLength;
    m_fidoConfigName.clear();
    m_fidoConfigId.clear();
    startFidoConfig(elevated);
}

void MainWindow::forceFidoPinClicked(bool elevated)
{
    if (!m_fidoForceReady || m_client->busy() || m_selectedSerial.isEmpty())
        return;
    if (!elevated) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(QStringLiteral("PIN erzwingen"));
        box.setText(QStringLiteral(
            "Der PIN gilt danach als gesperrt, bis er geändert wird. "
            "Anmeldedaten, Fingerabdrücke und die übrigen FIDO-Einstellungen brauchen zuerst den neuen PIN."));
        auto *accept = box.addButton(QStringLiteral("Erzwingen"), QMessageBox::AcceptRole);
        box.addButton(QStringLiteral("Abbrechen"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() != accept)
            return;
    }
    m_fidoConfigAction = FidoConfigAction::ForceChange;
    m_fidoConfigName.clear();
    m_fidoConfigId.clear();
    m_fidoConfigLength = 0;
    startFidoConfig(elevated);
}

void MainWindow::toggleFidoUvClicked(bool elevated)
{
    if (!m_fidoUvReady || m_client->busy() || m_selectedSerial.isEmpty())
        return;
    const bool on = m_client->snapshot().fido.alwaysUv.compare(QLatin1String("on"), Qt::CaseInsensitive) == 0;
    if (!elevated) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(QStringLiteral("Nutzerprüfung"));
        box.setText(on ? QStringLiteral(
                        "Die Nutzerprüfung wird nicht mehr bei jeder FIDO-Anmeldung verlangt. "
                        "Ein Dienst kann sie weiter verlangen.")
                     : QStringLiteral(
                        "Jede FIDO-Anmeldung verlangt danach den PIN oder einen Fingerabdruck, "
                        "auch wenn der Dienst das nicht verlangt."));
        auto *accept = box.addButton(QStringLiteral("Umschalten"), QMessageBox::AcceptRole);
        box.addButton(QStringLiteral("Abbrechen"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() != accept)
            return;
    }
    m_fidoConfigAction = FidoConfigAction::ToggleAlwaysUv;
    m_fidoConfigName.clear();
    m_fidoConfigId.clear();
    m_fidoConfigLength = 0;
    startFidoConfig(elevated);
}

void MainWindow::listFidoPrintsClicked(bool elevated)
{
    if (!m_fidoPrintReady || m_client->busy() || m_selectedSerial.isEmpty())
        return;
    m_fidoConfigAction = FidoConfigAction::FingerprintsList;
    m_fidoConfigName.clear();
    m_fidoConfigId.clear();
    m_fidoConfigLength = 0;
    startFidoConfig(elevated);
}

void MainWindow::addFidoPrintClicked(bool elevated)
{
    if (!m_fidoPrintReady || m_client->busy() || m_selectedSerial.isEmpty())
        return;
    if (!elevated) {
        const QString name = askFidoPrintName(this, QStringLiteral("Fingerabdruck hinzufügen"), QString());
        if (name.isEmpty())
            return;
        m_fidoConfigName = name;
    }
    m_fidoConfigAction = FidoConfigAction::FingerprintsAdd;
    m_fidoConfigId.clear();
    m_fidoConfigLength = 0;
    startFidoConfig(elevated);
}

void MainWindow::renameFidoPrintClicked(bool elevated)
{
    if (!m_fidoPrintReady || !m_fidoPrintsLoaded || m_client->busy() || m_selectedSerial.isEmpty())
        return;
    if (!elevated) {
        QString current;
        const QString id = selectedFidoPrint(&current);
        if (!fidoTemplateId(id))
            return;
        const QString name = askFidoPrintName(this, QStringLiteral("Fingerabdruck umbenennen"), current);
        if (name.isEmpty())
            return;
        m_fidoConfigId = id;
        m_fidoConfigName = name;
    }
    m_fidoConfigAction = FidoConfigAction::FingerprintsRename;
    m_fidoConfigLength = 0;
    startFidoConfig(elevated);
}

void MainWindow::deleteFidoPrintClicked(bool elevated)
{
    if (!m_fidoPrintReady || !m_fidoPrintsLoaded || m_client->busy() || m_selectedSerial.isEmpty())
        return;
    if (!elevated) {
        QString name;
        const QString id = selectedFidoPrint(&name);
        if (!fidoTemplateId(id))
            return;
        const QString who = name.isEmpty() ? id : QStringLiteral("%1 (%2)").arg(name, id);
        if (!confirmSerial(this, m_selectedSerial, QStringLiteral("Fingerabdruck löschen"),
                           QStringLiteral("Der Fingerabdruck %1 wird gelöscht. "
                                          "Zum Bestätigen die Seriennummer eintippen.")
                               .arg(who),
                           QStringLiteral("Endgültig löschen")))
            return;
        m_fidoConfigId = id;
        m_fidoConfigName = name;
    }
    m_fidoConfigAction = FidoConfigAction::FingerprintsDelete;
    m_fidoConfigLength = 0;
    startFidoConfig(elevated);
}

void MainWindow::finishFidoConfig(bool ok, int action, const QString &detail, const QString &output)
{
    QString body = output;
    const auto kind = FidoConfigAction(action);
    if (!ok && fidoAccessDenied(detail)) {
        body.fill(QLatin1Char('0'));
        body.clear();
        setBusy(false);
        QMessageBox box(this);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(QStringLiteral("Erhöhte Sitzung"));
        box.setText(QStringLiteral(
            "Windows hat den FIDO-Zugriff abgelehnt. Nur dieser Aufruf wird erhöht neu gestartet. "
            "Den PIN bitte noch einmal eingeben. Er geht nicht auf die Befehlszeile."));
        auto *accept = box.addButton(QStringLiteral("Erhöhen"), QMessageBox::AcceptRole);
        box.addButton(QStringLiteral("Abbrechen"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() == accept) {
            switch (kind) {
            case FidoConfigAction::ToggleAlwaysUv:
                toggleFidoUvClicked(true);
                break;
            case FidoConfigAction::SetMinLength:
                setFidoMinLengthClicked(true);
                break;
            case FidoConfigAction::ForceChange:
                forceFidoPinClicked(true);
                break;
            case FidoConfigAction::FingerprintsList:
                listFidoPrintsClicked(true);
                break;
            case FidoConfigAction::FingerprintsAdd:
                addFidoPrintClicked(true);
                break;
            case FidoConfigAction::FingerprintsRename:
                renameFidoPrintClicked(true);
                break;
            case FidoConfigAction::FingerprintsDelete:
                deleteFidoPrintClicked(true);
                break;
            }
        } else {
            m_fidoConfigName.clear();
            m_fidoConfigId.clear();
            statusBar()->showMessage(QStringLiteral("Die erhöhte Sitzung wurde abgebrochen."));
        }
        return;
    }

    if (!ok) {
        body.fill(QLatin1Char('0'));
        body.clear();
        m_fidoConfigName.clear();
        m_fidoConfigId.clear();
        m_pendingStatus = fidoConfigText(false, kind, detail, QString());
        refresh();
        return;
    }

    if (kind == FidoConfigAction::FingerprintsList) {
        const QList<FidoPrint> prints = parseFidoPrints(body);
        body.fill(QLatin1Char('0'));
        body.clear();
        const QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        m_fidoPrints->setRowCount(prints.size());
        for (int row = 0; row < prints.size(); ++row) {
            const FidoPrint &print = prints.at(row);
            const auto selectable = [](const QString &text) {
                QTableWidgetItem *item = cell(text);
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                return item;
            };
            auto *ident = selectable(print.id);
            ident->setFont(fixed);
            m_fidoPrints->setItem(row, 0, selectable(print.name.isEmpty() ? dash() : print.name));
            m_fidoPrints->setItem(row, 1, ident);
        }
        m_fidoPrintsLoaded = true;
        m_pendingStatus = prints.isEmpty() ? QStringLiteral("Keine Fingerabdrücke.")
                                           : QStringLiteral("%1 Fingerabdrücke gelesen.").arg(prints.size());
        refresh();
        return;
    }

    if (kind == FidoConfigAction::FingerprintsRename) {
        for (int row = 0; row < m_fidoPrints->rowCount(); ++row) {
            const QTableWidgetItem *ident = m_fidoPrints->item(row, 1);
            if (ident && ident->text() == m_fidoConfigId && m_fidoPrints->item(row, 0))
                m_fidoPrints->item(row, 0)->setText(m_fidoConfigName);
        }
    } else if (kind == FidoConfigAction::FingerprintsDelete) {
        for (int row = m_fidoPrints->rowCount() - 1; row >= 0; --row) {
            const QTableWidgetItem *ident = m_fidoPrints->item(row, 1);
            if (ident && ident->text() == m_fidoConfigId)
                m_fidoPrints->removeRow(row);
        }
        if (m_fidoPrints->rowCount() == 0)
            m_fidoPrintsLoaded = false;
    } else if (kind == FidoConfigAction::FingerprintsAdd) {
        clearFidoPrints();
    }

    const QString status = fidoConfigText(true, kind, detail, body);
    body.fill(QLatin1Char('0'));
    body.clear();
    m_fidoConfigName.clear();
    m_fidoConfigId.clear();
    m_pendingStatus = status;
    refresh();
}

namespace {

QString sshDirectory()
{
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    const QString dir = QDir(home).filePath(QStringLiteral(".ssh"));
    return QDir(dir).exists() ? dir : home;
}

bool sshLineOk(const QString &text)
{
    return !text.contains(QLatin1Char('\n')) && !text.contains(QLatin1Char('\r'));
}

bool opensshKeyTypeOk(const QString &keyType)
{
    const QString id = keyType.toLower();
    if (id.isEmpty())
        return true;
    return !id.contains(QLatin1String("x25519")) && !id.contains(QLatin1String("ml-"))
           && !id.contains(QLatin1String("mlkem")) && !id.contains(QLatin1String("mldsa"));
}

QString sshFailureText(const QString &detail)
{
    const QString text = detail.toLower();
    if (text.contains(QLatin1String("incorrect pin")) || text.contains(QLatin1String("invalid pin"))
        || text.contains(QLatin1String("wrong pin")) || text.contains(QLatin1String("pin verification")))
        return QStringLiteral("Falscher FIDO-PIN. Ein Fehlversuch verbraucht einen.");
    if (text.contains(QLatin1String("no fido"))
        || (text.contains(QLatin1String("device")) && text.contains(QLatin1String("not found"))))
        return QStringLiteral("ssh-keygen findet keinen Security Key.");
    if (detail.trimmed().isEmpty())
        return QStringLiteral("ssh-keygen ist fehlgeschlagen.");
    return detail.trimmed();
}

} // namespace

void MainWindow::startSsh(SshRequest request, const QString &status)
{
    setBusy(true);
    statusBar()->showMessage(status);
    m_client->runSsh(request);
    wipeSecret(request.pin);
}

void MainWindow::generateSshClicked()
{
    if (!m_fidoListReady || m_client->busy() || m_selectedSerial.isEmpty())
        return;
    if (Settings::resolveSshKeygen().isEmpty()) {
        statusBar()->showMessage(Settings::missingSshKeygenText());
        return;
    }
    const FidoRecord fido = m_client->snapshot().fido;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("SSH-Schlüssel"));
    dialog.setMinimumWidth(520);
    auto *layout = new QVBoxLayout(&dialog);
    auto *info = new QLabel(QStringLiteral(
        "Auf dem PC entstehen die Stub-Datei und daneben der öffentliche Schlüssel mit der Endung .pub. "
        "Der private Schlüssel bleibt auf dem YubiKey. Die Stub-Datei ist nur die Referenz, damit OpenSSH den Key findet. "
        "Liegt der Schlüssel auf dem Key, lässt sich die Stub-Datei später neu holen. "
        "Gesichert ist er mit dem FIDO-PIN, nicht mit einer Passphrase der Datei. "
        "Der PIN geht an die Konsole von ssh-keygen und steht nicht in der Befehlszeile. "
        "Unter Windows kann zusätzlich ein Systemfenster nach dem PIN fragen. Danach den Key berühren."));
    info->setWordWrap(true);
    layout->addWidget(info);
    if (fido.attemptsRemaining == 1) {
        auto *warn = new QLabel(QStringLiteral(
            "Noch 1 Versuch. Ein Fehlversuch sperrt den PIN. Ein Reset löscht die Anmeldedaten."));
        warn->setWordWrap(true);
        layout->addWidget(warn);
    } else if (fido.attemptsRemaining > 1) {
        auto *warn = new QLabel(QStringLiteral("Noch %1 Versuche. Ein Fehlversuch verbraucht einen davon.")
                                    .arg(fido.attemptsRemaining));
        warn->setWordWrap(true);
        layout->addWidget(warn);
    }
    auto *algorithm = new QComboBox;
    algorithm->addItem(QStringLiteral("Ed25519"), QStringLiteral("ed25519-sk"));
    algorithm->addItem(QStringLiteral("ECDSA P-256"), QStringLiteral("ecdsa-sk"));
    auto *application = new QLineEdit(QStringLiteral("ssh:"));
    auto *comment = new QLineEdit;
    auto *resident = new QCheckBox(QStringLiteral("Auf dem Key speichern"));
    resident->setChecked(true);
    auto *verify = new QCheckBox(QStringLiteral("PIN bei jeder Anmeldung"));
    auto *path = new QLineEdit;
    path->setReadOnly(true);
    auto suggest = [algorithm, path]() {
        const QString name = algorithm->currentData().toString() == QLatin1String("ecdsa-sk")
                                 ? QStringLiteral("id_ecdsa_sk")
                                 : QStringLiteral("id_ed25519_sk");
        const QString current = path->text();
        const bool untouched = current.isEmpty() || current.endsWith(QLatin1String("/id_ed25519_sk"))
                               || current.endsWith(QLatin1String("/id_ecdsa_sk"))
                               || current.endsWith(QLatin1String("\\id_ed25519_sk"))
                               || current.endsWith(QLatin1String("\\id_ecdsa_sk"));
        if (untouched)
            path->setText(QDir(sshDirectory()).filePath(name));
    };
    suggest();
    QObject::connect(algorithm, &QComboBox::currentIndexChanged, &dialog, suggest);
    auto *browse = new QPushButton(QStringLiteral("Datei…"));
    QObject::connect(browse, &QPushButton::clicked, &dialog, [this, path]() {
        const QString file = QFileDialog::getSaveFileName(
            this, QStringLiteral("Stub-Datei speichern"), path->text(), QStringLiteral("Alle Dateien (*)"));
        if (!file.isEmpty())
            path->setText(file);
    });
    auto *fileRow = new QHBoxLayout;
    fileRow->addWidget(path, 1);
    fileRow->addWidget(browse);
    auto *pin = new QLineEdit;
    pin->setEchoMode(QLineEdit::Password);
    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Algorithmus"), algorithm);
    form->addRow(QStringLiteral("Anwendung"), application);
    form->addRow(QStringLiteral("Kommentar"), comment);
    form->addRow(QStringLiteral("Stub-Datei"), fileRow);
    form->addRow(QString(), resident);
    form->addRow(QString(), verify);
    form->addRow(QStringLiteral("FIDO-PIN"), pin);
    layout->addLayout(form);
    auto *problem = new QLabel;
    problem->setWordWrap(true);
    layout->addWidget(problem);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Erzeugen"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Abbrechen"));
    layout->addWidget(buttons);
    SshRequest request;
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        const QString app = application->text().trimmed();
        if (!app.startsWith(QLatin1String("ssh:")) || app.contains(QLatin1Char(' ')) || !sshLineOk(app)
            || app.size() > 100) {
            problem->setText(QStringLiteral("Die Anwendung muss mit ssh: beginnen und darf kein Leerzeichen enthalten."));
            return;
        }
        if (!sshLineOk(comment->text()) || comment->text().size() > 200) {
            problem->setText(QStringLiteral("Der Kommentar enthält einen Zeilenumbruch oder ist zu lang."));
            return;
        }
        if (path->text().isEmpty()) {
            problem->setText(QStringLiteral("Die Stub-Datei fehlt."));
            return;
        }
        if (QFileInfo::exists(path->text()) || QFileInfo::exists(path->text() + QStringLiteral(".pub"))) {
            problem->setText(QStringLiteral("Die Stub-Datei oder der öffentliche Schlüssel ist schon da."));
            return;
        }
        if (resident->isChecked() && pin->text().isEmpty()) {
            problem->setText(QStringLiteral("Der FIDO-PIN fehlt."));
            return;
        }
        if (!sshLineOk(pin->text()) || pin->text().toUtf8().size() > 63) {
            problem->setText(QStringLiteral("Der PIN ist zu lang oder enthält einen Zeilenumbruch."));
            return;
        }
        request.action = SshAction::Generate;
        request.type = algorithm->currentData().toString();
        request.path = path->text();
        request.comment = comment->text().trimmed();
        request.application = app;
        request.pin = pin->text();
        request.resident = resident->isChecked();
        request.verify = verify->isChecked();
        dialog.accept();
    });
    const bool accepted = dialog.exec() == QDialog::Accepted;
    wipeEdit(pin);
    if (!accepted) {
        wipeSecret(request.pin);
        return;
    }
    const QString status = request.resident
                               ? QStringLiteral("ssh-keygen legt den Schlüssel an. Den PIN bestätigen, falls Windows danach fragt, und den Key berühren.")
                               : QStringLiteral("ssh-keygen legt den Schlüssel an. Ohne Speichern auf dem Key lässt sich die Stub-Datei nicht wiederherstellen. Den Key berühren.");
    startSsh(request, status);
}

void MainWindow::downloadSshClicked()
{
    if (!m_fidoListReady || m_client->busy() || m_selectedSerial.isEmpty())
        return;
    if (Settings::resolveSshKeygen().isEmpty()) {
        statusBar()->showMessage(Settings::missingSshKeygenText());
        return;
    }
    const FidoRecord fido = m_client->snapshot().fido;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("SSH-Stubs holen"));
    dialog.setMinimumWidth(520);
    auto *layout = new QVBoxLayout(&dialog);
    auto *info = new QLabel(QStringLiteral(
        "ssh-keygen liest die auf dem Key gespeicherten SSH-Schlüssel und schreibt Stub-Datei und öffentlichen Schlüssel "
        "in den gewählten Ordner. Der private Schlüssel bleibt auf dem YubiKey. "
        "Der PIN geht an die Konsole von ssh-keygen."));
    info->setWordWrap(true);
    layout->addWidget(info);
    if (fido.attemptsRemaining == 1) {
        auto *warn = new QLabel(QStringLiteral(
            "Noch 1 Versuch. Ein Fehlversuch sperrt den PIN. Ein Reset löscht die Anmeldedaten."));
        warn->setWordWrap(true);
        layout->addWidget(warn);
    }
    auto *path = new QLineEdit(sshDirectory());
    path->setReadOnly(true);
    auto *browse = new QPushButton(QStringLiteral("Ordner…"));
    QObject::connect(browse, &QPushButton::clicked, &dialog, [this, path]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this, QStringLiteral("Ordner für die Stub-Dateien"), path->text());
        if (!dir.isEmpty())
            path->setText(dir);
    });
    auto *fileRow = new QHBoxLayout;
    fileRow->addWidget(path, 1);
    fileRow->addWidget(browse);
    auto *pin = new QLineEdit;
    pin->setEchoMode(QLineEdit::Password);
    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Ordner"), fileRow);
    form->addRow(QStringLiteral("FIDO-PIN"), pin);
    layout->addLayout(form);
    auto *problem = new QLabel;
    problem->setWordWrap(true);
    layout->addWidget(problem);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Holen"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Abbrechen"));
    layout->addWidget(buttons);
    SshRequest request;
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (path->text().isEmpty() || !QDir(path->text()).exists()) {
            problem->setText(QStringLiteral("Der Ordner fehlt."));
            return;
        }
        if (pin->text().isEmpty() || !sshLineOk(pin->text()) || pin->text().toUtf8().size() > 63) {
            problem->setText(QStringLiteral("Der PIN fehlt, ist zu lang oder enthält einen Zeilenumbruch."));
            return;
        }
        request.action = SshAction::Download;
        request.path = path->text();
        request.pin = pin->text();
        dialog.accept();
    });
    const bool accepted = dialog.exec() == QDialog::Accepted;
    wipeEdit(pin);
    if (!accepted) {
        wipeSecret(request.pin);
        return;
    }
    startSsh(request, QStringLiteral("ssh-keygen holt die Stubs. Den PIN bestätigen, falls Windows danach fragt, und den Key berühren."));
}

void MainWindow::finishSsh(bool ok, int action, const QString &detail)
{
    const auto kind = SshAction(action);
    if (!ok)
        m_pendingStatus = sshFailureText(detail);
    else if (kind == SshAction::Download) {
        m_pendingStatus = QStringLiteral("Die Stub-Dateien wurden geschrieben.");
        if (detail.contains(QLatin1Char('\n')) || detail.contains(QLatin1String("Saved"), Qt::CaseInsensitive))
            QMessageBox::information(this, QStringLiteral("SSH-Stubs"), detail);
    } else {
        m_pendingStatus = QStringLiteral(
            "Der SSH-Schlüssel wurde angelegt. Die Stub-Datei und der öffentliche Schlüssel liegen bereit. "
            "Der private Schlüssel bleibt auf dem YubiKey.");
    }
    refresh();
}

void MainWindow::clearFidoCredentials()
{
    m_fidoCredentialsLoaded = false;
    wipeSecret(m_fidoPendingDelete);
    if (m_fidoCredentials)
        m_fidoCredentials->setRowCount(0);
}

void MainWindow::listFidoCredentialsClicked(bool elevated)
{
    if (!m_fidoListReady || m_client->busy() || m_selectedSerial.isEmpty())
        return;
    const YkmanSnapshot snap = m_client->snapshot();
    const bool reader = serialOnReader(snap, m_selectedSerial);
    const FidoRecord fido = snap.fido;
    QString hint = QStringLiteral("Die Liste wird erst danach gelesen.");
    if (reader)
        hint += QStringLiteral(
            " Der Key liegt auf dem Lesegerät. Das Auslesen gelingt dort nur direkt nach dem Auflegen. Sicher geht es über USB.");
    QString pin = askFidoVerifyPin(this, fido, QStringLiteral("Anmeldedaten"), hint,
                                   QStringLiteral("Anzeigen"));
    if (pin.isEmpty())
        return;
    setBusy(true);
    statusBar()->showMessage(QStringLiteral("Die Anmeldedaten werden gelesen. Falls der Key blinkt, jetzt berühren."));
    m_client->runFidoCredentials(m_selectedSerial, FidoCredentialAction::List, pin, QString(), elevated);
    wipeSecret(pin);
}

void MainWindow::deleteFidoCredentialClicked(bool elevated)
{
    if (!m_fidoListReady || !m_fidoCredentialsLoaded || m_client->busy() || m_selectedSerial.isEmpty())
        return;
    const QModelIndexList rows = m_fidoCredentials->selectionModel()
                                     ? m_fidoCredentials->selectionModel()->selectedRows()
                                     : QModelIndexList{};
    if (rows.isEmpty())
        return;
    const int row = rows.first().row();
    const QTableWidgetItem *idItem = m_fidoCredentials->item(row, 0);
    if (!idItem)
        return;
    const QString id = idItem->data(Qt::UserRole).toString();
    const QString service = idItem->text();
    const QString user = m_fidoCredentials->item(row, 1) ? m_fidoCredentials->item(row, 1)->text() : QString();
    if (!QRegularExpression(QStringLiteral("^[0-9a-fA-F]+$")).match(id).hasMatch())
        return;
    const QString who = user.isEmpty() ? service : QStringLiteral("%1 (%2)").arg(service, user);
    if (!confirmSerial(this, m_selectedSerial, QStringLiteral("Anmeldedaten löschen"),
                       QStringLiteral("Die Anmeldedaten für %1 werden gelöscht. "
                                      "Zum Bestätigen die Seriennummer eintippen.")
                           .arg(who),
                       QStringLiteral("Endgültig löschen")))
        return;
    const FidoRecord fido = m_client->snapshot().fido;
    QString pin = askFidoVerifyPin(this, fido, QStringLiteral("Anmeldedaten löschen"),
                                   QStringLiteral("Der PIN wird für diesen einen Eintrag gebraucht."),
                                   QStringLiteral("Löschen bestätigen"));
    if (pin.isEmpty())
        return;
    m_fidoPendingDelete = id;
    setBusy(true);
    statusBar()->showMessage(QStringLiteral("Die Anmeldedaten werden gelöscht. Falls der Key blinkt, jetzt berühren."));
    m_client->runFidoCredentials(m_selectedSerial, FidoCredentialAction::Delete, pin, id, elevated);
    wipeSecret(pin);
}

void MainWindow::finishFidoCredentials(bool ok, int action, const QString &detail, const QString &output)
{
    QString body = output;
    const auto kind = FidoCredentialAction(action);
    if (!ok && fidoAccessDenied(detail)) {
        wipeSecret(body);
        setBusy(false);
        QMessageBox box(this);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(QStringLiteral("Erhöhte Sitzung"));
        box.setText(QStringLiteral(
            "Windows hat den FIDO-Zugriff abgelehnt. Nur dieser Aufruf wird erhöht neu gestartet. "
            "Den PIN bitte noch einmal eingeben. Er geht nicht auf die Befehlszeile."));
        auto *accept = box.addButton(QStringLiteral("Erhöhen"), QMessageBox::AcceptRole);
        box.addButton(QStringLiteral("Abbrechen"), QMessageBox::RejectRole);
        box.exec();
        wipeSecret(m_fidoPendingDelete);
        if (box.clickedButton() == accept) {
            if (kind == FidoCredentialAction::Delete)
                deleteFidoCredentialClicked(true);
            else
                listFidoCredentialsClicked(true);
        } else {
            statusBar()->showMessage(QStringLiteral("Die erhöhte Sitzung wurde abgebrochen."));
        }
        return;
    }

    if (!ok) {
        wipeSecret(body);
        wipeSecret(m_fidoPendingDelete);
        m_pendingStatus = fidoCredentialText(detail, serialOnReader(m_client->snapshot(), m_selectedSerial));
        refresh();
        return;
    }

    if (kind == FidoCredentialAction::Delete) {
        const QString removed = m_fidoPendingDelete;
        wipeSecret(m_fidoPendingDelete);
        for (int row = m_fidoCredentials->rowCount() - 1; row >= 0; --row) {
            const QTableWidgetItem *item = m_fidoCredentials->item(row, 0);
            if (item && item->data(Qt::UserRole).toString() == removed)
                m_fidoCredentials->removeRow(row);
        }
        if (m_fidoCredentials->rowCount() == 0)
            m_fidoCredentialsLoaded = false;
        wipeSecret(body);
        m_pendingStatus = QStringLiteral("Die Anmeldedaten wurden gelöscht.");
        refresh();
        return;
    }

    const QList<FidoCredential> credentials = parseFidoCredentials(body);
    const bool none = body.contains(QStringLiteral("No discoverable credentials"), Qt::CaseInsensitive);
    wipeSecret(body);
    if (credentials.isEmpty() && !none && !detail.isEmpty()) {
        m_pendingStatus = QStringLiteral("Die Anmeldedaten ließen sich nicht lesen.");
        refresh();
        return;
    }
    const QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_fidoCredentials->setRowCount(credentials.size());
    for (int row = 0; row < credentials.size(); ++row) {
        const FidoCredential &credential = credentials.at(row);
        const auto selectable = [](const QString &text) {
            QTableWidgetItem *item = cell(text);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            return item;
        };
        auto *service = selectable(credential.rpId.isEmpty() ? dash() : credential.rpId);
        service->setData(Qt::UserRole, credential.id);
        auto *ident = selectable(credential.id.size() <= 8 ? credential.id
                                                           : credential.id.left(8) + QStringLiteral("…"));
        ident->setFont(fixed);
        m_fidoCredentials->setItem(row, 0, service);
        m_fidoCredentials->setItem(row, 1, selectable(credential.userName.isEmpty() ? dash() : credential.userName));
        m_fidoCredentials->setItem(row, 2, selectable(credential.displayName.isEmpty() ? dash() : credential.displayName));
        m_fidoCredentials->setItem(row, 3, ident);
    }
    m_fidoCredentialsLoaded = true;
    m_pendingStatus = credentials.isEmpty()
                          ? QStringLiteral("Keine auffindbaren Anmeldedaten.")
                          : QStringLiteral("%1 Anmeldedaten gelesen.").arg(credentials.size());
    refresh();
}

void MainWindow::clearInterfaces()
{
    while (QLayoutItem *item = m_checkLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_toggles.clear();
    m_apply->setEnabled(false);
}

void MainWindow::showInterfaces(const DeviceRecord &device)
{
    clearInterfaces();

    int row = 0;
    m_checkLayout->addWidget(captionLabel(QStringLiteral("Anwendung")), row, 0);
    m_checkLayout->addWidget(captionLabel(QStringLiteral("USB")), row, 1, Qt::AlignHCenter);
    if (device.nfcCapable)
        m_checkLayout->addWidget(captionLabel(QStringLiteral("NFC")), row, 2, Qt::AlignHCenter);
    ++row;

    const auto watch = [this]() {
        bool changed = false;
        for (const InterfaceToggle &toggle : m_toggles)
            changed = changed || (toggle.box && toggle.box->isChecked() != toggle.enabled);
        m_apply->setEnabled(changed);
    };

    for (const UsbApp &app : device.applications) {
        m_checkLayout->addWidget(new QLabel(app.name), row, 0);
        const auto addBox = [&](int column, const QString &transport, const QString &state) {
            if (state.isEmpty())
                return;
            auto *box = new QCheckBox;
            box->setChecked(state == QLatin1String("enabled"));
            InterfaceToggle toggle;
            toggle.id = app.id;
            toggle.name = app.name;
            toggle.transport = transport;
            toggle.enabled = box->isChecked();
            toggle.box = box;
            m_toggles.append(toggle);
            m_checkLayout->addWidget(box, row, column, Qt::AlignHCenter);
            connect(box, &QCheckBox::toggled, this, watch);
        };
        addBox(1, QStringLiteral("usb"), app.usb);
        if (device.nfcCapable)
            addBox(2, QStringLiteral("nfc"), app.nfc);
        ++row;
    }
}

void MainWindow::revertInterfaces()
{
    for (const InterfaceToggle &toggle : m_toggles) {
        toggle.box->blockSignals(true);
        toggle.box->setChecked(toggle.enabled);
        toggle.box->blockSignals(false);
    }
    m_apply->setEnabled(false);
}

QString MainWindow::askLockCode()
{
    bool ok = false;
    const QString code = QInputDialog::getText(
        this,
        QStringLiteral("Lock-Code"),
        QStringLiteral("Die Konfiguration ist gesperrt. Den vorhandenen Lock-Code eingeben, 32 Hex-Zeichen."),
        QLineEdit::Password,
        QString(),
        &ok);
    if (!ok)
        return {};
    const QString trimmed = code.trimmed();
    if (!QRegularExpression(QStringLiteral("^[0-9a-fA-F]{32}$")).match(trimmed).hasMatch()) {
        statusBar()->showMessage(QStringLiteral("Der Lock-Code muss genau 32 Hex-Zeichen haben."));
        return {};
    }
    return trimmed;
}

void MainWindow::applyInterfaces(const QString &lockCode)
{
    if (m_client->busy() || m_selectedSerial.isEmpty())
        return;

    QStringList usbEnable;
    QStringList usbDisable;
    QStringList nfcEnable;
    QStringList nfcDisable;
    QStringList summary;
    bool usbLeft = false;
    bool hasUsb = false;
    for (const InterfaceToggle &toggle : m_toggles) {
        const bool now = toggle.box->isChecked();
        const bool overNfc = toggle.transport == QLatin1String("nfc");
        if (toggle.transport == QLatin1String("usb")) {
            hasUsb = true;
            usbLeft = usbLeft || now;
        }
        if (now == toggle.enabled)
            continue;
        const QString via = overNfc ? QStringLiteral("NFC") : QStringLiteral("USB");
        const QString action = now ? QStringLiteral("einschalten") : QStringLiteral("ausschalten");
        summary << QStringLiteral("%1 über %2 %3").arg(toggle.name, via, action);
        QStringList &target = overNfc ? (now ? nfcEnable : nfcDisable) : (now ? usbEnable : usbDisable);
        target << toggle.id;
    }
    if (summary.isEmpty())
        return;
    if (hasUsb && !usbLeft) {
        statusBar()->showMessage(QStringLiteral(
            "Über USB muss mindestens eine Anwendung eingeschaltet bleiben."));
        return;
    }

    if (lockCode.isEmpty()) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(QStringLiteral("Anwendungen ändern"));
        box.setText(QStringLiteral(
                        "Diese Änderungen schreiben?\n\n%1\n\n"
                        "Geschrieben wird über USB. Ändert sich dabei eine USB-Schnittstelle, "
                        "verbindet sich der Key kurz neu.")
                        .arg(summary.join(QLatin1Char('\n'))));
        auto *accept = box.addButton(QStringLiteral("Übernehmen"), QMessageBox::AcceptRole);
        box.addButton(QStringLiteral("Abbrechen"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() != accept) {
            revertInterfaces();
            return;
        }
    }

    setBusy(true);
    statusBar()->clearMessage();
    m_awaitingReconnect = true;
    m_client->setApplications(m_selectedSerial, usbEnable, usbDisable, nfcEnable, nfcDisable, lockCode);
}

QString MainWindow::selectedOathAccount() const
{
    if (!m_oathAccounts)
        return {};
    const QModelIndexList rows = m_oathAccounts->selectionModel()->selectedRows();
    if (rows.isEmpty() || !m_oathAccounts->item(rows.first().row(), 0))
        return {};
    return m_oathAccounts->item(rows.first().row(), 0)->text();
}

void MainWindow::applyOathActions(bool busy)
{
    if (!m_oathCodes)
        return;
    const QString selected = selectedOathAccount();
    const bool row = !busy && m_oathReady && !selected.isEmpty();
    QString code;
    if (row) {
        const int index = m_oathAccounts->selectionModel()->selectedRows().first().row();
        if (QTableWidgetItem *item = m_oathAccounts->item(index, 3))
            code = item->text();
    }
    const bool digits = QRegularExpression(QStringLiteral("^\\d{6,8}$")).match(code).hasMatch();
    m_oathCodes->setEnabled(!busy && m_oathReady && m_oathAccounts->rowCount() > 0);
    m_oathOne->setEnabled(row);
    m_oathCopy->setEnabled(row && digits);
    m_oathAdd->setEnabled(!busy && m_oathReady);
    m_oathRename->setEnabled(row);
    m_oathDelete->setEnabled(row);
    m_oathPassword->setEnabled(!busy && (m_oathReady || m_oathLocked));
}

void MainWindow::showOath(const OathRecord &oath)
{
    if (m_oathCodeTimer)
        m_oathCodeTimer->stop();
    m_oathAccounts->setRowCount(0);
    m_oathLocked = oath.error.isEmpty() && oath.passwordProtected && !oath.loaded;
    m_oathReady = oath.error.isEmpty() && oath.loaded;
    if (!oath.error.isEmpty()) {
        showNote(m_oathNote, oath.error);
        m_oathPassword->setText(QStringLiteral("Passwort"));
        applyOathActions(m_client->busy());
        return;
    }
    if (m_oathLocked) {
        showNote(m_oathNote, QStringLiteral(
            "Die Konten sind mit einem Passwort geschützt. Es geht an die Konsole von ykman. "
            "YubiGUI speichert es nicht auf dem Rechner."));
        m_oathPassword->setText(QStringLiteral("Entsperren"));
        applyOathActions(m_client->busy());
        return;
    }
    if (!oath.loaded) {
        showNote(m_oathNote, QStringLiteral("Die Konten erscheinen, sobald der Key gelesen ist."));
        m_oathPassword->setText(QStringLiteral("Passwort"));
        applyOathActions(m_client->busy());
        return;
    }

    m_oathAccounts->setRowCount(oath.accounts.size());
    const QFont codeFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    for (int row = 0; row < oath.accounts.size(); ++row) {
        const OathAccount &account = oath.accounts.at(row);
        const auto selectable = [](const QString &text) {
            QTableWidgetItem *item = cell(text);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            return item;
        };
        m_oathAccounts->setItem(row, 0, selectable(account.name));
        m_oathAccounts->setItem(row, 1, selectable(account.type));
        m_oathAccounts->setItem(row, 2, selectable(account.type == QLatin1String("TOTP") && account.period > 0
                                                       ? QStringLiteral("%1 s").arg(account.period)
                                                       : dash()));
        auto *code = selectable(QString());
        code->setFont(codeFont);
        m_oathAccounts->setItem(row, 3, code);
    }
    showNote(m_oathNote, oath.passwordProtected && oath.passwordRemembered
        ? QStringLiteral("ykman hat das Passwort auf diesem Rechner gespeichert. YubiGUI speichert keins.")
        : QString());
    m_oathPassword->setText(oath.passwordProtected ? QStringLiteral("Passwort ändern")
                                                   : QStringLiteral("Passwort setzen"));
    applyOathActions(m_client->busy());
}

void MainWindow::clearOathCodes()
{
    if (!m_oathAccounts)
        return;
    for (int row = 0; row < m_oathAccounts->rowCount(); ++row) {
        if (QTableWidgetItem *item = m_oathAccounts->item(row, 3))
            item->setText(QString());
    }
    applyOathActions(m_client->busy());
}

void MainWindow::startOath(OathRequest request)
{
    if (m_client->busy() || m_selectedSerial.isEmpty()) {
        wipeOath(request);
        return;
    }
    if (request.password.isEmpty() && !m_oathSession.isEmpty()
        && request.action != OathAction::SetPassword && request.action != OathAction::ClearPassword)
        request.password = m_oathSession;
    setBusy(true);
    if (request.action == OathAction::Code || request.action == OathAction::CodeOne)
        statusBar()->showMessage(QStringLiteral("Falls der Key blinkt, jetzt berühren."));
    else if (request.action == OathAction::List)
        statusBar()->showMessage(QStringLiteral("Die Konten werden gelesen."));
    else
        statusBar()->showMessage(QStringLiteral("OATH wird geschrieben."));
    m_client->runOath(m_selectedSerial, request);
    wipeOath(request);
}

bool oathNeedsPassword(const OathRecord &oath, const QString &known)
{
    return oath.passwordProtected && known.isEmpty() && !oath.passwordRemembered;
}

void MainWindow::oathCodesClicked()
{
    if (!m_oathReady || m_client->busy())
        return;
    const OathRecord oath = m_client->snapshot().oath;
    if (oathNeedsPassword(oath, m_oathSession)) {
        oathPasswordClicked();
        return;
    }
    OathRequest request;
    request.action = OathAction::Code;
    startOath(request);
}

void MainWindow::oathOneClicked()
{
    if (!m_oathReady || m_client->busy())
        return;
    const QString name = selectedOathAccount();
    if (name.isEmpty())
        return;
    const OathRecord oath = m_client->snapshot().oath;
    if (!oathQueryUnique(oath.accounts, name)) {
        statusBar()->showMessage(QStringLiteral("Der Name trifft mehrere Konten."));
        return;
    }
    if (oathNeedsPassword(oath, m_oathSession)) {
        oathPasswordClicked();
        return;
    }
    QString type;
    for (const OathAccount &account : oath.accounts) {
        if (account.name == name)
            type = account.type;
    }
    if (type == QLatin1String("HOTP")) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(QStringLiteral("HOTP-Code"));
        box.setText(QStringLiteral(
            "Beim Holen rückt der Zähler vor. Der bisherige Code gilt danach nicht mehr."));
        auto *accept = box.addButton(QStringLiteral("Holen"), QMessageBox::AcceptRole);
        box.addButton(QStringLiteral("Abbrechen"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() != accept)
            return;
    }
    OathRequest request;
    request.action = OathAction::CodeOne;
    request.query = name;
    startOath(request);
}

void MainWindow::oathCopyClicked()
{
    const QModelIndexList rows = m_oathAccounts->selectionModel()->selectedRows();
    if (rows.isEmpty())
        return;
    const QTableWidgetItem *item = m_oathAccounts->item(rows.first().row(), 3);
    if (!item)
        return;
    const QString code = item->text();
    if (!QRegularExpression(QStringLiteral("^\\d{6,8}$")).match(code).hasMatch())
        return;
    QGuiApplication::clipboard()->setText(code);
    statusBar()->showMessage(QStringLiteral("Der Code liegt in der Zwischenablage."));
}

void MainWindow::oathAddClicked()
{
    if (!m_oathReady || m_client->busy())
        return;
    const OathRecord oath = m_client->snapshot().oath;
    if (oathNeedsPassword(oath, m_oathSession)) {
        oathPasswordClicked();
        return;
    }
    OathRequest request = askOathAdd(this, oath.accounts, m_selectedSerial);
    if (request.secret.isEmpty()) {
        wipeOath(request);
        return;
    }
    startOath(request);
}

void MainWindow::oathRenameClicked()
{
    if (!m_oathReady || m_client->busy())
        return;
    const QString name = selectedOathAccount();
    if (name.isEmpty())
        return;
    const OathRecord oath = m_client->snapshot().oath;
    if (!oathQueryUnique(oath.accounts, name)) {
        statusBar()->showMessage(QStringLiteral("Der Name trifft mehrere Konten."));
        return;
    }
    if (oathNeedsPassword(oath, m_oathSession)) {
        oathPasswordClicked();
        return;
    }
    const QString next = askOathRename(this, name);
    if (next.isEmpty())
        return;
    OathRequest request;
    request.action = OathAction::Rename;
    request.query = name;
    request.newName = next;
    startOath(request);
}

void MainWindow::oathDeleteClicked()
{
    if (!m_oathReady || m_client->busy())
        return;
    const QString name = selectedOathAccount();
    if (name.isEmpty())
        return;
    const OathRecord oath = m_client->snapshot().oath;
    if (!oathQueryUnique(oath.accounts, name)) {
        statusBar()->showMessage(QStringLiteral("Der Name trifft mehrere Konten."));
        return;
    }
    if (oathNeedsPassword(oath, m_oathSession)) {
        oathPasswordClicked();
        return;
    }
    if (!confirmSerial(this, m_selectedSerial, QStringLiteral("OATH-Konto löschen"),
                       QStringLiteral("Das Konto wird gelöscht und lässt sich nicht wiederherstellen. "
                                      "Zum Bestätigen die Seriennummer eintippen."),
                       QStringLiteral("Löschen")))
        return;
    OathRequest request;
    request.action = OathAction::Delete;
    request.query = name;
    startOath(request);
}

void MainWindow::oathPasswordClicked()
{
    if (m_client->busy() || m_selectedSerial.isEmpty())
        return;
    const OathRecord oath = m_client->snapshot().oath;
    if (!oath.error.isEmpty())
        return;
    const bool unlock = m_oathLocked;
    if (!unlock && !m_oathReady)
        return;
    OathPasswordChoice choice = askOathPassword(this, unlock, oath.passwordProtected, m_selectedSerial);
    if (!choice.accepted) {
        wipeSecret(choice.current);
        wipeSecret(choice.next);
        return;
    }
    OathRequest request;
    if (unlock) {
        wipeSecret(m_oathSession);
        m_oathSession = choice.current;
        request.action = OathAction::List;
        request.password = choice.current;
    } else if (choice.clear) {
        request.action = OathAction::ClearPassword;
        request.password = choice.current;
    } else {
        wipeSecret(m_oathPendingPassword);
        m_oathPendingPassword = choice.next;
        request.action = OathAction::SetPassword;
        request.password = choice.current;
        request.newPassword = choice.next;
    }
    wipeSecret(choice.current);
    wipeSecret(choice.next);
    startOath(request);
}

void MainWindow::finishOath(bool ok, int action, const QString &detail, const QString &output)
{
    QString body = output;
    const auto kind = OathAction(action);
    if (!ok) {
        if (detail.contains(QLatin1String("wrong password"), Qt::CaseInsensitive))
            wipeSecret(m_oathSession);
        wipeSecret(m_oathPendingPassword);
        wipeSecret(body);
        setBusy(false);
        statusBar()->showMessage(oathFailureText(detail));
        return;
    }

    if (kind == OathAction::Code || kind == OathAction::CodeOne) {
        const QRegularExpression digits(QStringLiteral("^\\d{6,8}$"));
        const QString selected = selectedOathAccount();
        if (kind == OathAction::CodeOne && digits.match(body.trimmed()).hasMatch() && !selected.isEmpty()) {
            for (int row = 0; row < m_oathAccounts->rowCount(); ++row) {
                if (m_oathAccounts->item(row, 0) && m_oathAccounts->item(row, 0)->text() == selected)
                    m_oathAccounts->item(row, 3)->setText(body.trimmed());
            }
        } else {
            const QRegularExpression gap(QStringLiteral(" {2,}"));
            const QStringList lines = body.split(QLatin1Char('\n'));
            for (QString line : lines) {
                line = line.trimmed();
                int cut = -1;
                auto matches = gap.globalMatch(line);
                while (matches.hasNext())
                    cut = matches.next().capturedStart();
                if (cut < 1)
                    continue;
                const QString name = line.left(cut).trimmed();
                QString code = line.mid(cut).trimmed();
                if (code == QLatin1String("[Requires Touch]"))
                    code = QStringLiteral("Bitte berühren");
                else if (code == QLatin1String("[HOTP]"))
                    code = QStringLiteral("Einzeln holen");
                for (int row = 0; row < m_oathAccounts->rowCount(); ++row) {
                    if (m_oathAccounts->item(row, 0) && m_oathAccounts->item(row, 0)->text() == name
                        && m_oathAccounts->item(row, 3))
                        m_oathAccounts->item(row, 3)->setText(code);
                }
            }
        }
        wipeSecret(body);
        setBusy(false);
        m_oathCodeTimer->start(30000);
        statusBar()->showMessage(QStringLiteral("Die Codes bleiben 30 Sekunden sichtbar."));
        return;
    }

    wipeSecret(body);
    if (kind == OathAction::List) {
        const YkmanSnapshot snap = m_client->snapshot();
        showOath(snap.oath);
        setBusy(false);
        const DeviceRecord *shown = snap.devices.isEmpty() ? nullptr : &snap.devices.first();
        for (const DeviceRecord &device : snap.devices) {
            if (device.serial == m_selectedSerial)
                shown = &device;
        }
        statusBar()->showMessage(shown ? connectedMessage(*shown) : QStringLiteral("Verbunden."));
        return;
    }
    if (kind == OathAction::SetPassword) {
        wipeSecret(m_oathSession);
        m_oathSession = m_oathPendingPassword;
        wipeSecret(m_oathPendingPassword);
        m_oathRelist = !m_oathSession.isEmpty();
        m_pendingStatus = QStringLiteral("Das OATH-Passwort wurde gesetzt.");
    } else if (kind == OathAction::ClearPassword) {
        wipeSecret(m_oathSession);
        wipeSecret(m_oathPendingPassword);
        m_oathRelist = false;
        m_pendingStatus = QStringLiteral("Das OATH-Passwort wurde entfernt.");
    } else {
        wipeSecret(m_oathPendingPassword);
        m_oathRelist = m_client->snapshot().oath.passwordProtected && !m_oathSession.isEmpty();
        if (kind == OathAction::Add)
            m_pendingStatus = QStringLiteral("Das Konto wurde angelegt.");
        else if (kind == OathAction::Rename)
            m_pendingStatus = QStringLiteral("Das Konto wurde umbenannt.");
        else if (kind == OathAction::Delete)
            m_pendingStatus = QStringLiteral("Das Konto wurde gelöscht.");
        else
            m_pendingStatus = QStringLiteral("OATH wurde geändert.");
    }
    refresh();
}

namespace {

QString pivTries(int remaining, int total, bool factory)
{
    if (remaining < 0)
        return dash();
    QString text = total > 0 ? QStringLiteral("%1 von %2 Versuchen").arg(remaining).arg(total)
                             : QStringLiteral("%1 Versuche übrig").arg(remaining);
    if (remaining == 0)
        text += QStringLiteral(", gesperrt");
    if (factory)
        text += QStringLiteral(", Werkseinstellung");
    return text;
}

QString pivSlotTitle(const PivSlotInfo &slot)
{
    const QString id = slot.slot.toUpper();
    QString name;
    if (id == QLatin1String("9A"))
        name = QStringLiteral("Authentisierung");
    else if (id == QLatin1String("9C"))
        name = QStringLiteral("Signatur");
    else if (id == QLatin1String("9D"))
        name = QStringLiteral("Schlüsselverwaltung");
    else if (id == QLatin1String("9E"))
        name = QStringLiteral("Kartenauthentisierung");
    else if (id == QLatin1String("F9"))
        name = QStringLiteral("Beglaubigung");
    else if (slot.name.startsWith(QLatin1String("RETIRED"), Qt::CaseInsensitive))
        name = QStringLiteral("Reserve %1").arg(slot.name.mid(7));
    else
        name = slot.name;
    return name.isEmpty() ? id : QStringLiteral("%1 %2").arg(id, name);
}

QString pivBio(const QString &value)
{
    if (value.contains(QLatin1String("Not configured"), Qt::CaseInsensitive))
        return QStringLiteral("Nicht eingerichtet");
    const QRegularExpressionMatch attempts =
        QRegularExpression(QStringLiteral("(\\d+)\\s+attempt")).match(value);
    if (value.contains(QLatin1String("Configured"), Qt::CaseInsensitive) && attempts.hasMatch())
        return QStringLiteral("Eingerichtet, %1 Versuche übrig").arg(attempts.captured(1));
    return value;
}

bool pivLengthOk(const QString &text)
{
    const int bytes = text.toUtf8().size();
    return bytes >= 6 && bytes <= 8 && !text.contains(QLatin1Char('\n')) && !text.contains(QLatin1Char('\r'));
}

struct PivChoice
{
    bool accepted = false;
    QString current;
    QString next;
};

PivChoice askPivSecret(QWidget *parent, const QString &title, const QString &info,
                       const QString &currentLabel, const QString &nextLabel, const QString &acceptLabel)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setMinimumWidth(440);
    auto *layout = new QVBoxLayout(&dialog);
    auto *text = new QLabel(info);
    text->setWordWrap(true);
    layout->addWidget(text);
    auto *current = new QLineEdit;
    auto *next = new QLineEdit;
    auto *repeat = new QLineEdit;
    for (QLineEdit *edit : {current, next, repeat})
        edit->setEchoMode(QLineEdit::Password);
    auto *form = new QFormLayout;
    form->addRow(currentLabel, current);
    form->addRow(nextLabel, next);
    form->addRow(QStringLiteral("Wiederholen"), repeat);
    layout->addLayout(form);
    auto *problem = new QLabel;
    problem->setWordWrap(true);
    layout->addWidget(problem);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(acceptLabel);
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Abbrechen"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (current->text().isEmpty()) {
            problem->setText(QStringLiteral("%1 fehlt.").arg(currentLabel));
            return;
        }
        if (next->text().isEmpty()) {
            problem->setText(QStringLiteral("%1 fehlt.").arg(nextLabel));
            return;
        }
        if (!pivLengthOk(current->text()) || !pivLengthOk(next->text())) {
            problem->setText(QStringLiteral("PIN und PUK brauchen 6 bis 8 Zeichen."));
            return;
        }
        if (next->text() != repeat->text()) {
            problem->setText(QStringLiteral("Die beiden neuen Werte stimmen nicht überein."));
            return;
        }
        dialog.accept();
    });

    PivChoice choice;
    if (dialog.exec() == QDialog::Accepted) {
        choice.accepted = true;
        choice.current = current->text();
        choice.next = next->text();
    }
    wipeEdit(current);
    wipeEdit(next);
    wipeEdit(repeat);
    return choice;
}

QString pivFailureText(const QString &detail)
{
    const QString text = detail.toLower();
    const bool puk = text.contains(QLatin1String("puk"));
    const QString which = puk ? QStringLiteral("Der PUK") : QStringLiteral("Die PIN");
    if (text.contains(QLatin1String("fido is configured")))
        return QStringLiteral(
            "Der PIV-Reset ist gesperrt, solange FIDO eingerichtet ist. "
            "Ein Werksreset des ganzen Keys ist hier nicht vorgesehen.");
    if (text.contains(QLatin1String("wrong management key")) || text.contains(QLatin1String("authentication failed")))
        return QStringLiteral("Der Management-Key wurde abgelehnt.");
    if (text.contains(QLatin1String("cannot sign")) || text.contains(QLatin1String("signing not supported")))
        return QStringLiteral("Dieser Schlüssel kann kein Zertifikat unterschreiben.");
    if (text.contains(QLatin1String("password")) || text.contains(QLatin1String("decrypt")))
        return QStringLiteral("Das Dateipasswort wurde abgelehnt.");
    if (text.contains(QLatin1String("no certificate")) || text.contains(QLatin1String("no cert")))
        return QStringLiteral("In diesem Slot liegt kein Zertifikat.");
    if (text.contains(QLatin1String("invalid subject")))
        return QStringLiteral("Der Name des Zertifikats ist ungültig.");
    if (text.contains(QLatin1String("tries left"))) {
        const QRegularExpressionMatch attempts =
            QRegularExpression(QStringLiteral("(\\d+)\\s+tries")).match(detail);
        if (attempts.hasMatch()) {
            const int left = attempts.captured(1).toInt();
            const QString rest = left == 1 ? QStringLiteral("Noch 1 Versuch.")
                                           : QStringLiteral("Noch %1 Versuche.").arg(left);
            return QStringLiteral("%1 stimmt nicht. %2").arg(which, rest);
        }
    }
    if (text.contains(QLatin1String("is blocked")))
        return puk ? QStringLiteral("Der PUK ist gesperrt.")
                   : QStringLiteral("Die PIN ist gesperrt. Nur der PUK entsperrt sie noch.");
    if (text.contains(QLatin1String("wrong"))) {
        const QRegularExpressionMatch attempts =
            QRegularExpression(QStringLiteral("(\\d+)\\s+attempt")).match(detail);
        if (!attempts.hasMatch())
            return QStringLiteral("%1 stimmt nicht.").arg(which);
        const int left = attempts.captured(1).toInt();
        const QString rest = left == 1 ? QStringLiteral("Noch 1 Versuch.")
                                       : QStringLiteral("Noch %1 Versuche.").arg(left);
        return QStringLiteral("%1 stimmt nicht. %2").arg(which, rest);
    }
    if (text.contains(QLatin1String("complexity")))
        return QStringLiteral("Der neue Wert erfüllt die Komplexitätsregel nicht.");
    if (text.contains(QLatin1String("exactly 8")))
        return QStringLiteral("Der neue Wert muss genau 8 Zeichen lang sein.");
    if (text.contains(QLatin1String("between")) && text.contains(QLatin1String("8")))
        return QStringLiteral("PIN und PUK brauchen 6 bis 8 Zeichen.");
    if (text.contains(QLatin1String("aborted")))
        return QStringLiteral("Abgebrochen.");
    if (detail.trimmed().isEmpty())
        return QStringLiteral("Die PIV-Änderung ist fehlgeschlagen.");
    return detail.trimmed();
}

} // namespace

void MainWindow::applyPivActions(bool busy)
{
    if (!m_changePivPin)
        return;
    m_changePivPin->setEnabled(!busy && m_pivPinReady);
    m_changePivPuk->setEnabled(!busy && m_pivPukReady);
    m_unblockPiv->setEnabled(!busy && m_pivUnblockReady);
    bool certSelected = false;
    bool keySelected = false;
    if (m_pivSlots && m_pivSlots->selectionModel()) {
        const QModelIndexList rows = m_pivSlots->selectionModel()->selectedRows();
        if (!rows.isEmpty()) {
            if (QTableWidgetItem *item = m_pivSlots->item(rows.first().row(), 0)) {
                certSelected = item->data(Qt::UserRole + 1).toBool();
                keySelected = item->data(Qt::UserRole + 2).toBool();
            }
        }
    }
    m_pivGenerate->setEnabled(!busy && m_pivWriteReady);
    m_pivImportKey->setEnabled(!busy && m_pivWriteReady);
    m_pivImportKey->setToolTip(QStringLiteral(
        "Legt einen vorhandenen privaten Schlüssel in den Slot. Die Datei auf dem Rechner bleibt liegen."));
    m_pivExport->setEnabled(!busy && keySelected);
    m_pivExport->setToolTip(QStringLiteral(
        "Schreibt den öffentlichen Schlüssel des Slots. Den privaten Schlüssel gibt der Key nicht heraus."));
    m_pivExportCert->setEnabled(!busy && certSelected);
    m_pivExportCert->setToolTip(QStringLiteral(
        "Schreibt das Zertifikat aus dem Slot. Der private Schlüssel bleibt auf dem YubiKey."));
    m_pivRequest->setEnabled(!busy && m_pivWriteReady && keySelected);
    m_pivRequest->setToolTip(QStringLiteral(
        "Schreibt eine Zertifikatsanfrage für eine CA. Der private Schlüssel bleibt auf dem YubiKey."));
    m_pivImport->setEnabled(!busy && m_pivWriteReady);
    m_pivSign->setEnabled(!busy && m_pivWriteReady);
    m_pivDeleteCert->setEnabled(!busy && m_pivWriteReady && certSelected);
    m_pivManagementButton->setEnabled(!busy && m_pivManageReady);
    m_pivReset->setEnabled(!busy && m_pivResetReady);
}

void MainWindow::showPiv(const PivRecord &piv)
{
    const auto showBio = [this](bool visible) {
        m_pivBio->setVisible(visible);
        if (QWidget *label = m_pivForm->labelForField(m_pivBio))
            label->setVisible(visible);
    };
    m_pivSlots->setRowCount(0);
    if (!piv.error.isEmpty()) {
        m_pivVersion->setText(dash());
        m_pivPin->setText(dash());
        m_pivPuk->setText(dash());
        m_pivManagement->setText(dash());
        m_pivBio->setText(dash());
        showBio(false);
        showNote(m_pivNote, piv.error);
        m_pivPinReady = false;
        m_pivPukReady = false;
        m_pivUnblockReady = false;
        m_pivWriteReady = false;
        m_pivManageReady = false;
        m_pivResetReady = false;
        applyPivActions(m_client->busy());
        return;
    }

    m_pivVersion->setText(piv.version.isEmpty() ? dash() : piv.version);
    m_pivPin->setText(pivTries(piv.pinAttempts, piv.pinTotal, piv.pinDefault));
    m_pivPuk->setText(piv.pukAttempts < 0 ? dash() : pivTries(piv.pukAttempts, piv.pukTotal, piv.pukDefault));
    QString management = piv.managementAlgorithm.isEmpty() ? dash() : piv.managementAlgorithm;
    if (piv.managementDefault && !piv.managementAlgorithm.isEmpty())
        management += QStringLiteral(", Werkseinstellung");
    m_pivManagement->setText(management);
    showBio(!piv.biometrics.isEmpty());
    m_pivBio->setText(piv.biometrics.isEmpty() ? dash() : pivBio(piv.biometrics));

    const QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_pivSlots->setRowCount(piv.entries.size());
    for (int row = 0; row < piv.entries.size(); ++row) {
        const PivSlotInfo &slot = piv.entries.at(row);
        QString subject = slot.subject;
        if (subject.isEmpty() && !slot.keyType.isEmpty())
            subject = QStringLiteral("Schlüssel %1").arg(slot.keyType);
        const auto selectable = [](const QString &text) {
            QTableWidgetItem *item = cell(text);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            return item;
        };
        auto *subjectItem = selectable(subject.isEmpty() ? dash() : subject);
        subjectItem->setFont(fixed);
        auto *slotItem = selectable(pivSlotTitle(slot));
        slotItem->setData(Qt::UserRole, slot.slot);
        slotItem->setData(Qt::UserRole + 1, !slot.subject.isEmpty());
        slotItem->setData(Qt::UserRole + 2, !slot.subject.isEmpty() || !slot.keyType.isEmpty());
        slotItem->setData(Qt::UserRole + 3, slot.keyType);
        m_pivSlots->setItem(row, 0, slotItem);
        m_pivSlots->setItem(row, 1, subjectItem);
        m_pivSlots->setItem(row, 2, selectable(slot.expires.isEmpty() ? dash() : slot.expires));
    }

    showNote(m_pivNote, [&]() {
        QStringList lines;
        if (piv.resetBlocked)
            lines << QStringLiteral("Der PIV-Reset ist gesperrt, solange FIDO eingerichtet ist.");
        if (!piv.biometrics.isEmpty())
            lines << QStringLiteral(
                "Die PIV-Biometrie zeigt ykman nur an. Anlegen und Löschen bietet dieser Befehl nicht.");
        return lines.join(QLatin1Char(' '));
    }());
    m_pivPinReady = piv.pinAttempts > 0;
    m_pivPukReady = piv.pukAttempts > 0;
    m_pivUnblockReady = piv.pinAttempts == 0 && piv.pukAttempts > 0;
    m_pivWriteReady = piv.pinAttempts > 0;
    m_pivManageReady = true;
    m_pivResetReady = !piv.resetBlocked;
    applyPivActions(m_client->busy());
}

void MainWindow::changePivClicked(int actionValue)
{
    if (m_client->busy() || m_selectedSerial.isEmpty())
        return;
    const auto action = PivAction(actionValue);
    const PivRecord piv = m_client->snapshot().piv;
    const bool usePuk = action != PivAction::ChangePin;
    const int remaining = usePuk ? piv.pukAttempts : piv.pinAttempts;
    const int total = usePuk ? piv.pukTotal : piv.pinTotal;
    if (action == PivAction::UnblockPin) {
        if (piv.pinAttempts != 0 || piv.pukAttempts < 1)
            return;
    } else if (remaining < 1) {
        return;
    }

    QString title = QStringLiteral("PIV-PIN ändern");
    QString currentLabel = QStringLiteral("Bisherige PIN");
    QString nextLabel = QStringLiteral("Neue PIN");
    QString accept = QStringLiteral("Setzen");
    QString noun = QStringLiteral("PIN");
    if (action == PivAction::ChangePuk) {
        title = QStringLiteral("PIV-PUK ändern");
        currentLabel = QStringLiteral("Bisheriger PUK");
        nextLabel = QStringLiteral("Neuer PUK");
        noun = QStringLiteral("PUK");
    } else if (action == PivAction::UnblockPin) {
        title = QStringLiteral("PIN entsperren");
        currentLabel = QStringLiteral("PUK");
        nextLabel = QStringLiteral("Neue PIN");
        accept = QStringLiteral("Entsperren");
        noun = QStringLiteral("PUK");
    }
    const QString counted = noun == QLatin1String("PUK") ? QStringLiteral("den PUK") : QStringLiteral("die PIN");
    QString info = total > 0 ? QStringLiteral("Noch %1 von %2 Versuchen für %3.").arg(remaining).arg(total).arg(counted)
                             : QStringLiteral("Noch %1 Versuche für %2.").arg(remaining).arg(counted);
    if (remaining == 1)
        info += noun == QLatin1String("PUK")
                    ? QStringLiteral(" Der nächste Fehlversuch sperrt den PUK.")
                    : QStringLiteral(" Der nächste Fehlversuch sperrt die PIN.");
    info += QStringLiteral(" Der Wert geht an die Konsole von ykman und steht nicht in der Befehlszeile.");

    PivChoice choice = askPivSecret(this, title, info, currentLabel, nextLabel, accept);
    if (!choice.accepted) {
        wipeSecret(choice.current);
        wipeSecret(choice.next);
        return;
    }
    PivRequest request;
    request.action = action;
    request.current = choice.current;
    request.next = choice.next;
    wipeSecret(choice.current);
    wipeSecret(choice.next);
    setBusy(true);
    if (action == PivAction::ChangePuk)
        statusBar()->showMessage(QStringLiteral("Der PUK wird geändert."));
    else if (action == PivAction::UnblockPin)
        statusBar()->showMessage(QStringLiteral("Die PIN wird entsperrt."));
    else
        statusBar()->showMessage(QStringLiteral("Die PIN wird geändert."));
    m_client->runPiv(m_selectedSerial, request);
    wipeSecret(request.current);
    wipeSecret(request.next);
}

void MainWindow::finishPiv(bool ok, int action, const QString &detail)
{
    const auto kind = PivAction(action);
    QString revealed;
    if (ok && kind == PivAction::ChangeManagement) {
        const QRegularExpressionMatch key =
            QRegularExpression(QStringLiteral("Management key set:\\s*([0-9a-fA-F]+)")).match(detail);
        if (key.hasMatch())
            revealed = key.captured(1);
    }
    if (!revealed.isEmpty()) {
        showGeneratedSecret(this, QStringLiteral("Management-Key\n%1").arg(revealed));
        wipeSecret(revealed);
    }
    if (!ok)
        m_pendingStatus = pivFailureText(detail);
    else if (kind == PivAction::ChangePuk)
        m_pendingStatus = QStringLiteral("Der PIV-PUK wurde geändert.");
    else if (kind == PivAction::UnblockPin)
        m_pendingStatus = QStringLiteral("Die PIN wurde mit dem PUK entsperrt.");
    else if (kind == PivAction::GenerateKey)
        m_pendingStatus = detail == QLatin1String("ssh")
                              ? QStringLiteral(
                                    "Der Schlüssel wurde erzeugt. Die Datei ist der öffentliche OpenSSH-Schlüssel. "
                                    "Der private Schlüssel bleibt auf dem YubiKey.")
                              : QStringLiteral(
                                    "Der Schlüssel wurde erzeugt. Der öffentliche Schlüssel liegt in der gewählten Datei.");
    else if (kind == PivAction::ExportKey)
        m_pendingStatus = detail == QLatin1String("ssh")
                              ? QStringLiteral("Der öffentliche OpenSSH-Schlüssel wurde geschrieben.")
                              : QStringLiteral("Der öffentliche Schlüssel wurde geschrieben.");
    else if (kind == PivAction::ExportCert)
        m_pendingStatus = QStringLiteral("Das Zertifikat wurde geschrieben.");
    else if (kind == PivAction::ImportKey)
        m_pendingStatus = QStringLiteral(
            "Der private Schlüssel wurde in den Slot gelegt. Die Datei auf dem Rechner bleibt liegen.");
    else if (kind == PivAction::RequestCert)
        m_pendingStatus = QStringLiteral(
            "Die Zertifikatsanfrage wurde geschrieben. Der private Schlüssel bleibt auf dem YubiKey.");
    else if (kind == PivAction::ImportCert)
        m_pendingStatus = QStringLiteral("Das Zertifikat wurde importiert.");
    else if (kind == PivAction::GenerateCert)
        m_pendingStatus = QStringLiteral("Das Zertifikat wurde ausgestellt.");
    else if (kind == PivAction::DeleteCert)
        m_pendingStatus = QStringLiteral("Das Zertifikat wurde gelöscht.");
    else if (kind == PivAction::ChangeManagement)
        m_pendingStatus = QStringLiteral("Der Management-Key wurde geändert.");
    else if (kind == PivAction::Reset)
        m_pendingStatus = QStringLiteral("PIV wurde zurückgesetzt.");
    else
        m_pendingStatus = QStringLiteral("Die PIV-PIN wurde geändert.");
    refresh();
}

namespace {

bool pivSigns(const QString &keyType)
{
    const QString id = keyType.toLower();
    return !id.contains(QLatin1String("x25519")) && !id.contains(QLatin1String("ml-kem"))
           && !id.contains(QLatin1String("mlkem"));
}

bool privateKeyNeedsPassword(const QString &path)
{
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly))
        return true;
    const QByteArray head = input.read(400);
    if (!head.contains("-----BEGIN"))
        return true;
    return head.contains("ENCRYPTED");
}

bool publicPemOk(const QString &path, QString *problem)
{
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) {
        *problem = QStringLiteral("Die Datei mit dem öffentlichen Schlüssel lässt sich nicht lesen.");
        return false;
    }
    const QByteArray head = input.read(256);
    if (head.contains("PRIVATE KEY") || head.contains("ENCRYPTED")) {
        *problem = QStringLiteral(
            "Das ist ein privater Schlüssel. Die Anfrage braucht die öffentliche PEM-Datei.");
        return false;
    }
    if (!head.contains("-----BEGIN") || !head.contains("PUBLIC KEY")) {
        *problem = QStringLiteral("Die Datei ist keine öffentliche PEM.");
        return false;
    }
    return true;
}

bool sameFile(const QString &left, const QString &right)
{
    return QFileInfo(left).absoluteFilePath().compare(QFileInfo(right).absoluteFilePath(),
                                                      Qt::CaseInsensitive)
           == 0;
}

bool pivSlotOk(const QString &slot)
{
    const QString text = slot.trimmed().toLower();
    if (text == QLatin1String("9a") || text == QLatin1String("9c") || text == QLatin1String("9d")
        || text == QLatin1String("9e") || text == QLatin1String("f9"))
        return true;
    if (text.size() != 2)
        return false;
    bool ok = false;
    const int value = text.toInt(&ok, 16);
    return ok && value >= 0x82 && value <= 0x95;
}

bool hexKeyOk(const QString &text)
{
    return QRegularExpression(QStringLiteral("^[0-9a-fA-F]{32}$|^[0-9a-fA-F]{48}$|^[0-9a-fA-F]{64}$"))
        .match(text)
        .hasMatch();
}

bool hexKeyOk(const QString &text, const QString &algorithm)
{
    int expected = 0;
    if (algorithm == QLatin1String("aes128"))
        expected = 32;
    else if (algorithm == QLatin1String("aes256"))
        expected = 64;
    else if (algorithm == QLatin1String("aes192") || algorithm == QLatin1String("tdes"))
        expected = 48;
    if (expected == 0)
        return hexKeyOk(text);
    return text.size() == expected
           && QRegularExpression(QStringLiteral("^[0-9a-fA-F]+$")).match(text).hasMatch();
}

const PivSlotInfo *pivEntry(const PivRecord &piv, const QString &slot)
{
    for (const PivSlotInfo &entry : piv.entries) {
        if (entry.slot.compare(slot, Qt::CaseInsensitive) == 0)
            return &entry;
    }
    return nullptr;
}

QComboBox *pivSlotBox(const QString &selected)
{
    auto *box = new QComboBox;
    box->addItem(QStringLiteral("9a Authentisierung"), QStringLiteral("9a"));
    box->addItem(QStringLiteral("9c Signatur"), QStringLiteral("9c"));
    box->addItem(QStringLiteral("9d Schlüsselverwaltung"), QStringLiteral("9d"));
    box->addItem(QStringLiteral("9e Kartenauthentisierung"), QStringLiteral("9e"));
    const QString id = selected.toLower();
    if (!id.isEmpty() && box->findData(id) < 0)
        box->insertItem(0, id, id);
    const int index = box->findData(id.isEmpty() ? QStringLiteral("9a") : id);
    if (index >= 0)
        box->setCurrentIndex(index);
    return box;
}

struct AuthWidgets
{
    QLineEdit *pin = nullptr;
    QCheckBox *stored = nullptr;
    QLineEdit *currentKey = nullptr;
    QLineEdit *serial = nullptr;
};

AuthWidgets addAuth(QFormLayout *form, QVBoxLayout *layout, const PivRecord &piv)
{
    AuthWidgets widgets;
    widgets.pin = new QLineEdit;
    widgets.pin->setEchoMode(QLineEdit::Password);
    form->addRow(QStringLiteral("PIN"), widgets.pin);
    if (!piv.managementDefault) {
        widgets.stored = new QCheckBox(QStringLiteral(
            "Der Key liegt auf dem YubiKey und ist mit der PIN geschützt"));
        layout->addWidget(widgets.stored);
        widgets.currentKey = new QLineEdit;
        widgets.currentKey->setEchoMode(QLineEdit::Password);
        form->addRow(QStringLiteral("Bisheriger Management-Key"), widgets.currentKey);
        QObject::connect(widgets.stored, &QCheckBox::toggled, widgets.currentKey, [widgets](bool on) {
            widgets.currentKey->setEnabled(!on);
        });
    }
    widgets.serial = new QLineEdit;
    form->addRow(QStringLiteral("Seriennummer"), widgets.serial);
    widgets.serial->hide();
    if (QWidget *label = form->labelForField(widgets.serial))
        label->hide();
    return widgets;
}

void revealSerial(QFormLayout *form, AuthWidgets widgets)
{
    widgets.serial->show();
    if (QWidget *label = form->labelForField(widgets.serial))
        label->show();
}

void wipeAuth(AuthWidgets widgets)
{
    wipeEdit(widgets.pin);
    if (widgets.currentKey)
        wipeEdit(widgets.currentKey);
    if (widgets.serial)
        wipeEdit(widgets.serial);
}

bool takeAuth(AuthWidgets widgets, const PivRecord &piv, bool requireSerial, const QString &deviceSerial,
              QLabel *problem, PivRequest &request)
{
    if (!pivLengthOk(widgets.pin->text())) {
        problem->setText(QStringLiteral("Die PIN braucht 6 bis 8 Zeichen."));
        return false;
    }
    const bool stored = widgets.stored && widgets.stored->isChecked();
    if (!piv.managementDefault && !stored && !hexKeyOk(widgets.currentKey->text())) {
        problem->setText(QStringLiteral("Der Management-Key braucht 32, 48 oder 64 Hex-Zeichen."));
        return false;
    }
    if (requireSerial && widgets.serial->text().trimmed() != deviceSerial) {
        problem->setText(QStringLiteral("Die Seriennummer stimmt nicht."));
        return false;
    }
    request.pin = widgets.pin->text();
    request.managementDefault = piv.managementDefault;
    request.managementStored = stored;
    if (!stored && widgets.currentKey)
        request.managementKey = widgets.currentKey->text();
    return true;
}

QString attemptSentence(const PivRecord &piv)
{
    return piv.pinTotal > 0
               ? QStringLiteral("Noch %1 von %2 PIN-Versuchen. Ein Fehlversuch verbraucht einen.")
                     .arg(piv.pinAttempts)
                     .arg(piv.pinTotal)
               : QStringLiteral("Noch %1 PIN-Versuche. Ein Fehlversuch verbraucht einen.").arg(piv.pinAttempts);
}

void wipePiv(PivRequest &request)
{
    wipeSecret(request.current);
    wipeSecret(request.next);
    wipeSecret(request.pin);
    wipeSecret(request.managementKey);
    wipeSecret(request.newManagementKey);
    wipeSecret(request.filePassword);
}

} // namespace

QString MainWindow::selectedPivSlot() const
{
    if (!m_pivSlots || !m_pivSlots->selectionModel())
        return {};
    const QModelIndexList rows = m_pivSlots->selectionModel()->selectedRows();
    if (rows.isEmpty())
        return {};
    const QTableWidgetItem *item = m_pivSlots->item(rows.first().row(), 0);
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void MainWindow::startPiv(PivRequest request, const QString &status)
{
    setBusy(true);
    statusBar()->showMessage(status);
    m_client->runPiv(m_selectedSerial, request);
    wipePiv(request);
}

void MainWindow::generatePivKeyClicked()
{
    if (!m_pivWriteReady || m_client->busy())
        return;
    const PivRecord piv = m_client->snapshot().piv;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Schlüssel erzeugen"));
    dialog.setMinimumWidth(480);
    auto *layout = new QVBoxLayout(&dialog);
    auto *info = new QLabel(attemptSentence(piv) + QStringLiteral(
        " Die PIN geht an die Konsole von ykman. Ein belegter Slot wird ersetzt und verlangt die Seriennummer. "
        "Der private Schlüssel bleibt auf dem YubiKey. Die Datei ist nur der öffentliche Schlüssel. "
        "PIV legt keine Stub-Datei an."));
    info->setWordWrap(true);
    layout->addWidget(info);
    auto *slot = pivSlotBox(selectedPivSlot());
    auto *algorithm = new QComboBox;
    algorithm->addItem(QStringLiteral("ECC P-256"), QStringLiteral("eccp256"));
    algorithm->addItem(QStringLiteral("ECC P-384"), QStringLiteral("eccp384"));
    algorithm->addItem(QStringLiteral("RSA 2048"), QStringLiteral("rsa2048"));
    algorithm->addItem(QStringLiteral("RSA 3072"), QStringLiteral("rsa3072"));
    algorithm->addItem(QStringLiteral("RSA 4096"), QStringLiteral("rsa4096"));
    algorithm->addItem(QStringLiteral("Ed25519"), QStringLiteral("ed25519"));
    algorithm->addItem(QStringLiteral("X25519"), QStringLiteral("x25519"));
    auto *pinPolicy = new QComboBox;
    pinPolicy->addItem(QStringLiteral("Voreinstellung"), QStringLiteral("default"));
    pinPolicy->addItem(QStringLiteral("Einmal je Sitzung"), QStringLiteral("once"));
    pinPolicy->addItem(QStringLiteral("Jedes Mal"), QStringLiteral("always"));
    pinPolicy->addItem(QStringLiteral("Nie"), QStringLiteral("never"));
    auto *touchPolicy = new QComboBox;
    touchPolicy->addItem(QStringLiteral("Voreinstellung"), QStringLiteral("default"));
    touchPolicy->addItem(QStringLiteral("Nie"), QStringLiteral("never"));
    touchPolicy->addItem(QStringLiteral("Immer"), QStringLiteral("always"));
    touchPolicy->addItem(QStringLiteral("Kurz merken"), QStringLiteral("cached"));
    auto *format = new QComboBox;
    format->addItem(QStringLiteral("PEM"), QStringLiteral("pem"));
    format->addItem(QStringLiteral("OpenSSH"), QStringLiteral("ssh"));
    if (Settings::resolveSshKeygen().isEmpty())
        format->setItemData(1, 0, Qt::UserRole - 1);
    auto *comment = new QLineEdit;
    comment->setPlaceholderText(QStringLiteral("optional, nur bei OpenSSH"));
    auto *path = new QLineEdit;
    path->setReadOnly(true);
    auto *browse = new QPushButton(QStringLiteral("Datei…"));
    QObject::connect(browse, &QPushButton::clicked, &dialog, [this, path, format]() {
        const bool ssh = format->currentData().toString() == QLatin1String("ssh");
        const QString file = QFileDialog::getSaveFileName(
            this, QStringLiteral("Öffentlichen Schlüssel speichern"), QString(),
            ssh ? QStringLiteral("OpenSSH (*.pub)") : QStringLiteral("PEM (*.pem)"));
        if (!file.isEmpty())
            path->setText(file);
    });
    auto *fileRow = new QHBoxLayout;
    fileRow->addWidget(path, 1);
    fileRow->addWidget(browse);
    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Slot"), slot);
    form->addRow(QStringLiteral("Algorithmus"), algorithm);
    form->addRow(QStringLiteral("PIN-Regel"), pinPolicy);
    form->addRow(QStringLiteral("Berührung"), touchPolicy);
    form->addRow(QStringLiteral("Format"), format);
    form->addRow(QStringLiteral("Kommentar"), comment);
    form->addRow(QStringLiteral("Öffentlicher Schlüssel"), fileRow);
    layout->addLayout(form);
    AuthWidgets auth = addAuth(form, layout, piv);
    auto *problem = new QLabel;
    problem->setWordWrap(true);
    layout->addWidget(problem);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Erzeugen"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Abbrechen"));
    layout->addWidget(buttons);
    PivRequest request;
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        const QString id = slot->currentData().toString();
        if (!pivSlotOk(id)) {
            problem->setText(QStringLiteral("Der Slot ist ungültig."));
            return;
        }
        if (path->text().isEmpty()) {
            problem->setText(QStringLiteral("Die Datei für den öffentlichen Schlüssel fehlt."));
            return;
        }
        const bool ssh = format->currentData().toString() == QLatin1String("ssh");
        if (ssh && Settings::resolveSshKeygen().isEmpty()) {
            problem->setText(Settings::missingSshKeygenText());
            return;
        }
        if (ssh && algorithm->currentData().toString() == QLatin1String("x25519")) {
            problem->setText(QStringLiteral("X25519 ist kein SSH-Anmeldeschlüssel."));
            return;
        }
        if (comment->text().contains(QLatin1Char('\n')) || comment->text().contains(QLatin1Char('\r'))) {
            problem->setText(QStringLiteral("Der Kommentar enthält einen Zeilenumbruch."));
            return;
        }
        const PivSlotInfo *entry = pivEntry(piv, id);
        const bool occupied = entry && (!entry->keyType.isEmpty() || !entry->subject.isEmpty());
        if (occupied)
            revealSerial(form, auth);
        if (!takeAuth(auth, piv, occupied, m_selectedSerial, problem, request))
            return;
        request.action = PivAction::GenerateKey;
        request.slot = id;
        request.algorithm = algorithm->currentData().toString();
        request.pinPolicy = pinPolicy->currentData().toString();
        request.touchPolicy = touchPolicy->currentData().toString();
        request.outputPath = path->text();
        request.format = format->currentData().toString();
        request.comment = comment->text().trimmed();
        dialog.accept();
    });
    const bool accepted = dialog.exec() == QDialog::Accepted;
    wipeAuth(auth);
    if (!accepted) {
        wipePiv(request);
        return;
    }
    startPiv(request, QStringLiteral("Der Schlüssel wird erzeugt. Falls der Key blinkt, jetzt berühren."));
}

void MainWindow::exportPivPublicClicked()
{
    if (m_client->busy() || !m_pivSlots || !m_pivSlots->selectionModel())
        return;
    const QModelIndexList rows = m_pivSlots->selectionModel()->selectedRows();
    if (rows.isEmpty())
        return;
    const QTableWidgetItem *item = m_pivSlots->item(rows.first().row(), 0);
    if (!item || !item->data(Qt::UserRole + 2).toBool())
        return;
    const QString id = item->data(Qt::UserRole).toString();
    const QString keyType = item->data(Qt::UserRole + 3).toString();
    const bool sshAvailable = !Settings::resolveSshKeygen().isEmpty();
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Öffentlichen Schlüssel exportieren"));
    dialog.setMinimumWidth(480);
    auto *layout = new QVBoxLayout(&dialog);
    auto *info = new QLabel(QStringLiteral(
        "Exportiert wird nur der öffentliche Schlüssel. Der private Schlüssel bleibt auf dem YubiKey. "
        "PIV legt keine Stub-Datei an. OpenSSH spricht den Slot später über PKCS#11 an."));
    info->setWordWrap(true);
    layout->addWidget(info);
    auto *format = new QComboBox;
    format->addItem(QStringLiteral("PEM"), QStringLiteral("pem"));
    format->addItem(QStringLiteral("OpenSSH"), QStringLiteral("ssh"));
    if (!sshAvailable)
        format->setItemData(1, 0, Qt::UserRole - 1);
    auto *comment = new QLineEdit;
    comment->setPlaceholderText(QStringLiteral("optional, nur bei OpenSSH"));
    auto *path = new QLineEdit;
    path->setReadOnly(true);
    auto *browse = new QPushButton(QStringLiteral("Datei…"));
    QObject::connect(browse, &QPushButton::clicked, &dialog, [this, path, format]() {
        const bool ssh = format->currentData().toString() == QLatin1String("ssh");
        const QString file = QFileDialog::getSaveFileName(
            this, QStringLiteral("Öffentlichen Schlüssel speichern"), QString(),
            ssh ? QStringLiteral("OpenSSH (*.pub)") : QStringLiteral("PEM (*.pem)"));
        if (!file.isEmpty())
            path->setText(file);
    });
    auto *fileRow = new QHBoxLayout;
    fileRow->addWidget(path, 1);
    fileRow->addWidget(browse);
    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Format"), format);
    form->addRow(QStringLiteral("Kommentar"), comment);
    form->addRow(QStringLiteral("Datei"), fileRow);
    layout->addLayout(form);
    auto *problem = new QLabel;
    problem->setWordWrap(true);
    layout->addWidget(problem);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Exportieren"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Abbrechen"));
    layout->addWidget(buttons);
    PivRequest request;
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        const bool ssh = format->currentData().toString() == QLatin1String("ssh");
        if (!pivSlotOk(id)) {
            problem->setText(QStringLiteral("Der Slot ist ungültig."));
            return;
        }
        if (path->text().isEmpty()) {
            problem->setText(QStringLiteral("Die Datei für den öffentlichen Schlüssel fehlt."));
            return;
        }
        if (ssh && !sshAvailable) {
            problem->setText(Settings::missingSshKeygenText());
            return;
        }
        if (ssh && !opensshKeyTypeOk(keyType)) {
            problem->setText(QStringLiteral("Dieser Schlüssel ist kein SSH-Anmeldeschlüssel."));
            return;
        }
        if (comment->text().contains(QLatin1Char('\n')) || comment->text().contains(QLatin1Char('\r'))) {
            problem->setText(QStringLiteral("Der Kommentar enthält einen Zeilenumbruch."));
            return;
        }
        request.action = PivAction::ExportKey;
        request.slot = id;
        request.outputPath = path->text();
        request.format = ssh ? QStringLiteral("ssh") : QStringLiteral("pem");
        request.comment = comment->text().trimmed();
        dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted)
        return;
    startPiv(request, QStringLiteral("Der öffentliche Schlüssel wird geschrieben."));
}

void MainWindow::exportPivCertClicked()
{
    if (m_client->busy() || !m_pivSlots || !m_pivSlots->selectionModel())
        return;
    const QModelIndexList rows = m_pivSlots->selectionModel()->selectedRows();
    if (rows.isEmpty())
        return;
    const QTableWidgetItem *item = m_pivSlots->item(rows.first().row(), 0);
    if (!item || !item->data(Qt::UserRole + 1).toBool())
        return;
    const QString id = item->data(Qt::UserRole).toString();
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Zertifikat exportieren"));
    dialog.setMinimumWidth(480);
    auto *layout = new QVBoxLayout(&dialog);
    auto *info = new QLabel(QStringLiteral(
        "Exportiert wird das Zertifikat aus dem Slot. Der private Schlüssel bleibt auf dem YubiKey."));
    info->setWordWrap(true);
    layout->addWidget(info);
    auto *format = new QComboBox;
    format->addItem(QStringLiteral("PEM"), QStringLiteral("pem"));
    format->addItem(QStringLiteral("DER"), QStringLiteral("der"));
    auto *path = new QLineEdit;
    path->setReadOnly(true);
    auto *browse = new QPushButton(QStringLiteral("Datei…"));
    QObject::connect(browse, &QPushButton::clicked, &dialog, [this, path, format]() {
        const bool der = format->currentData().toString() == QLatin1String("der");
        const QString file = QFileDialog::getSaveFileName(
            this, QStringLiteral("Zertifikat speichern"), QString(),
            der ? QStringLiteral("DER (*.der *.cer)") : QStringLiteral("PEM (*.pem *.crt)"));
        if (!file.isEmpty())
            path->setText(file);
    });
    auto *fileRow = new QHBoxLayout;
    fileRow->addWidget(path, 1);
    fileRow->addWidget(browse);
    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Format"), format);
    form->addRow(QStringLiteral("Datei"), fileRow);
    layout->addLayout(form);
    auto *problem = new QLabel;
    problem->setWordWrap(true);
    layout->addWidget(problem);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Exportieren"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Abbrechen"));
    layout->addWidget(buttons);
    PivRequest request;
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (!pivSlotOk(id)) {
            problem->setText(QStringLiteral("Der Slot ist ungültig."));
            return;
        }
        if (path->text().isEmpty()) {
            problem->setText(QStringLiteral("Die Datei für das Zertifikat fehlt."));
            return;
        }
        request.action = PivAction::ExportCert;
        request.slot = id;
        request.outputPath = path->text();
        request.format = format->currentData().toString();
        dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted)
        return;
    startPiv(request, QStringLiteral("Das Zertifikat wird geschrieben."));
}

void MainWindow::requestPivCertClicked()
{
    if (!m_pivWriteReady || m_client->busy())
        return;
    const PivRecord piv = m_client->snapshot().piv;
    const QString selected = selectedPivSlot();
    const PivSlotInfo *selectedEntry = pivEntry(piv, selected);
    if (!selectedEntry || selectedEntry->keyType.isEmpty())
        return;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Zertifikatsanfrage"));
    dialog.setMinimumWidth(520);
    auto *layout = new QVBoxLayout(&dialog);
    auto *info = new QLabel(attemptSentence(piv) + QStringLiteral(
        " Die Anfrage wird mit dem Schlüssel im Slot unterschrieben. "
        "Die öffentliche PEM kommt aus „Schlüssel erzeugen“ oder „Öffentlichen Schlüssel“. "
        "X25519 kann das nicht. Die PIN geht an die Konsole von ykman. "
        "Der private Schlüssel bleibt auf dem YubiKey."));
    info->setWordWrap(true);
    layout->addWidget(info);
    auto *slot = pivSlotBox(selected);
    auto *subject = new QLineEdit;
    auto *publicKey = new QLineEdit;
    publicKey->setReadOnly(true);
    auto *publicBrowse = new QPushButton(QStringLiteral("Datei…"));
    QObject::connect(publicBrowse, &QPushButton::clicked, &dialog, [this, publicKey]() {
        const QString file = QFileDialog::getOpenFileName(
            this, QStringLiteral("Öffentlichen Schlüssel wählen"), QString(),
            QStringLiteral("PEM (*.pem)"));
        if (!file.isEmpty())
            publicKey->setText(file);
    });
    auto *publicRow = new QHBoxLayout;
    publicRow->addWidget(publicKey, 1);
    publicRow->addWidget(publicBrowse);
    auto *csr = new QLineEdit;
    csr->setReadOnly(true);
    auto *csrBrowse = new QPushButton(QStringLiteral("Datei…"));
    QObject::connect(csrBrowse, &QPushButton::clicked, &dialog, [this, csr]() {
        const QString file = QFileDialog::getSaveFileName(
            this, QStringLiteral("Zertifikatsanfrage speichern"), QString(),
            QStringLiteral("CSR (*.csr *.pem)"));
        if (!file.isEmpty())
            csr->setText(file);
    });
    auto *csrRow = new QHBoxLayout;
    csrRow->addWidget(csr, 1);
    csrRow->addWidget(csrBrowse);
    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Slot"), slot);
    form->addRow(QStringLiteral("Name"), subject);
    form->addRow(QStringLiteral("Öffentlicher Schlüssel"), publicRow);
    form->addRow(QStringLiteral("Anfrage"), csrRow);
    layout->addLayout(form);
    auto *pin = new QLineEdit;
    pin->setEchoMode(QLineEdit::Password);
    form->addRow(QStringLiteral("PIN"), pin);
    auto *problem = new QLabel;
    problem->setWordWrap(true);
    layout->addWidget(problem);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Anfrage schreiben"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Abbrechen"));
    layout->addWidget(buttons);
    PivRequest request;
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        const QString id = slot->currentData().toString();
        const PivSlotInfo *entry = pivEntry(piv, id);
        if (!pivSlotOk(id) || !entry || entry->keyType.isEmpty()) {
            problem->setText(QStringLiteral("In diesem Slot liegt kein Schlüssel."));
            return;
        }
        if (!pivSigns(entry->keyType)) {
            problem->setText(QStringLiteral("Dieser Schlüssel kann kein Zertifikat unterschreiben."));
            return;
        }
        const QString name = subject->text().trimmed();
        if (name.isEmpty() || name.contains(QLatin1Char('\n')) || name.contains(QLatin1Char('\r'))) {
            problem->setText(QStringLiteral("Der Name fehlt."));
            return;
        }
        QString why;
        if (!publicPemOk(publicKey->text(), &why)) {
            problem->setText(why);
            return;
        }
        if (csr->text().isEmpty()) {
            problem->setText(QStringLiteral("Die Datei für die Anfrage fehlt."));
            return;
        }
        if (sameFile(publicKey->text(), csr->text())) {
            problem->setText(QStringLiteral("Die Anfrage darf die öffentliche PEM nicht überschreiben."));
            return;
        }
        if (!pivLengthOk(pin->text())) {
            problem->setText(QStringLiteral("Die PIN braucht 6 bis 8 Zeichen."));
            return;
        }
        request.action = PivAction::RequestCert;
        request.slot = id;
        request.subject = name.contains(QLatin1Char('=')) ? name : QStringLiteral("CN=%1").arg(name);
        request.inputPath = publicKey->text();
        request.outputPath = csr->text();
        request.pin = pin->text();
        dialog.accept();
    });
    const bool accepted = dialog.exec() == QDialog::Accepted;
    wipeEdit(pin);
    if (!accepted) {
        wipePiv(request);
        return;
    }
    startPiv(request, QStringLiteral("Die Zertifikatsanfrage wird geschrieben. Falls der Key blinkt, jetzt berühren."));
}

void MainWindow::importPivKeyClicked()
{
    if (!m_pivWriteReady || m_client->busy())
        return;
    const PivRecord piv = m_client->snapshot().piv;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Schlüssel importieren"));
    dialog.setMinimumWidth(520);
    auto *layout = new QVBoxLayout(&dialog);
    auto *info = new QLabel(attemptSentence(piv) + QStringLiteral(
        " Der private Schlüssel kommt aus einer Datei und bleibt in dieser Datei liegen. "
        "Die PIN geht an die Konsole von ykman. Ein belegter Slot wird ersetzt und verlangt die Seriennummer. "
        "Das Zertifikat im Slot bleibt liegen."));
    info->setWordWrap(true);
    layout->addWidget(info);
    auto *slot = pivSlotBox(selectedPivSlot());
    auto *path = new QLineEdit;
    path->setReadOnly(true);
    auto *password = new QLineEdit;
    password->setEchoMode(QLineEdit::Password);
    password->setEnabled(false);
    auto *browse = new QPushButton(QStringLiteral("Datei…"));
    QObject::connect(browse, &QPushButton::clicked, &dialog, [this, path, password]() {
        const QString file = QFileDialog::getOpenFileName(
            this, QStringLiteral("Privaten Schlüssel wählen"), QString(),
            QStringLiteral("Privater Schlüssel (*.pem *.key *.p12 *.pfx)"));
        if (file.isEmpty())
            return;
        path->setText(file);
        const bool needsPassword = privateKeyNeedsPassword(file);
        password->setEnabled(needsPassword);
        if (!needsPassword)
            password->clear();
    });
    auto *fileRow = new QHBoxLayout;
    fileRow->addWidget(path, 1);
    fileRow->addWidget(browse);
    auto *pinPolicy = new QComboBox;
    pinPolicy->addItem(QStringLiteral("Voreinstellung"), QStringLiteral("default"));
    pinPolicy->addItem(QStringLiteral("Einmal je Sitzung"), QStringLiteral("once"));
    pinPolicy->addItem(QStringLiteral("Jedes Mal"), QStringLiteral("always"));
    pinPolicy->addItem(QStringLiteral("Nie"), QStringLiteral("never"));
    auto *touchPolicy = new QComboBox;
    touchPolicy->addItem(QStringLiteral("Voreinstellung"), QStringLiteral("default"));
    touchPolicy->addItem(QStringLiteral("Nie"), QStringLiteral("never"));
    touchPolicy->addItem(QStringLiteral("Immer"), QStringLiteral("always"));
    touchPolicy->addItem(QStringLiteral("Kurz merken"), QStringLiteral("cached"));
    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Slot"), slot);
    form->addRow(QStringLiteral("Datei"), fileRow);
    form->addRow(QStringLiteral("Dateipasswort"), password);
    form->addRow(QStringLiteral("PIN-Regel"), pinPolicy);
    form->addRow(QStringLiteral("Berührung"), touchPolicy);
    layout->addLayout(form);
    AuthWidgets auth = addAuth(form, layout, piv);
    auto *problem = new QLabel;
    problem->setWordWrap(true);
    layout->addWidget(problem);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Importieren"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Abbrechen"));
    layout->addWidget(buttons);
    PivRequest request;
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        const QString id = slot->currentData().toString();
        if (!pivSlotOk(id) || path->text().isEmpty()) {
            problem->setText(QStringLiteral("Slot und Schlüsseldatei werden gebraucht."));
            return;
        }
        if (password->isEnabled() && password->text().isEmpty()) {
            problem->setText(QStringLiteral("Das Dateipasswort fehlt."));
            return;
        }
        const PivSlotInfo *entry = pivEntry(piv, id);
        const bool occupied = entry && (!entry->keyType.isEmpty() || !entry->subject.isEmpty());
        if (occupied)
            revealSerial(form, auth);
        if (!takeAuth(auth, piv, occupied, m_selectedSerial, problem, request))
            return;
        request.action = PivAction::ImportKey;
        request.slot = id;
        request.outputPath = path->text();
        request.filePassword = password->isEnabled() ? password->text() : QString();
        request.pinPolicy = pinPolicy->currentData().toString();
        request.touchPolicy = touchPolicy->currentData().toString();
        dialog.accept();
    });
    const bool accepted = dialog.exec() == QDialog::Accepted;
    wipeAuth(auth);
    wipeEdit(password);
    if (!accepted) {
        wipePiv(request);
        return;
    }
    startPiv(request, QStringLiteral("Der Schlüssel wird in den Slot gelegt. Falls der Key blinkt, jetzt berühren."));
}

void MainWindow::importPivCertClicked()
{
    if (!m_pivWriteReady || m_client->busy())
        return;
    const PivRecord piv = m_client->snapshot().piv;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Zertifikat importieren"));
    dialog.setMinimumWidth(480);
    auto *layout = new QVBoxLayout(&dialog);
    auto *info = new QLabel(attemptSentence(piv) + QStringLiteral(
        " PEM und DER gehen ohne Dateipasswort. PKCS#12 fragt es zuerst ab. "
        "Ein vorhandenes Zertifikat wird ersetzt und verlangt die Seriennummer."));
    info->setWordWrap(true);
    layout->addWidget(info);
    auto *slot = pivSlotBox(selectedPivSlot());
    auto *path = new QLineEdit;
    path->setReadOnly(true);
    auto *password = new QLineEdit;
    password->setEchoMode(QLineEdit::Password);
    password->setEnabled(false);
    auto *browse = new QPushButton(QStringLiteral("Datei…"));
    QObject::connect(browse, &QPushButton::clicked, &dialog, [this, path, password]() {
        const QString file = QFileDialog::getOpenFileName(
            this, QStringLiteral("Zertifikat wählen"), QString(),
            QStringLiteral("Zertifikat (*.pem *.crt *.cer *.der *.p12 *.pfx)"));
        if (file.isEmpty())
            return;
        path->setText(file);
        QFile input(file);
        const bool pem = input.open(QIODevice::ReadOnly) && input.read(80).contains("-----BEGIN");
        password->setEnabled(!pem);
        if (pem)
            password->clear();
    });
    auto *fileRow = new QHBoxLayout;
    fileRow->addWidget(path, 1);
    fileRow->addWidget(browse);
    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Slot"), slot);
    form->addRow(QStringLiteral("Datei"), fileRow);
    form->addRow(QStringLiteral("Dateipasswort"), password);
    layout->addLayout(form);
    AuthWidgets auth = addAuth(form, layout, piv);
    auto *problem = new QLabel;
    problem->setWordWrap(true);
    layout->addWidget(problem);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Importieren"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Abbrechen"));
    layout->addWidget(buttons);
    PivRequest request;
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        const QString id = slot->currentData().toString();
        if (!pivSlotOk(id) || path->text().isEmpty()) {
            problem->setText(QStringLiteral("Slot und Zertifikatsdatei werden gebraucht."));
            return;
        }
        if (password->isEnabled() && password->text().isEmpty()) {
            problem->setText(QStringLiteral("Das Dateipasswort fehlt."));
            return;
        }
        const PivSlotInfo *entry = pivEntry(piv, id);
        const bool replace = entry && !entry->subject.isEmpty();
        if (replace)
            revealSerial(form, auth);
        if (!takeAuth(auth, piv, replace, m_selectedSerial, problem, request))
            return;
        request.action = PivAction::ImportCert;
        request.slot = id;
        request.outputPath = path->text();
        request.filePassword = password->isEnabled() ? password->text() : QString();
        dialog.accept();
    });
    const bool accepted = dialog.exec() == QDialog::Accepted;
    wipeAuth(auth);
    wipeEdit(password);
    if (!accepted) {
        wipePiv(request);
        return;
    }
    startPiv(request, QStringLiteral("Das Zertifikat wird geschrieben. Falls der Key blinkt, jetzt berühren."));
}

void MainWindow::signPivCertClicked()
{
    if (!m_pivWriteReady || m_client->busy())
        return;
    const PivRecord piv = m_client->snapshot().piv;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Zertifikat ausstellen"));
    dialog.setMinimumWidth(460);
    auto *layout = new QVBoxLayout(&dialog);
    auto *info = new QLabel(attemptSentence(piv) + QStringLiteral(
        " Das Zertifikat wird vom Schlüssel im Slot unterschrieben. X25519 kann das nicht. "
        "Ein vorhandenes Zertifikat wird ersetzt und verlangt die Seriennummer."));
    info->setWordWrap(true);
    layout->addWidget(info);
    auto *slot = pivSlotBox(selectedPivSlot());
    auto *subject = new QLineEdit;
    auto *days = new QSpinBox;
    days->setRange(1, 3650);
    days->setValue(365);
    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Slot"), slot);
    form->addRow(QStringLiteral("Name"), subject);
    form->addRow(QStringLiteral("Tage"), days);
    layout->addLayout(form);
    AuthWidgets auth = addAuth(form, layout, piv);
    auto *problem = new QLabel;
    problem->setWordWrap(true);
    layout->addWidget(problem);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Ausstellen"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Abbrechen"));
    layout->addWidget(buttons);
    PivRequest request;
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        const QString id = slot->currentData().toString();
        const PivSlotInfo *entry = pivEntry(piv, id);
        if (!pivSlotOk(id) || !entry || entry->keyType.isEmpty()) {
            problem->setText(QStringLiteral("In diesem Slot liegt kein Schlüssel."));
            return;
        }
        if (!pivSigns(entry->keyType)) {
            problem->setText(QStringLiteral("Dieser Schlüssel kann kein Zertifikat unterschreiben."));
            return;
        }
        const QString name = subject->text().trimmed();
        if (name.isEmpty()) {
            problem->setText(QStringLiteral("Der Name fehlt."));
            return;
        }
        const bool replace = !entry->subject.isEmpty();
        if (replace)
            revealSerial(form, auth);
        if (!takeAuth(auth, piv, replace, m_selectedSerial, problem, request))
            return;
        request.action = PivAction::GenerateCert;
        request.slot = id;
        request.subject = name.contains(QLatin1Char('=')) ? name : QStringLiteral("CN=%1").arg(name);
        request.validDays = days->value();
        dialog.accept();
    });
    const bool accepted = dialog.exec() == QDialog::Accepted;
    wipeAuth(auth);
    if (!accepted) {
        wipePiv(request);
        return;
    }
    startPiv(request, QStringLiteral("Das Zertifikat wird ausgestellt. Falls der Key blinkt, jetzt berühren."));
}

void MainWindow::deletePivCertClicked()
{
    if (!m_pivWriteReady || m_client->busy())
        return;
    const QString id = selectedPivSlot();
    const PivRecord piv = m_client->snapshot().piv;
    const PivSlotInfo *entry = pivEntry(piv, id);
    if (!entry || entry->subject.isEmpty())
        return;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Zertifikat löschen"));
    dialog.setMinimumWidth(440);
    auto *layout = new QVBoxLayout(&dialog);
    auto *info = new QLabel(attemptSentence(piv) + QStringLiteral(
        " Das Zertifikat wird gelöscht. Der Schlüssel im Slot bleibt. Zum Bestätigen die Seriennummer eintippen."));
    info->setWordWrap(true);
    layout->addWidget(info);
    auto *form = new QFormLayout;
    layout->addLayout(form);
    AuthWidgets auth = addAuth(form, layout, piv);
    revealSerial(form, auth);
    auto *problem = new QLabel;
    problem->setWordWrap(true);
    layout->addWidget(problem);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Zertifikat löschen"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Abbrechen"));
    layout->addWidget(buttons);
    PivRequest request;
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (!takeAuth(auth, piv, true, m_selectedSerial, problem, request))
            return;
        request.action = PivAction::DeleteCert;
        request.slot = id;
        dialog.accept();
    });
    const bool accepted = dialog.exec() == QDialog::Accepted;
    wipeAuth(auth);
    if (!accepted) {
        wipePiv(request);
        return;
    }
    startPiv(request, QStringLiteral("Das Zertifikat wird gelöscht. Falls der Key blinkt, jetzt berühren."));
}

void MainWindow::changePivManagementClicked()
{
    if (!m_pivManageReady || m_client->busy())
        return;
    const PivRecord piv = m_client->snapshot().piv;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Management-Key"));
    dialog.setMinimumWidth(480);
    auto *layout = new QVBoxLayout(&dialog);
    auto *info = new QLabel(QStringLiteral(
        "Der bisherige Key gilt danach nicht mehr. Ein zufälliger Key wird einmal angezeigt. "
        "Er steht nicht in der Befehlszeile. Zum Bestätigen die Seriennummer eintippen."));
    info->setWordWrap(true);
    layout->addWidget(info);
    auto *algorithm = new QComboBox;
    algorithm->addItem(QStringLiteral("AES-128"), QStringLiteral("aes128"));
    algorithm->addItem(QStringLiteral("AES-192"), QStringLiteral("aes192"));
    algorithm->addItem(QStringLiteral("AES-256"), QStringLiteral("aes256"));
    algorithm->addItem(QStringLiteral("3DES"), QStringLiteral("tdes"));
    const QString currentAlgo = piv.managementAlgorithm.toLower().remove(QLatin1Char('-')).remove(QLatin1Char(' '));
    const int algoIndex = algorithm->findData(currentAlgo == QLatin1String("aes192")
                                                  || currentAlgo.contains(QLatin1String("aes192"))
                                              ? QStringLiteral("aes192")
                                              : currentAlgo);
    if (algorithm->findData(piv.managementAlgorithm.toLower()) >= 0)
        algorithm->setCurrentIndex(algorithm->findData(piv.managementAlgorithm.toLower()));
    else if (currentAlgo.contains(QLatin1String("aes128")))
        algorithm->setCurrentIndex(0);
    else if (currentAlgo.contains(QLatin1String("aes256")))
        algorithm->setCurrentIndex(2);
    else if (currentAlgo.contains(QLatin1String("tdes")) || currentAlgo.contains(QLatin1String("3des")))
        algorithm->setCurrentIndex(3);
    else
        algorithm->setCurrentIndex(1);
    Q_UNUSED(algoIndex);
    auto *generate = new QCheckBox(QStringLiteral("Zufällig erzeugen"));
    generate->setChecked(true);
    auto *touch = new QCheckBox(QStringLiteral("Beim Benutzen eine Berührung verlangen"));
    auto *protect = new QCheckBox(QStringLiteral("Auf dem Key speichern, geschützt mit der PIN"));
    auto *next = new QLineEdit;
    next->setEchoMode(QLineEdit::Password);
    next->setEnabled(false);
    QObject::connect(generate, &QCheckBox::toggled, next, [next](bool on) { next->setEnabled(!on); });
    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Algorithmus"), algorithm);
    form->addRow(QStringLiteral("Neuer Key"), next);
    layout->addLayout(form);
    layout->addWidget(generate);
    layout->addWidget(touch);
    layout->addWidget(protect);
    AuthWidgets auth = addAuth(form, layout, piv);
    revealSerial(form, auth);
    auth.pin->setEnabled(false);
    QObject::connect(protect, &QCheckBox::toggled, auth.pin, [auth](bool on) {
        if (!auth.stored || !auth.stored->isChecked())
            auth.pin->setEnabled(on);
    });
    if (auth.stored) {
        QObject::connect(auth.stored, &QCheckBox::toggled, auth.pin, [auth, protect](bool on) {
            auth.pin->setEnabled(on || protect->isChecked());
        });
    }
    auto *problem = new QLabel;
    problem->setWordWrap(true);
    layout->addWidget(problem);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Key setzen"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Abbrechen"));
    layout->addWidget(buttons);
    PivRequest request;
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        const QString algo = algorithm->currentData().toString();
        const bool stored = auth.stored && auth.stored->isChecked();
        if (!generate->isChecked() && !hexKeyOk(next->text(), algo)) {
            problem->setText(QStringLiteral("Der neue Key hat nicht die Länge dieses Algorithmus."));
            return;
        }
        if (!piv.managementDefault && !stored && !hexKeyOk(auth.currentKey->text())) {
            problem->setText(QStringLiteral("Der bisherige Management-Key braucht 32, 48 oder 64 Hex-Zeichen."));
            return;
        }
        if ((stored || protect->isChecked()) && !pivLengthOk(auth.pin->text())) {
            problem->setText(QStringLiteral("Die PIN braucht 6 bis 8 Zeichen."));
            return;
        }
        if (piv.pinAttempts == 0 && (stored || protect->isChecked())) {
            problem->setText(QStringLiteral("Die PIN ist gesperrt."));
            return;
        }
        if (auth.serial->text().trimmed() != m_selectedSerial) {
            problem->setText(QStringLiteral("Die Seriennummer stimmt nicht."));
            return;
        }
        request.action = PivAction::ChangeManagement;
        request.algorithm = algo;
        request.generateManagement = generate->isChecked();
        request.managementTouch = touch->isChecked();
        request.protectManagement = protect->isChecked();
        request.managementDefault = piv.managementDefault;
        request.managementStored = stored;
        if (!generate->isChecked())
            request.newManagementKey = next->text();
        if (!stored && auth.currentKey)
            request.managementKey = auth.currentKey->text();
        if (stored || protect->isChecked())
            request.pin = auth.pin->text();
        dialog.accept();
    });
    const bool accepted = dialog.exec() == QDialog::Accepted;
    wipeAuth(auth);
    wipeEdit(next);
    if (!accepted) {
        wipePiv(request);
        return;
    }
    startPiv(request, QStringLiteral("Der Management-Key wird geändert."));
}

void MainWindow::resetPivClicked()
{
    if (!m_pivResetReady || m_client->busy())
        return;
    if (!confirmSerial(this, m_selectedSerial, QStringLiteral("PIV zurücksetzen"),
                       QStringLiteral("Alle PIV-Schlüssel und Zertifikate werden gelöscht. "
                                      "PIN, PUK und Management-Key kehren auf die Werkseinstellung zurück. "
                                      "OTP, OATH, FIDO und OpenPGP bleiben. "
                                      "Zum Bestätigen die Seriennummer eintippen."),
                       QStringLiteral("Zurücksetzen")))
        return;
    PivRequest request;
    request.action = PivAction::Reset;
    startPiv(request, QStringLiteral("PIV wird zurückgesetzt."));
}

namespace {

bool openPgpSecretOk(const QString &value, int minimum, bool kdf)
{
    if (value.contains(QLatin1Char('\n')) || value.contains(QLatin1Char('\r')))
        return false;
    const int size = value.toUtf8().size();
    if (size < 1 || size > 127)
        return false;
    return kdf || size >= minimum;
}

QString openPgpLengthHint(bool admin, bool kdf)
{
    if (kdf)
        return QStringLiteral("Der Wert darf nicht leer sein und höchstens 127 Zeichen haben.");
    return admin ? QStringLiteral("Admin-PIN und Reset-Code brauchen 8 bis 127 Zeichen.")
                 : QStringLiteral("Die Benutzer-PIN braucht 6 bis 127 Zeichen.");
}

QString openPgpAttemptsText(int remaining)
{
    if (remaining < 0)
        return dash();
    if (remaining == 1)
        return QStringLiteral("1 Versuch");
    return QStringLiteral("%1 Versuche").arg(remaining);
}

QString openPgpTouchLabel(const QString &policy)
{
    if (policy == QLatin1String("Off"))
        return QStringLiteral("Aus");
    if (policy == QLatin1String("On"))
        return QStringLiteral("An");
    if (policy == QLatin1String("Fixed"))
        return QStringLiteral("Fest");
    if (policy == QLatin1String("Cached"))
        return QStringLiteral("Kurz merken");
    if (policy == QLatin1String("Cached Fixed"))
        return QStringLiteral("Fest, kurz merken");
    if (policy.isEmpty() || policy == QLatin1String("N/A") || policy == QLatin1String("Unknown"))
        return dash();
    return policy;
}

QString openPgpKeyLabel(const QString &id, const QString &fallback)
{
    if (id == QLatin1String("sig"))
        return QStringLiteral("Signatur");
    if (id == QLatin1String("dec"))
        return QStringLiteral("Entschlüsselung");
    if (id == QLatin1String("aut"))
        return QStringLiteral("Authentisierung");
    if (id == QLatin1String("att"))
        return QStringLiteral("Beglaubigung");
    return fallback.isEmpty() ? dash() : fallback;
}

QString openPgpSignatureLabel(const QString &policy)
{
    if (policy == QLatin1String("Once"))
        return QStringLiteral("Einmal");
    if (policy == QLatin1String("Always"))
        return QStringLiteral("Immer");
    return policy.isEmpty() ? dash() : policy;
}

QString openPgpAttemptSentence(int remaining, const QString &which)
{
    QString text = remaining == 1 ? QStringLiteral("Noch 1 Versuch für %1.").arg(which)
                                  : QStringLiteral("Noch %1 Versuche für %2.").arg(remaining).arg(which);
    if (remaining == 1)
        text += QStringLiteral(" Der nächste Fehlversuch sperrt diese Eingabe.");
    text += QStringLiteral(" Der Wert geht an die Konsole von ykman und steht nicht in der Befehlszeile.");
    return text;
}

struct OpenPgpSecrets
{
    bool accepted = false;
    QString current;
    QString next;
    QString admin;
};

OpenPgpSecrets askOpenPgpSecrets(QWidget *parent, const QString &title, const QString &info, bool withCurrent,
                                 int nextMinimum, int otherMinimum, bool kdf, const QString &currentLabel,
                                 const QString &nextLabel, const QString &adminLabel, const QString &acceptLabel)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setMinimumWidth(460);
    auto *layout = new QVBoxLayout(&dialog);
    auto *text = new QLabel(info);
    text->setWordWrap(true);
    layout->addWidget(text);
    auto *current = new QLineEdit;
    auto *next = new QLineEdit;
    auto *repeat = new QLineEdit;
    auto *admin = new QLineEdit;
    for (QLineEdit *edit : {current, next, repeat, admin})
        edit->setEchoMode(QLineEdit::Password);
    auto *form = new QFormLayout;
    if (withCurrent)
        form->addRow(currentLabel, current);
    form->addRow(nextLabel, next);
    form->addRow(QStringLiteral("Wiederholen"), repeat);
    if (!withCurrent)
        form->addRow(adminLabel, admin);
    layout->addLayout(form);
    auto *problem = new QLabel;
    problem->setWordWrap(true);
    layout->addWidget(problem);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(acceptLabel);
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Abbrechen"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (withCurrent && current->text().isEmpty()) {
            problem->setText(QStringLiteral("%1 fehlt.").arg(currentLabel));
            return;
        }
        if (next->text().isEmpty()) {
            problem->setText(QStringLiteral("%1 fehlt.").arg(nextLabel));
            return;
        }
        if (!withCurrent && admin->text().isEmpty()) {
            problem->setText(QStringLiteral("%1 fehlt.").arg(adminLabel));
            return;
        }
        if (withCurrent && !openPgpSecretOk(current->text(), otherMinimum, kdf)) {
            problem->setText(openPgpLengthHint(otherMinimum >= 8, kdf));
            return;
        }
        if (!openPgpSecretOk(next->text(), nextMinimum, kdf)) {
            problem->setText(openPgpLengthHint(nextMinimum >= 8, kdf));
            return;
        }
        if (!withCurrent && !openPgpSecretOk(admin->text(), otherMinimum, kdf)) {
            problem->setText(openPgpLengthHint(true, kdf));
            return;
        }
        if (next->text() != repeat->text()) {
            problem->setText(QStringLiteral("Die beiden neuen Werte stimmen nicht überein."));
            return;
        }
        dialog.accept();
    });

    OpenPgpSecrets choice;
    if (dialog.exec() == QDialog::Accepted) {
        choice.accepted = true;
        choice.current = current->text();
        choice.next = next->text();
        choice.admin = admin->text();
    }
    wipeEdit(current);
    wipeEdit(next);
    wipeEdit(repeat);
    wipeEdit(admin);
    return choice;
}

struct OpenPgpPolicyChoice
{
    bool accepted = false;
    QString policy;
    QString admin;
};

OpenPgpPolicyChoice askOpenPgpSignature(QWidget *parent, const QString &current, int adminAttempts, bool kdf)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Signatur-PIN"));
    dialog.setMinimumWidth(460);
    auto *layout = new QVBoxLayout(&dialog);
    auto *text = new QLabel(openPgpAttemptSentence(adminAttempts, QStringLiteral("den Admin-PIN")));
    text->setWordWrap(true);
    layout->addWidget(text);
    auto *policy = new QComboBox;
    policy->addItem(QStringLiteral("Einmal"), QStringLiteral("once"));
    policy->addItem(QStringLiteral("Immer"), QStringLiteral("always"));
    if (current == QLatin1String("Always"))
        policy->setCurrentIndex(1);
    auto *admin = new QLineEdit;
    admin->setEchoMode(QLineEdit::Password);
    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Beim Signieren"), policy);
    form->addRow(QStringLiteral("Admin-PIN"), admin);
    layout->addLayout(form);
    auto *problem = new QLabel;
    problem->setWordWrap(true);
    layout->addWidget(problem);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Richtlinie setzen"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Abbrechen"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (!openPgpSecretOk(admin->text(), 8, kdf)) {
            problem->setText(admin->text().isEmpty() ? QStringLiteral("Der Admin-PIN fehlt.")
                                                     : openPgpLengthHint(true, kdf));
            return;
        }
        dialog.accept();
    });

    OpenPgpPolicyChoice choice;
    if (dialog.exec() == QDialog::Accepted) {
        choice.accepted = true;
        choice.policy = policy->currentData().toString();
        choice.admin = admin->text();
    }
    wipeEdit(admin);
    return choice;
}

struct OpenPgpTouchChoice
{
    bool accepted = false;
    QString key;
    QString policy;
    QString admin;
};

OpenPgpTouchChoice askOpenPgpTouch(QWidget *parent, const OpenPgpRecord &record, const QString &serial,
                                   const QString &selectedKey)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Berührung"));
    dialog.setMinimumWidth(520);
    auto *layout = new QVBoxLayout(&dialog);
    auto *text = new QLabel(openPgpAttemptSentence(record.adminAttempts, QStringLiteral("den Admin-PIN")));
    text->setWordWrap(true);
    layout->addWidget(text);
    auto *key = new QComboBox;
    for (const OpenPgpKey &entry : record.keys)
        key->addItem(openPgpKeyLabel(entry.id, entry.name), entry.id);
    const int selected = key->findData(selectedKey);
    if (selected >= 0)
        key->setCurrentIndex(selected);
    auto *policy = new QComboBox;
    policy->addItem(QStringLiteral("Aus"), QStringLiteral("off"));
    policy->addItem(QStringLiteral("An"), QStringLiteral("on"));
    policy->addItem(QStringLiteral("Kurz merken"), QStringLiteral("cached"));
    policy->addItem(QStringLiteral("Fest"), QStringLiteral("fixed"));
    policy->addItem(QStringLiteral("Fest, kurz merken"), QStringLiteral("cached-fixed"));
    auto *admin = new QLineEdit;
    admin->setEchoMode(QLineEdit::Password);
    auto *serialEdit = new QLineEdit;
    auto *warning = new QLabel(QStringLiteral(
        "Eine feste Berührung lässt sich später nicht wieder lockern. Zum Bestätigen die Seriennummer eintippen."));
    warning->setWordWrap(true);
    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Schlüssel"), key);
    form->addRow(QStringLiteral("Berührung"), policy);
    form->addRow(QStringLiteral("Admin-PIN"), admin);
    form->addRow(QStringLiteral("Seriennummer"), serialEdit);
    layout->addLayout(form);
    layout->addWidget(warning);
    auto *problem = new QLabel;
    problem->setWordWrap(true);
    layout->addWidget(problem);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Berührung setzen"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Abbrechen"));
    layout->addWidget(buttons);
    const auto fixedPolicy = [&]() {
        const QString value = policy->currentData().toString();
        return value == QLatin1String("fixed") || value == QLatin1String("cached-fixed");
    };
    const auto showSerial = [&]() {
        const bool fixed = fixedPolicy();
        serialEdit->setVisible(fixed);
        warning->setVisible(fixed);
        if (QWidget *label = form->labelForField(serialEdit))
            label->setVisible(fixed);
        if (!fixed)
            serialEdit->clear();
    };
    QObject::connect(policy, &QComboBox::currentIndexChanged, &dialog, [&](int) { showSerial(); });
    showSerial();
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (key->currentData().toString().isEmpty()) {
            problem->setText(QStringLiteral("Der Schlüssel fehlt."));
            return;
        }
        if (!openPgpSecretOk(admin->text(), 8, record.kdf)) {
            problem->setText(admin->text().isEmpty() ? QStringLiteral("Der Admin-PIN fehlt.")
                                                     : openPgpLengthHint(true, record.kdf));
            return;
        }
        if (fixedPolicy() && serialEdit->text().trimmed() != serial) {
            problem->setText(QStringLiteral("Die Seriennummer stimmt nicht."));
            return;
        }
        dialog.accept();
    });

    OpenPgpTouchChoice choice;
    if (dialog.exec() == QDialog::Accepted) {
        choice.accepted = true;
        choice.key = key->currentData().toString();
        choice.policy = policy->currentData().toString();
        choice.admin = admin->text();
    }
    wipeEdit(admin);
    wipeEdit(serialEdit);
    return choice;
}

QString openPgpFailureText(const QString &detail)
{
    const QString text = detail.toLower();
    const QRegularExpressionMatch attempts =
        QRegularExpression(QStringLiteral("(\\d+)\\s+attempt")).match(detail);
    const QString left = attempts.hasMatch()
                             ? (attempts.captured(1) == QLatin1String("1")
                                    ? QStringLiteral(" Noch 1 Versuch.")
                                    : QStringLiteral(" Noch %1 Versuche.").arg(attempts.captured(1)))
                             : QString();
    if (text.contains(QLatin1String("wrong admin pin")))
        return QStringLiteral("Der Admin-PIN stimmt nicht.%1").arg(left);
    if (text.contains(QLatin1String("wrong reset code")) || text.contains(QLatin1String("wrong resetting")))
        return QStringLiteral("Der Reset-Code stimmt nicht.%1").arg(left);
    if (text.contains(QLatin1String("wrong pin")))
        return QStringLiteral("Die Benutzer-PIN stimmt nicht.%1").arg(left);
    if (text.contains(QLatin1String("is blocked")) || text.contains(QLatin1String("pin blocked")))
        return QStringLiteral("Die Eingabe ist gesperrt.");
    if (text.contains(QLatin1String("complexity")))
        return QStringLiteral("Der neue Wert erfüllt die Komplexitätsregel nicht.");
    if (text.contains(QLatin1String("pin length")) || text.contains(QLatin1String("must be in the range")))
        return QStringLiteral("Die Länge passt nicht zu OpenPGP.");
    if (text.contains(QLatin1String("no such command")) || text.contains(QLatin1String("no such option")))
        return QStringLiteral("Dieses ykman kennt keinen OpenPGP-Reset.");
    if (text.contains(QLatin1String("aborted")))
        return QStringLiteral("Abgebrochen.");
    if (detail.trimmed().isEmpty())
        return QStringLiteral("Die OpenPGP-Änderung ist fehlgeschlagen.");
    return detail.trimmed();
}

void wipeOpenPgp(OpenPgpRequest &request)
{
    wipeSecret(request.current);
    wipeSecret(request.next);
    wipeSecret(request.admin);
}

} // namespace

void MainWindow::showOpenPgp(const OpenPgpRecord &record)
{
    m_openPgpKeys->setRowCount(0);
    if (!record.error.isEmpty()) {
        m_openPgpVersion->setText(dash());
        m_openPgpApplication->setText(dash());
        m_openPgpPinState->setText(dash());
        m_openPgpResetState->setText(dash());
        m_openPgpAdminState->setText(dash());
        m_openPgpSignature->setText(dash());
        m_openPgpKdf->setText(dash());
        showNote(m_openPgpNote, record.error);
        m_openPgpPinReady = false;
        m_openPgpAdminReady = false;
        m_openPgpTouchReady = false;
        m_openPgpResetReady = false;
        applyOpenPgpActions(m_client->busy());
        return;
    }

    m_openPgpVersion->setText(record.version.isEmpty() ? dash() : record.version);
    m_openPgpApplication->setText(record.application.isEmpty() ? dash() : record.application);
    m_openPgpPinState->setText(openPgpAttemptsText(record.pinAttempts));
    m_openPgpResetState->setText(openPgpAttemptsText(record.resetAttempts));
    m_openPgpAdminState->setText(openPgpAttemptsText(record.adminAttempts));
    m_openPgpSignature->setText(openPgpSignatureLabel(record.signaturePolicy));
    m_openPgpKdf->setText(record.pinAttempts < 0 ? dash() : (record.kdf ? QStringLiteral("An") : QStringLiteral("Aus")));

    const QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_openPgpKeys->setRowCount(record.keys.size());
    for (int row = 0; row < record.keys.size(); ++row) {
        const OpenPgpKey &key = record.keys.at(row);
        const auto selectable = [](const QString &text) {
            QTableWidgetItem *item = cell(text);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            return item;
        };
        auto *name = selectable(openPgpKeyLabel(key.id, key.name));
        name->setData(Qt::UserRole, key.id);
        auto *fingerprint = selectable(key.fingerprint.isEmpty() ? dash() : key.fingerprint);
        fingerprint->setFont(fixed);
        m_openPgpKeys->setItem(row, 0, name);
        m_openPgpKeys->setItem(row, 1, fingerprint);
        m_openPgpKeys->setItem(row, 2, selectable(openPgpTouchLabel(key.touch)));
    }

    showNote(m_openPgpNote, QString());
    m_openPgpPinReady = record.pinAttempts > 0;
    m_openPgpAdminReady = record.adminAttempts > 0;
    m_openPgpTouchReady = m_openPgpAdminReady && !record.keys.isEmpty();
    m_openPgpResetReady = record.pinAttempts >= 0 || !record.version.isEmpty();
    applyOpenPgpActions(m_client->busy());
}

void MainWindow::applyOpenPgpActions(bool busy)
{
    if (!m_changeOpenPgpPin)
        return;
    m_changeOpenPgpPin->setEnabled(!busy && m_openPgpPinReady);
    m_changeOpenPgpAdmin->setEnabled(!busy && m_openPgpAdminReady);
    m_setOpenPgpReset->setEnabled(!busy && m_openPgpAdminReady);
    m_setOpenPgpSignature->setEnabled(!busy && m_openPgpAdminReady);
    m_setOpenPgpTouch->setEnabled(!busy && m_openPgpTouchReady);
    m_resetOpenPgp->setEnabled(!busy && m_openPgpResetReady);
}

void MainWindow::changeOpenPgpPinClicked()
{
    if (!m_openPgpPinReady || m_client->busy() || m_selectedSerial.isEmpty())
        return;
    const OpenPgpRecord record = m_client->snapshot().openpgp;
    OpenPgpSecrets choice = askOpenPgpSecrets(
        this, QStringLiteral("Benutzer-PIN"),
        openPgpAttemptSentence(record.pinAttempts, QStringLiteral("die Benutzer-PIN")), true, 6, 6, record.kdf,
        QStringLiteral("Bisherige PIN"), QStringLiteral("Neue PIN"), QString(), QStringLiteral("PIN setzen"));
    if (!choice.accepted) {
        wipeSecret(choice.current);
        wipeSecret(choice.next);
        return;
    }
    OpenPgpRequest request;
    request.action = OpenPgpAction::ChangePin;
    request.current = choice.current;
    request.next = choice.next;
    wipeSecret(choice.current);
    wipeSecret(choice.next);
    startOpenPgp(request, QStringLiteral("Die Benutzer-PIN wird geändert."));
}

void MainWindow::changeOpenPgpAdminClicked()
{
    if (!m_openPgpAdminReady || m_client->busy() || m_selectedSerial.isEmpty())
        return;
    const OpenPgpRecord record = m_client->snapshot().openpgp;
    OpenPgpSecrets choice = askOpenPgpSecrets(
        this, QStringLiteral("Admin-PIN"),
        openPgpAttemptSentence(record.adminAttempts, QStringLiteral("den Admin-PIN")), true, 8, 8, record.kdf,
        QStringLiteral("Bisheriger Admin-PIN"), QStringLiteral("Neuer Admin-PIN"), QString(),
        QStringLiteral("Admin-PIN setzen"));
    if (!choice.accepted) {
        wipeSecret(choice.current);
        wipeSecret(choice.next);
        return;
    }
    OpenPgpRequest request;
    request.action = OpenPgpAction::ChangeAdmin;
    request.current = choice.current;
    request.next = choice.next;
    wipeSecret(choice.current);
    wipeSecret(choice.next);
    startOpenPgp(request, QStringLiteral("Der Admin-PIN wird geändert."));
}

void MainWindow::setOpenPgpResetClicked()
{
    if (!m_openPgpAdminReady || m_client->busy() || m_selectedSerial.isEmpty())
        return;
    const OpenPgpRecord record = m_client->snapshot().openpgp;
    QString info = openPgpAttemptSentence(record.adminAttempts, QStringLiteral("den Admin-PIN"));
    info += QStringLiteral(" ykman fragt zuerst den neuen Code und danach den Admin-PIN.");
    OpenPgpSecrets choice = askOpenPgpSecrets(
        this, QStringLiteral("Reset-Code"), info, false, 8, 8, record.kdf, QString(),
        QStringLiteral("Neuer Reset-Code"), QStringLiteral("Admin-PIN"), QStringLiteral("Code setzen"));
    if (!choice.accepted) {
        wipeSecret(choice.next);
        wipeSecret(choice.admin);
        return;
    }
    OpenPgpRequest request;
    request.action = OpenPgpAction::SetResetCode;
    request.next = choice.next;
    request.admin = choice.admin;
    wipeSecret(choice.next);
    wipeSecret(choice.admin);
    startOpenPgp(request, QStringLiteral("Der Reset-Code wird gesetzt."));
}

void MainWindow::setOpenPgpSignatureClicked()
{
    if (!m_openPgpAdminReady || m_client->busy() || m_selectedSerial.isEmpty())
        return;
    const OpenPgpRecord record = m_client->snapshot().openpgp;
    OpenPgpPolicyChoice choice = askOpenPgpSignature(this, record.signaturePolicy, record.adminAttempts, record.kdf);
    if (!choice.accepted) {
        wipeSecret(choice.admin);
        return;
    }
    OpenPgpRequest request;
    request.action = OpenPgpAction::SetSignature;
    request.policy = choice.policy;
    request.admin = choice.admin;
    wipeSecret(choice.admin);
    startOpenPgp(request, QStringLiteral("Die Signatur-PIN wird geändert."));
}

void MainWindow::setOpenPgpTouchClicked()
{
    if (!m_openPgpTouchReady || m_client->busy() || m_selectedSerial.isEmpty())
        return;
    QString selected;
    if (m_openPgpKeys->selectionModel()) {
        const QModelIndexList rows = m_openPgpKeys->selectionModel()->selectedRows();
        if (!rows.isEmpty() && m_openPgpKeys->item(rows.first().row(), 0))
            selected = m_openPgpKeys->item(rows.first().row(), 0)->data(Qt::UserRole).toString();
    }
    const OpenPgpRecord record = m_client->snapshot().openpgp;
    OpenPgpTouchChoice choice = askOpenPgpTouch(this, record, m_selectedSerial, selected);
    if (!choice.accepted) {
        wipeSecret(choice.admin);
        return;
    }
    OpenPgpRequest request;
    request.action = OpenPgpAction::SetTouch;
    request.key = choice.key;
    request.policy = choice.policy;
    request.admin = choice.admin;
    wipeSecret(choice.admin);
    startOpenPgp(request, QStringLiteral("Die Berührung wird geändert. Falls der Key blinkt, jetzt berühren."));
}

void MainWindow::resetOpenPgpClicked()
{
    if (!m_openPgpResetReady || m_client->busy() || m_selectedSerial.isEmpty())
        return;
    if (!confirmSerial(this, m_selectedSerial, QStringLiteral("OpenPGP zurücksetzen"),
                       QStringLiteral("Alle OpenPGP-Schlüssel und Zertifikate werden gelöscht. "
                                      "Benutzer-PIN, Admin-PIN und Reset-Code kehren auf die Werkseinstellung zurück. "
                                      "Der Beglaubigungsschlüssel bleibt. "
                                      "OTP, OATH, FIDO und PIV bleiben. "
                                      "Zum Bestätigen die Seriennummer eintippen."),
                       QStringLiteral("Zurücksetzen")))
        return;
    OpenPgpRequest request;
    request.action = OpenPgpAction::Reset;
    startOpenPgp(request, QStringLiteral("OpenPGP wird zurückgesetzt."));
}

void MainWindow::startOpenPgp(OpenPgpRequest request, const QString &status)
{
    setBusy(true);
    statusBar()->showMessage(status);
    m_client->runOpenPgp(m_selectedSerial, request);
    wipeOpenPgp(request);
}

void MainWindow::finishOpenPgp(bool ok, int action, const QString &detail)
{
    const auto kind = OpenPgpAction(action);
    if (!ok)
        m_pendingStatus = openPgpFailureText(detail);
    else if (kind == OpenPgpAction::ChangeAdmin)
        m_pendingStatus = QStringLiteral("Der Admin-PIN wurde geändert.");
    else if (kind == OpenPgpAction::SetResetCode)
        m_pendingStatus = QStringLiteral("Der Reset-Code wurde gesetzt.");
    else if (kind == OpenPgpAction::SetSignature)
        m_pendingStatus = QStringLiteral("Die Signatur-PIN wurde geändert.");
    else if (kind == OpenPgpAction::SetTouch)
        m_pendingStatus = QStringLiteral("Die Berührung wurde geändert.");
    else if (kind == OpenPgpAction::Reset)
        m_pendingStatus = QStringLiteral("OpenPGP wurde auf die Werkseinstellung gesetzt. Der Beglaubigungsschlüssel bleibt.");
    else
        m_pendingStatus = QStringLiteral("Die Benutzer-PIN wurde geändert.");
    refresh();
}

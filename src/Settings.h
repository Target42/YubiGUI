#pragma once

#include <QString>

namespace Settings {

QString ykmanOverride();
QString resolveYkman();
QString missingYkmanText();
QString resolveSshKeygen();
QString missingSshKeygenText();
QString searchHint();

// Unter Linux wahr, wenn pcscd läuft. Auf anderen Systemen immer wahr.
bool pcscdRunning();

// Befehle zum Kopieren. Leer, wenn nichts fehlt oder das System Windows ist.
QString linuxInstallText(bool ykman, bool pcscd);

// Ein leerer Pfad löscht die Einstellung. Ein gesetzter Pfad wird mit
// `ykman version` geprüft, bevor er gespeichert wird.
bool storeYkman(const QString &path, QString *error);

}

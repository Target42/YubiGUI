#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QUrl>

class QTcpServer;
class QTcpSocket;

// Liefert die Hilfedateien nur an 127.0.0.1 aus. Browser dürfen file:-Adressen
// oft nicht lesen, und ein Snap sieht /usr/share nicht.
class HelpServer : public QObject
{
public:
    explicit HelpServer(QObject *parent = nullptr);
    ~HelpServer() override;

    // docRoot enthält die Ordner hilfe und bilder.
    bool serve(const QString &docRoot);
    QUrl urlForFile(const QString &absoluteFile) const;

private:
    void acceptPending();
    void readSocket(QTcpSocket *socket);
    void reply(QTcpSocket *socket, int status, const QByteArray &type, const QByteArray &body, bool headOnly);
    QString fileForPath(const QString &requestTarget) const;

    QTcpServer *m_server = nullptr;
    QString m_root;
    QHash<QTcpSocket *, QByteArray> m_pending;
};

#include "HelpServer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

namespace {

constexpr int kMaxHeaderBytes = 16 * 1024;
constexpr qint64 kMaxFileBytes = 8 * 1024 * 1024;

QString normalized(const QString &path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

bool insideRoot(const QString &root, const QString &file)
{
    const QString base = normalized(root);
    const QString target = normalized(file);
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity sense = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity sense = Qt::CaseSensitive;
#endif
    return target.startsWith(base + QLatin1Char('/'), sense);
}

QByteArray mimeFor(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == QLatin1String("html") || ext == QLatin1String("htm"))
        return "text/html; charset=utf-8";
    if (ext == QLatin1String("css"))
        return "text/css; charset=utf-8";
    if (ext == QLatin1String("png"))
        return "image/png";
    if (ext == QLatin1String("jpg") || ext == QLatin1String("jpeg"))
        return "image/jpeg";
    if (ext == QLatin1String("gif"))
        return "image/gif";
    if (ext == QLatin1String("webp"))
        return "image/webp";
    if (ext == QLatin1String("svg"))
        return "image/svg+xml";
    if (ext == QLatin1String("ico"))
        return "image/x-icon";
    return {};
}

} // namespace

HelpServer::HelpServer(QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection, this, [this] { acceptPending(); });
}

HelpServer::~HelpServer()
{
    m_pending.clear();
    if (m_server)
        m_server->close();
}

bool HelpServer::serve(const QString &docRoot)
{
    const QString root = QFileInfo(docRoot).canonicalFilePath();
    if (root.isEmpty() || !QFileInfo(root).isDir())
        return false;
    if (m_server->isListening() && normalized(m_root) == normalized(root))
        return true;
    m_server->close();
    m_root = root;
    return m_server->listen(QHostAddress::LocalHost, 0);
}

QUrl HelpServer::urlForFile(const QString &absoluteFile) const
{
    if (!m_server->isListening())
        return {};
    const QString file = QFileInfo(absoluteFile).canonicalFilePath();
    if (file.isEmpty() || !insideRoot(m_root, file))
        return {};
    const QString relative = QDir::fromNativeSeparators(QDir(m_root).relativeFilePath(file));
    if (relative.isEmpty() || relative == QLatin1String(".") || relative.startsWith(QLatin1String("..")))
        return {};
    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(QStringLiteral("127.0.0.1"));
    url.setPort(m_server->serverPort());
    url.setPath(QLatin1Char('/') + relative);
    return url;
}

void HelpServer::acceptPending()
{
    while (QTcpSocket *socket = m_server->nextPendingConnection()) {
        socket->setParent(this);
        m_pending.insert(socket, {});
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { readSocket(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            m_pending.remove(socket);
            socket->deleteLater();
        });
    }
}

void HelpServer::readSocket(QTcpSocket *socket)
{
    if (!m_pending.contains(socket) || socket->property("done").toBool())
        return;
    QByteArray &header = m_pending[socket];
    header += socket->readAll();
    if (header.size() > kMaxHeaderBytes) {
        reply(socket, 400, "text/plain; charset=utf-8", "Ungültige Anfrage", false);
        return;
    }
    const int end = header.indexOf("\r\n\r\n");
    if (end < 0)
        return;

    const QByteArray request = header.left(header.indexOf("\r\n"));
    const QList<QByteArray> parts = request.split(' ');
    if (parts.size() < 2) {
        reply(socket, 400, "text/plain; charset=utf-8", "Ungültige Anfrage", false);
        return;
    }
    const QByteArray method = parts.at(0);
    const bool headOnly = method == "HEAD";
    if (method != "GET" && !headOnly) {
        reply(socket, 405, "text/plain; charset=utf-8", "Nur GET", false);
        return;
    }

    const QString file = fileForPath(QString::fromUtf8(parts.at(1)));
    if (file.isEmpty()) {
        reply(socket, 404, "text/plain; charset=utf-8", "Nicht gefunden", headOnly);
        return;
    }
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly) || in.size() > kMaxFileBytes) {
        reply(socket, 404, "text/plain; charset=utf-8", "Nicht gefunden", headOnly);
        return;
    }
    reply(socket, 200, mimeFor(file), in.readAll(), headOnly);
}

QString HelpServer::fileForPath(const QString &requestTarget) const
{
    QString target = requestTarget;
    const int query = target.indexOf(QLatin1Char('?'));
    if (query >= 0)
        target.truncate(query);
    target = QUrl::fromPercentEncoding(target.toUtf8());
    if (!target.startsWith(QLatin1Char('/')) || target.contains(QLatin1Char('\\')) || target.contains(QChar(QChar::Null)))
        return {};

    const QStringList raw = target.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (raw.isEmpty())
        return fileForPath(QStringLiteral("/hilfe/index.html"));

    QStringList parts;
    for (const QString &part : raw) {
        if (part == QLatin1String(".") || part == QLatin1String(".."))
            return {};
        parts.append(part);
    }
    if (parts.first() != QLatin1String("hilfe") && parts.first() != QLatin1String("bilder"))
        return {};

    QString candidate = QDir(m_root).filePath(parts.join(QLatin1Char('/')));
    if (QFileInfo(candidate).isDir())
        candidate = QDir(candidate).filePath(QStringLiteral("index.html"));
    const QString canonical = QFileInfo(candidate).canonicalFilePath();
    if (canonical.isEmpty() || !QFileInfo(canonical).isFile() || !insideRoot(m_root, canonical))
        return {};
    if (mimeFor(canonical).isEmpty())
        return {};
    return canonical;
}

void HelpServer::reply(QTcpSocket *socket, int status, const QByteArray &type, const QByteArray &body, bool headOnly)
{
    if (socket->property("done").toBool())
        return;
    socket->setProperty("done", true);

    const char *reason = "OK";
    if (status == 400)
        reason = "Bad Request";
    else if (status == 404)
        reason = "Not Found";
    else if (status == 405)
        reason = "Method Not Allowed";

    QByteArray message;
    message += "HTTP/1.1 " + QByteArray::number(status) + ' ' + reason + "\r\n";
    message += "Content-Type: " + type + "\r\n";
    message += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    message += "Connection: close\r\n";
    message += "Cache-Control: no-store\r\n";
    message += "X-Content-Type-Options: nosniff\r\n";
    message += "Referrer-Policy: no-referrer\r\n";
    message += "Cross-Origin-Resource-Policy: same-origin\r\n";
    message += "Content-Security-Policy: default-src 'none'; img-src 'self'; style-src 'self'; base-uri 'none'\r\n";
    if (status == 405)
        message += "Allow: GET, HEAD\r\n";
    message += "\r\n";
    socket->write(message);
    if (!headOnly)
        socket->write(body);
    socket->disconnectFromHost();
}

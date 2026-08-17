#ifndef QT_ERROR_COMPAT_H
#define QT_ERROR_COMPAT_H

#include <QObject>
#include <QProcess>
#include <QAbstractSocket>

// ---------------------------------------------------------------------------
// QtErrorCompat
//
// Provides a unified way to connect to error signals across all Qt versions
// from 5.12.12 through 6.x, without using QT_VERSION checks.
//
// It connects BOTH the legacy "error(...)" signal and the newer
// "errorOccurred(...)" signal, but guarantees that your slot is invoked
// only once per error event.
// ---------------------------------------------------------------------------

class QtErrorCompat : public QObject
{
    Q_OBJECT

public:
    explicit QtErrorCompat(QObject *parent = nullptr)
        : QObject(parent)
    {}

    // -----------------------------------------------------------------------
    // Connect QProcess error signals
    // -----------------------------------------------------------------------
    template<typename Receiver>
    void connectErrors(QProcess *proc,
                       Receiver *receiver,
                       void (Receiver::*slot)(QProcess::ProcessError))
    {
        // Newer Qt (5.15+)
        QObject::connect(proc,
                         SIGNAL(errorOccurred(QProcess::ProcessError)),
                         this,
                         SLOT(handleProcessError(QProcess::ProcessError)));

        // Older Qt (5.12.12)
        QObject::connect(proc,
                         QOverload<QProcess::ProcessError>::of(&QProcess::error),
                         this,
                         &QtErrorCompat::handleProcessError);

        // Forward to user slot
        QObject::connect(this,
                         &QtErrorCompat::processError,
                         receiver,
                         slot);
    }

    // -----------------------------------------------------------------------
    // Connect QAbstractSocket error signals (QTcpSocket, QUdpSocket, etc.)
    // -----------------------------------------------------------------------
    template<typename Receiver>
    void connectErrors(QAbstractSocket *sock,
                       Receiver *receiver,
                       void (Receiver::*slot)(QAbstractSocket::SocketError))
    {
        // Newer Qt (5.15+)
        QObject::connect(sock,
                         SIGNAL(errorOccurred(QAbstractSocket::SocketError)),
                         this,
                         SLOT(handleSocketError(QAbstractSocket::SocketError)));

        // Older Qt (5.12.12)
        QObject::connect(sock,
                         QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error),
                         this,
                         &QtErrorCompat::handleSocketError);

        // Forward to user slot
        QObject::connect(this,
                         &QtErrorCompat::socketError,
                         receiver,
                         slot);
    }

signals:
    void processError(QProcess::ProcessError);
    void socketError(QAbstractSocket::SocketError);

private slots:
    void handleProcessError(QProcess::ProcessError e)
    {
        static bool guard = false;
        if (guard) return;
        guard = true;
        emit processError(e);
        guard = false;
    }

    void handleSocketError(QAbstractSocket::SocketError e)
    {
        static bool guard = false;
        if (guard) return;
        guard = true;
        emit socketError(e);
        guard = false;
    }
};

#endif // QT_ERROR_COMPAT_H

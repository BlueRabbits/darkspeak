#ifndef CONNECTIONSOCKET_H
#define CONNECTIONSOCKET_H

#include <memory>

#include <QTcpSocket>
#include <QUuid>

#include "ds/memoryview.h"

namespace ds {
namespace prot {

class ConnectionSocket : public QTcpSocket
{
    Q_OBJECT

public:
    using ptr_t = std::shared_ptr<ConnectionSocket>;
    using data_t = crypto::MemoryView<uint8_t>;

    ConnectionSocket();
    ~ConnectionSocket();

    const QUuid& getUuid() const noexcept {
        return uuid;
    }

    template <typename T>
    void write(const T& data) {
        const char *p = reinterpret_cast<const char *>(data.data());
        const qint64 bytes = data.size();
        outData.append(p, bytes);
        sendMore();
    }

    void wantBytes(size_t bytesRequested);

signals:
    void connectedToHost(const QUuid& uuid);
    void socketFailed(const QUuid& uuid, const SocketError& socketError);
    void disconnectedFromHost(const QUuid& uuid);
    void haveBytes(const data_t& data);
    void outputBufferEmptied();

private slots:
    void onConnected();
    void onDisconnected();
    void onSocketFailed(const SocketError& socketError);

private:
    void processInput();
    void sendMore();

    QUuid uuid = QUuid::createUuid();
    QByteArray outData;
    QByteArray inData;
    size_t bytesWanted_ = {};
    size_t maxInDataSize = 1024 * 265;
};

}} // namespaces


Q_DECLARE_SMART_POINTER_METATYPE(std::shared_ptr)
Q_DECLARE_METATYPE(ds::prot::ConnectionSocket::ptr_t)
Q_DECLARE_METATYPE(ds::prot::ConnectionSocket::data_t)

#endif // CONNECTIONSOCKET_H

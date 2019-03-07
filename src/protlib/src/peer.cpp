#include <array>
#include <algorithm>
#include <vector>
#include <assert.h>
#include <sodium.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>

#include "ds/peer.h"
#include "ds/message.h"
#include "ds/errors.h"
#include "logfault/logfault.h"

namespace ds {
namespace prot {

using namespace std;
using namespace core;

namespace  {
const std::vector<QString> encoding_names = {"us-ascii", "utf-8"};
const std::map<QString, Message::Encoding>  encoding_lookup = {
    {"us-ascii", Message::US_ACSII},
    {"utf-8", Message::UTF8}
};

Message::Encoding toEncoding(const QString& name) {
    const auto it = encoding_lookup.find(name);
    if (it == encoding_lookup.end()) {
        throw NotFoundError(name);
    }
    return it->second;
}

} // anonymous namespace

Peer::Peer(ConnectionSocket::ptr_t connection,
           core::ConnectData connectionData)
    : connection_{move(connection)}, connectionData_{move(connectionData)}
    , uuid_{connection_->getUuid()}
{
    connect(connection_.get(), &ConnectionSocket::connected,
            this, [this]() {

        LFLOG_DEBUG << "Peer " << getConnectionId().toString()
                    << " is connected";

        //emit connectedToPeer(shared_from_this());
    });

    connect(connection_.get(), &ConnectionSocket::disconnected,
            this, [this]() {

        LFLOG_DEBUG << "Peer " << getConnectionId().toString()
                    << " is disconnected";

        emit disconnectedFromPeer(shared_from_this());
    });

    connect(connection_.get(), &ConnectionSocket::outputBufferEmptied,
            this, [this]() {
       emit outputBufferEmptied();
    }, Qt::QueuedConnection);

    connect(this, &PeerConnection::receivedData,
            this, &Peer::onReceivedData);// Qt::QueuedConnection);

    connect(this, &Peer::closeLater,
            this, &Peer::onCloseLater,
            Qt::QueuedConnection);
}

Peer::~Peer()
{

}

uint64_t Peer::send(const QJsonDocument &json)
{
    if (!connection_->isOpen()) {
        throw runtime_error("Connection is closed");
    }

    auto jsonData = json.toJson(QJsonDocument::Compact);

    // Data format:
    // Two bytes length | one byte version | four bytes channel | 8 bytes id | data

    // The length is encrypted individually to allow the peer to read it before
    // fetching the payload.

    const size_t len = 1 + 4 + 8 + static_cast<size_t>(jsonData.size());
    vector<uint8_t> payload_len(2),
            buffer(len),
            cipherlen(2 + crypt_bytes),
            ciphertext(len + crypt_bytes);
    mview_t version{buffer.data(), 1};
    mview_t channel{version.end(), 4};
    mview_t id{channel.end(), 8};
    mview_t json_payload{id.end(), static_cast<size_t>(jsonData.size())};

    assert(buffer.size() == (+ version.size()
                             + channel.size()
                             + id.size()
                             + json_payload.size()));

    union {
        array<unsigned char, 8> bytes;
        quint16 uint16;
        quint32 uint32;
        quint64 uint64;
    } number_u = {};

    number_u.uint16 = qToBigEndian(static_cast<quint16>(len));
    for(size_t i = 0; i < payload_len.size(); ++i) {
        payload_len.at(i) = number_u.bytes.at(i);
    }

    version.at(0) = '\1';

    // Channel 0 is the control channel
    number_u.uint32 = qToBigEndian(static_cast<quint32>(0));
    for(size_t i = 0; i < channel.size(); ++i) {
        channel.at(i) = number_u.bytes.at(i);
    }

    number_u.uint64 = qToBigEndian(++request_id_);
    for(size_t i = 0; i < id.size(); ++i) {
        id.at(i) = number_u.bytes.at(i);
    }

    assert(static_cast<size_t>(jsonData.size()) == json_payload.size());
    memcpy(json_payload.data(), jsonData.constData(), static_cast<size_t>(jsonData.size()));

    // encrypt length
    if (crypto_secretstream_xchacha20poly1305_push(&stateOut,
                                               cipherlen.data(),
                                               nullptr,
                                               payload_len.data(),
                                               payload_len.size(),
                                               nullptr, 0, 0) != 0) {
        throw runtime_error("Stream encryption failed");
    }

    connection_->write(cipherlen);

    LFLOG_DEBUG << "Sending payloaad to "
                << connection_->getUuid().toString()
                << ": "
                << jsonData;

    // Encrypt the payload
    if (crypto_secretstream_xchacha20poly1305_push(&stateOut,
                                               ciphertext.data(),
                                               nullptr,
                                               buffer.data(),
                                               buffer.size(),
                                               nullptr, 0, 0) != 0) {
        throw runtime_error("Stream encryption failed");
    }

    connection_->write(ciphertext);

    return request_id_;
}

void Peer::onReceivedData(const quint32 channel, const quint64 id, QByteArray data)
{
    if (channel == 0) {
        // Control channel. Data is supposed to be Json.
        QJsonDocument json = QJsonDocument::fromJson(data);
        if (json.isNull()) {
            LFLOG_ERROR << "Incoming data on " << getConnectionId().toString()
                        << " with id=" << id
                        << " is supposed to be in Json format, but it is not.";
            throw runtime_error("Not Json");
        }

        const auto type = json.object().value("type");

        if (type == "AddMe") {
            PeerAddmeReq req{shared_from_this(), getConnectionId(), id,
                        json.object().value("nick").toString(),
                        json.object().value("message").toString(),
                        json.object().value("address").toString().toUtf8(),
                        getPeerCert()->getB58PubKey()};

            LFLOG_DEBUG << "Emitting addmeRequest";
            emit addmeRequest(req);
        } else if (type == "Ack") {
            PeerAck ack{shared_from_this(), getConnectionId(), id,
                        json.object().value("what").toString().toUtf8(),
                        json.object().value("status").toString().toUtf8(),
                        json.object().value("data").toString()};

            LFLOG_DEBUG << "Emitting Ack";
            emit receivedAck(ack);
        } else if (type == "Message") {
            PeerMessage msg{shared_from_this(), getConnectionId(), id,
                        QByteArray::fromBase64(json.object().value("conversation").toString().toUtf8()),
                        QByteArray::fromBase64(json.object().value("message-id").toString().toUtf8()),
                        QDateTime::fromTime_t(json.object().value("date").toString().toUInt()),
                        json.object().value("content").toString(),
                        QByteArray::fromBase64(json.object().value("from").toString().toUtf8()),
                        toEncoding(json.object().value("encoding").toString()),
                        QByteArray::fromBase64(json.object().value("signature").toString().toUtf8())};

            LFLOG_DEBUG << "Emitting PeerMessage";
            emit receivedMessage(msg);
        } else {
            LFLOG_WARN << "Unrecognized request from peer at connection "
                       << getConnectionId().toString();
        }
    }
}

void Peer::onCloseLater()
{
    if (connection_->isOpen()) {
        connection_->close();
    }

    emit disconnectedFromPeer(shared_from_this());
}

void Peer::enableEncryptedStream()
{
    if (inState_ == InState::CLOSING) {
        return;
    }

    assert(inState_ == InState::DISABLED);
    wantChunkSize();
}

void Peer::wantChunkSize()
{
    if (inState_ == InState::CLOSING) {
        return;
    }

    LFLOG_TRACE << "Want chunk-len bytes (2) on " << connection_->getUuid().toString();
    inState_ = InState::CHUNK_SIZE;
    connection_->wantBytes(2 + crypt_bytes);
}

void Peer::wantChunkData(const size_t bytes)
{
    if (inState_ == InState::CLOSING) {
        return;
    }

    LFLOG_TRACE << "Want " << bytes << " data-bytes on " << connection_->getUuid().toString();
    inState_ = InState::CHUNK_DATA;
    connection_->wantBytes(bytes + crypt_bytes);
}

// Incoming, stream-encrypted data
void Peer::processStream(const Peer::data_t &ciphertext)
{
    if (inState_ == InState::CLOSING) {
        return;
    }

    if (inState_ == InState::CHUNK_SIZE) {
        union {
            array<uint8_t, 2> bytes;
            quint16 uint16;
        } number_u = {};
        mview_t data{number_u.bytes};
        decrypt(data, ciphertext);
        wantChunkData(qFromBigEndian(number_u.uint16));
    } else if (inState_ == InState::CHUNK_DATA){

        static const QByteArray binary = {"[binary]"};
            assert(ciphertext.size() >= crypt_bytes + 5);
        std::vector<uint8_t> buffer(ciphertext.size() - crypt_bytes);
        mview_t buffer_view{buffer};
        mview_t version{buffer.data(), 1};
        mview_t channel{version.end(), 4};
        mview_t id{channel.end(), 8};

        const int payload_size = static_cast<int>(buffer.size())
                - static_cast<int>(version.size())
                - static_cast<int>(channel.size())
                - static_cast<int>(id.size());

        if (payload_size < 0) {
            throw runtime_error("Payload size underflow");
        }

        mview_t payload{id.end(), static_cast<size_t>(payload_size)};

        assert(buffer.size() == (+ version.size()
                                 + channel.size()
                                 + id.size()
                                 + payload.size()));

        decrypt(buffer_view, ciphertext);

        if (version.at(0) != '\1') {
            LFLOG_WARN << "Unknown chunk version" << static_cast<unsigned int>(version.at(0));
            throw runtime_error("Unknown chunk version");
        }

        union {
            array<unsigned char, 8> bytes;
            quint16 uint16;
            quint32 uint32;
            quint64 uint64;
        } number_u = {};


        for(size_t i = 0; i < channel.size(); ++i) {
            number_u.bytes.at(i) = channel.at(i);
        }

        const quint32 channel_id = qFromBigEndian(number_u.uint32);

        number_u = {};
        for(size_t i = 0; i < id.size(); ++i) {
            number_u.bytes.at(i) = id.at(i);
        }

        const quint64 chunk_id = qFromBigEndian(number_u.uint64);


        LFLOG_DEBUG << "Received chunk on "
                    << connection_->getUuid().toString()
                    << ", size=" << payload.size()
                    << ", channel=" << channel_id
                    << ", id=" << chunk_id
                    << ", payload=" << (channel_id ? binary : safePayload(payload));

        try {
            onReceivedData(channel_id, chunk_id, payload.toByteArray());
        } catch (const std::exception& ex) {
            LFLOG_ERROR << "Caught exception while processing incoming message on connection "
                        << getConnectionId().toString()
                        << " :"
                        << ex.what();
            close();
            return;
        }

        wantChunkSize();
    } else {
        throw runtime_error("Unexpected InState");
    }
}

void Peer::prepareEncryption(Peer::stream_state_t &state,
                             mview_t& header,
                             Peer::mview_t &key)
{
    crypto_secretstream_xchacha20poly1305_keygen(key.data());

    if (crypto_secretstream_xchacha20poly1305_init_push(&state, header.data(), key.cdata()) != 0) {
        throw runtime_error("Failed to initialize encryption");
    }

}

void Peer::prepareDecryption(Peer::stream_state_t &state,
                             const mview_t &header,
                             const Peer::mview_t &key)
{
    if (crypto_secretstream_xchacha20poly1305_init_pull(&state, header.cdata(), key.cdata()) != 0) {
        LFLOG_WARN << "Invalid decryption key / header from: " << connection_->getUuid().toString();
        throw runtime_error("Failed to initialize decryption");
    }
}

void Peer::decrypt(Peer::mview_t &data, const Peer::mview_t &ciphertext)
{
    assert((data.size() + crypt_bytes) == ciphertext.size());
    unsigned char tag = {};
    if (crypto_secretstream_xchacha20poly1305_pull(&stateIn,
                                                   data.data(),
                                                   nullptr,
                                                   &tag,
                                                   ciphertext.cdata(),
                                                   ciphertext.size(),
                                                   nullptr, 0) != 0) {
        throw runtime_error("Decryption of stream failed");
    }

    if (tag == crypto_secretstream_xchacha20poly1305_TAG_FINAL) {

        // NOTE: Currently we dont use this feature, so this is not supposed to happen.

        LFLOG_DEBUG << "Received tag 'FINAL' on " << connection_->getUuid().toString()
                    << ". Closing connection";

        // TODO: Send final in the other direction before close?
        connection_->close();
    }
}

QByteArray Peer::safePayload(const Peer::mview_t &data)
{
    const auto json_text = data.toByteArray();
    QJsonDocument json = QJsonDocument::fromJson(json_text);
    if (!json.isNull()) {
        return json_text;
    }

    return "*** NOT Json ***";
}

QUuid Peer::getConnectionId() const
{
    return uuid_;
}

crypto::DsCert::ptr_t Peer::getPeerCert() const noexcept
{
    return connectionData_.contactsCert;
}

void Peer::close()
{
   inState_ = InState::CLOSING;
   emit closeLater();
}

QUuid Peer::getIdentityId() const noexcept
{
    return connectionData_.service;
}

uint64_t Peer::sendAck(const QString &what, const QString &status, const QString& data)
{
    auto json = QJsonDocument{
        QJsonObject{
            {"type", "Ack"},
            {"what", what},
            {"status", status},
            {"data", data}
        }
    };

    LFLOG_DEBUG << "Sending Ack: " << what
                << " with status: " << status
                << " over connection " << getConnectionId().toString();

    return send(json);
}

bool Peer::isConnected() const noexcept
{
    return connection_ && connection_->isOpen();
}

uint64_t Peer::sendMessage(const core::Message &message)
{
    auto json = QJsonDocument{
        QJsonObject{
            {"type", "Message"},
            {"message-id", QString{message.getData().messageId.toBase64()}},
            {"date", static_cast<qint64>(message.getData().composedTime.toTime_t())},
            {"content", message.getData().content},
            {"encoding", encoding_names.at(static_cast<size_t>(message.getData().encoding))},
            {"conversation", QString{message.getData().conversation.toBase64()}},
            {"from", QString{message.getData().sender.toBase64()}},
            {"signature", QString{message.getData().signature.toBase64()}}
        }
    };

    LFLOG_DEBUG << "Sending Message: " << message.getId()
                << " over connection " << getConnectionId().toString();

    return send(json);
}


}} // namespace


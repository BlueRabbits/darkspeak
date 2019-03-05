#ifndef MESSAGE_H
#define MESSAGE_H

#include <memory>

#include <QObject>
#include <QByteArray>
#include <QDateTime>

#include "ds/dscert.h"

namespace ds {
namespace core {

struct MessageData;

class Message : public QObject, public std::enable_shared_from_this<Message>
{
    Q_OBJECT
public:
    enum Encoding
    {
        US_ACSII,
        UTF8
    };

    Q_ENUM(Encoding)

    enum Direction {
        OUTGOING,
        INCOMING
    };

    Q_ENUM(Direction)

    using ptr_t = std::shared_ptr<Message>;

    Message(QObject& parent);

    Q_PROPERTY(int id READ getId)
    Q_PROPERTY(Direction direction READ getDirection)
    Q_PROPERTY(QDateTime composed READ getComposedTime)
    Q_PROPERTY(QDateTime received READ getSentReceivedTime NOTIFY onReceivedChanged)
    Q_PROPERTY(QString content READ getContent)

    int getId() const noexcept;
    int getConversationId() const noexcept;
    Direction getDirection() const noexcept;
    QDateTime getComposedTime() const noexcept;
    QDateTime getSentReceivedTime() const noexcept;
    void setSentReceivedTime(const QDateTime when);
    void touchSentReceivedTime();
    QString getContent() const noexcept;
    Direction getType() const noexcept;

    void init();
    void sign(const crypto::DsCert& cert);
    bool validate(const crypto::DsCert& cert) const;

    /*! Add the new Message to the database. */
    void addToDb();

    /*! Delete from the database */
    void deleteFromDb();

    const char *getTableName() const noexcept { return "message"; }

    static ptr_t load(QObject& parent, int dbId);

signals:
    void onReceivedChanged();

private:
    int id_ = -1;
    int conversationId_ = -1;
    Direction direction_ = Direction::OUTGOING;
    QDateTime sentReceivedTime_; // Depending on type
    std::unique_ptr<MessageData> data_;
};

struct MessageData {
    // Hash key for conversation
    // For p2p, it's the receivers pubkey hash.
    QByteArray conversation;
    QByteArray messageId;
    QDateTime composedTime;
    QString content;

    // Senders pubkey hash.
    QByteArray sender;
    Message::Encoding encoding = Message::Encoding::US_ACSII;
    QByteArray signature;
};

struct MessageContent {
    QString content;
    QDateTime composedTime;
    Message::Direction direction = Message::OUTGOING;
    QDateTime sentReceivedTime;
};

}} // Namespaces

Q_DECLARE_METATYPE(ds::core::Message *)
Q_DECLARE_METATYPE(ds::core::MessageData)

#endif // MESSAGE_H

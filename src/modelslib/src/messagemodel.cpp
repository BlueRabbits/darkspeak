
#include "ds/messagemodel.h"
#include "ds/dsengine.h"
#include "ds/dscert.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QBuffer>
#include <QDateTime>
#include <QDebug>
#include <QSqlRecord>
#include <QUuid>


using namespace std;
using namespace ds::core;
using namespace ds::crypto;

namespace ds {
namespace models {

MessageModel::MessageModel(QObject &parent)
    : QAbstractListModel(&parent)
{
    auto mgr = DsEngine::instance().getMessageManager();
    connect(mgr, &MessageManager::messageAdded,
            this, &MessageModel::onMessageAdded);
    connect(mgr, &MessageManager::messageDeleted,
            this, &MessageModel::onMessageDeleted);
    connect(mgr, &MessageManager::messageReceivedDateChanged,
            this, &MessageModel::onMessageReceivedDateChanged);
}

void MessageModel::setConversation(Conversation *conversation)
{
    if (conversation != conversation_.get()) {
        return;
    }

    conversation_ = conversation ? conversation->shared_from_this() : nullptr;

    beginResetModel();

    rows_.clear();
    queryRows(rows_);

    endResetModel();
}

int MessageModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return static_cast<int>(rows_.size());
}

QVariant MessageModel::data(const QModelIndex &ix, int role) const
{
    if (!ix.isValid()) {
        return {};
    }

    auto &r = rows_.at(static_cast<size_t>(ix.row()));
    // Lazy loading
    if (!r.data_) {
        r.data_ = loadData(r.id);
    }

    switch(role) {
    case H_ID:
        return r.id;
    case H_CONTENT:
        return r.data_->content;
    case H_COMPOSED:
        return r.data_->composedTime;
    case H_DIRECTION:
        return r.data_->direction;
    case H_RECEIVED:
        return r.data_->sentReceivedTime;
    }

    return QAbstractListModel::data(ix, role);
}

QHash<int, QByteArray> MessageModel::roleNames() const
{
    static const QHash<int, QByteArray> names = {
        {H_ID, "messageId"},
        {H_CONTENT, "content"},
        {H_COMPOSED, "composedTime"},
        {H_DIRECTION, "direction"},
        {H_RECEIVED, "receivedTime"}
    };

    return names;
}

void MessageModel::onMessageAdded(const Message::ptr_t &message)
{
    if (!conversation_ || (conversation_->getId() != message->getConversationId())) {
        return; // Irrelevant
    }

    // Always add at the end
    const int rowid = static_cast<int>(rows_.size());
    beginInsertRows({}, rowid, rowid);
    rows_.push_back({message->getId(), loadData(*message)});
    endInsertRows();
}

void MessageModel::onMessageDeleted(const Message::ptr_t &message)
{
    if (!conversation_ || (conversation_->getId() != message->getConversationId())) {
        return; // Irrelevant
    }

    const int messageId = message->getId();
    int rowid = 0;
    for(auto it = rows_.begin(); it != rows_.end(); ++it, ++rowid) {
        if (it->id == messageId) {
            beginRemoveRows({}, rowid, rowid);
            rows_.erase(it);
            endRemoveRows();
            return;
        }
    }
}

void MessageModel::onMessageReceivedDateChanged(const Message::ptr_t &message)
{
    if (!conversation_ || (conversation_->getId() != message->getConversationId())) {
        return; // Irrelevant
    }

    const int messageId = message->getId();
    int rowid = 0;
    for(auto it = rows_.begin(); it != rows_.end(); ++it, ++rowid) {
        if (it->id == messageId) {
            emit dataChanged({}, index(rowid), {H_RECEIVED});
            return;
        }
    }
}

void MessageModel::queryRows(MessageModel::rows_t &rows)
{
    if (!conversation_) {
        return;
    }

    QSqlQuery query;
    query.prepare("SELECT id FROM message WHERE conversation_id=:cid ORDER BY id");
    query.bindValue(":cid", conversation_->getId());

    if(!query.exec()) {
        throw Error(QStringLiteral("Failed to add Conversation: %1").arg(
                        query.lastError().text()));
    }

    // Populate
    while(query.next()) {
        rows.emplace_back(query.value(0).toInt());
    }
}

std::shared_ptr<MessageContent> MessageModel::loadData(const int id) const
{
    // TODO: Try to get it from the message cache

    // Read from database
    QSqlQuery query;

    enum Fields {
        direction, composed_time, received_time, content
    };

    query.prepare("SELECT direction, composed_time, received_time, content FROM message where id=:id ");
    query.bindValue(":id", id);

    if(!query.exec()) {
        throw Error(QStringLiteral("Failed to fetch Message: %1").arg(
                        query.lastError().text()));
    }

    if (!query.next()) {
        throw NotFoundError(QStringLiteral("Message not found!"));
    }

    auto ptr = make_shared<MessageContent>();

    ptr->direction = static_cast<Message::Direction>(query.value(direction).toInt());
    ptr->composedTime = query.value(composed_time).toDateTime();
    ptr->sentReceivedTime = query.value(received_time).toDateTime();
    ptr->content = query.value(content).toString();

    return ptr;
}

std::shared_ptr<MessageContent> MessageModel::loadData(const Message &message) const
{
    auto ptr = make_shared<MessageContent>();

    ptr->direction = message.getDirection();
    ptr->composedTime = message.getComposedTime();
    ptr->sentReceivedTime = message.getSentReceivedTime();
    ptr->content = message.getContent();

    return ptr;
}



}} // namespaces

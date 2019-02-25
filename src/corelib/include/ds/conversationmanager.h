#ifndef CONVERSATIONMANAGER_H
#define CONVERSATIONMANAGER_H

#include <deque>
#include <unordered_map>
#include <QUuid>
#include <QObject>

#include "ds/conversation.h"
#include "ds/identity.h"
#include "ds/registry.h"
#include "ds/lru_cache.h"


namespace ds {
namespace core {

class ConversationManager : public QObject
{
    Q_OBJECT
public:
    ConversationManager(QObject& parent);

    // Get existing conversation
    Conversation::ptr_t getConversation(const QUuid& uuid);

    // Get or create a new p2p conversation with this contact
    Conversation::ptr_t getConversation(Contact *participant);

    // Delete a conversations and all its messages
    void deleteConversation(const QUuid& uuid);

    // Add p2p conversation
     Conversation::ptr_t addConversation(const QString& name, const QString& topic, Contact *participant);

    // Put the Conversation at the front of the lru cache
    void touch(const Conversation::ptr_t& Conversation);

signals:
    void ConversationAdded(const Conversation::ptr_t& conversation);
    void ConversationDeleted(const QUuid& Conversation);

private:
    Registry<QUuid, Conversation> registry_;
    LruCache<Conversation::ptr_t> lru_cache_{3};

};

}} // namespaces

#endif // CONVERSATIONMANAGER_H

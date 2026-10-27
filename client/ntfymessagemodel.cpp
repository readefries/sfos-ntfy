// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ntfymessagemodel.h"

#include "ntfydaemonclient.h"
#include "ntfynotificationcontent.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

const int NtfyMessageModel::PageSize;

NtfyMessageModel::NtfyMessageModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void NtfyMessageModel::setClient(NtfyDaemonClient *client)
{
    if (m_client == client)
        return;
    if (m_client)
        m_client->disconnect(this);
    m_client = client;
    if (m_client) {
        connect(m_client, &NtfyDaemonClient::messagesLoaded, this, &NtfyMessageModel::onMessagesLoaded);
        connect(m_client, &NtfyDaemonClient::messageReceived, this, &NtfyMessageModel::onMessageReceived);
    }
    emit clientChanged();
    reload();
}

void NtfyMessageModel::setSubscriptionId(qlonglong subscriptionId)
{
    if (m_subscriptionId == subscriptionId)
        return;
    m_subscriptionId = subscriptionId;
    emit subscriptionIdChanged();
    reload();
}

void NtfyMessageModel::reload()
{
    if (!m_client || m_subscriptionId <= 0)
        return;
    setLoading(true);
    m_client->fetchMessages(m_subscriptionId, PageSize, 0);
}

void NtfyMessageModel::loadMore()
{
    if (!m_client || m_loading || !m_canLoadMore || m_items.isEmpty())
        return;
    setLoading(true);
    m_client->fetchMessages(m_subscriptionId, PageSize, m_items.last().rowId);
}

NtfyMessageModel::Item NtfyMessageModel::itemFromJson(const QJsonObject &message)
{
    Item item;
    item.rowId = qlonglong(message.value(QLatin1String("id")).toDouble());
    item.read = message.value(QLatin1String("read")).toBool();
    item.event = NtfyEvent::fromJson(
            QJsonDocument(message.value(QLatin1String("event")).toObject()).toJson(QJsonDocument::Compact));
    return item;
}

void NtfyMessageModel::onMessagesLoaded(qlonglong subscriptionId, qlonglong beforeId, const QJsonArray &messages)
{
    if (subscriptionId != m_subscriptionId)
        return;

    QList<Item> items;
    for (const QJsonValue &message : messages)
        items.append(itemFromJson(message.toObject()));

    if (beforeId == 0) {
        beginResetModel();
        m_items = items;
        endResetModel();
    } else if (!items.isEmpty()) {
        beginInsertRows(QModelIndex(), m_items.size(), m_items.size() + items.size() - 1);
        m_items.append(items);
        endInsertRows();
    }
    emit countChanged();
    setCanLoadMore(items.size() >= PageSize);
    setLoading(false);
}

void NtfyMessageModel::onMessageReceived(qlonglong subscriptionId, const QJsonObject &message)
{
    if (subscriptionId != m_subscriptionId)
        return;

    const Item item = itemFromJson(message);
    for (const Item &existing : m_items) {
        if (existing.rowId == item.rowId)
            return;
    }
    beginInsertRows(QModelIndex(), 0, 0);
    m_items.prepend(item);
    endInsertRows();
    emit countChanged();
}

void NtfyMessageModel::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void NtfyMessageModel::setCanLoadMore(bool canLoadMore)
{
    if (m_canLoadMore == canLoadMore)
        return;
    m_canLoadMore = canLoadMore;
    emit canLoadMoreChanged();
}

int NtfyMessageModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant NtfyMessageModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return QVariant();

    const Item &item = m_items.at(index.row());
    const NtfyEvent &event = item.event;
    switch (role) {
    case RowIdRole:
        return item.rowId;
    case MessageIdRole:
        return event.id;
    case TitleRole:
        // The notification falls back to the topic; within a topic's page that's just noise.
        return event.title.isEmpty() ? QString() : NtfyNotificationContent::fromMessage(event).summary;
    case BodyRole:
        return NtfyNotificationContent::fromMessage(event).body;
    case TimeRole:
        return QDateTime::fromMSecsSinceEpoch(event.time * 1000);
    case PriorityRole:
        return event.priority;
    case ClickRole:
        return event.click;
    case AttachmentNameRole:
        return event.attachment.name;
    case AttachmentUrlRole:
        return event.attachment.url;
    case ReadRole:
        return item.read;
    }
    return QVariant();
}

QHash<int, QByteArray> NtfyMessageModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles.insert(RowIdRole, "rowId");
    roles.insert(MessageIdRole, "messageId");
    roles.insert(TitleRole, "title");
    roles.insert(BodyRole, "body");
    roles.insert(TimeRole, "time");
    roles.insert(PriorityRole, "priority");
    roles.insert(ClickRole, "click");
    roles.insert(AttachmentNameRole, "attachmentName");
    roles.insert(AttachmentUrlRole, "attachmentUrl");
    roles.insert(ReadRole, "read");
    return roles;
}

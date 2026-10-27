// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ntfysubscriptionmodel.h"

#include "ntfydaemonclient.h"
#include "ntfynotificationcontent.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QUrl>

namespace {

qlonglong idOf(const QJsonValue &value)
{
    return qlonglong(value.toObject().value(QLatin1String("id")).toDouble());
}

QString serverName(const QString &url)
{
    const QUrl parsed(url);
    QString name = parsed.host() + parsed.path();
    if (parsed.port() > 0)
        name = parsed.host() + QLatin1Char(':') + QString::number(parsed.port()) + parsed.path();
    if (parsed.scheme() == QLatin1String("http"))
        name = QStringLiteral("http://") + name;
    return name;
}

NtfyEvent lastEvent(const QJsonObject &subscription)
{
    const QJsonObject last = subscription.value(QLatin1String("lastMessage")).toObject();
    if (last.isEmpty())
        return NtfyEvent();
    return NtfyEvent::fromJson(QJsonDocument(last).toJson(QJsonDocument::Compact));
}

} // namespace

NtfySubscriptionModel::NtfySubscriptionModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void NtfySubscriptionModel::setClient(NtfyDaemonClient *client)
{
    if (m_client == client)
        return;
    if (m_client)
        m_client->disconnect(this);
    m_client = client;
    if (m_client) {
        connect(m_client, &NtfyDaemonClient::subscriptionsLoaded, this, &NtfySubscriptionModel::setSubscriptions);
        connect(m_client, &NtfyDaemonClient::subscriptionsChanged, this, &NtfySubscriptionModel::refresh);
        connect(m_client, &NtfyDaemonClient::messageReceived, this, &NtfySubscriptionModel::refresh);
        refresh();
    }
    emit clientChanged();
}

void NtfySubscriptionModel::refresh()
{
    if (m_client)
        m_client->fetchSubscriptions();
}

void NtfySubscriptionModel::setSubscriptions(const QJsonArray &subscriptions)
{
    const int oldTotal = totalUnread();

    bool sameRows = subscriptions.size() == m_subscriptions.size();
    for (int i = 0; sameRows && i < subscriptions.size(); ++i)
        sameRows = idOf(subscriptions.at(i)) == idOf(m_subscriptions.at(i));

    if (sameRows) {
        // Update in place, so views keep their scroll position and delegates.
        m_subscriptions = subscriptions;
        if (!m_subscriptions.isEmpty())
            emit dataChanged(index(0), index(m_subscriptions.size() - 1));
    } else {
        beginResetModel();
        m_subscriptions = subscriptions;
        endResetModel();
        emit countChanged();
    }

    if (totalUnread() != oldTotal)
        emit totalUnreadChanged();
    if (!m_loaded) {
        m_loaded = true;
        emit loadedChanged();
    }
}

int NtfySubscriptionModel::totalUnread() const
{
    int total = 0;
    for (const QJsonValue &subscription : m_subscriptions) {
        const QJsonObject obj = subscription.toObject();
        if (!obj.value(QLatin1String("muted")).toBool())
            total += obj.value(QLatin1String("unread")).toInt();
    }
    return total;
}

int NtfySubscriptionModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_subscriptions.size();
}

QVariant NtfySubscriptionModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_subscriptions.size())
        return QVariant();

    const QJsonObject obj = m_subscriptions.at(index.row()).toObject();
    switch (role) {
    case SubscriptionIdRole:
        return idOf(obj);
    case ServerIdRole:
        return qlonglong(obj.value(QLatin1String("serverId")).toDouble());
    case ServerUrlRole:
        return obj.value(QLatin1String("serverUrl")).toString();
    case ServerNameRole:
        return serverName(obj.value(QLatin1String("serverUrl")).toString());
    case TopicRole:
        return obj.value(QLatin1String("topic")).toString();
    case DisplayNameRole:
        return obj.value(QLatin1String("displayName")).toString();
    case TitleRole: {
        const QString name = obj.value(QLatin1String("displayName")).toString();
        return name.isEmpty() ? obj.value(QLatin1String("topic")).toString() : name;
    }
    case MutedRole:
        return obj.value(QLatin1String("muted")).toBool();
    case UnreadRole:
        return obj.value(QLatin1String("unread")).toInt();
    case LastMessageRole: {
        const NtfyEvent event = lastEvent(obj);
        if (!event.isValid())
            return QString();
        const NtfyNotificationContent content = NtfyNotificationContent::fromMessage(event);
        return event.title.isEmpty() ? content.body : content.summary + QStringLiteral(": ") + content.body;
    }
    case LastTimeRole: {
        const NtfyEvent event = lastEvent(obj);
        return event.isValid() ? QDateTime::fromMSecsSinceEpoch(event.time * 1000) : QDateTime();
    }
    }
    return QVariant();
}

QHash<int, QByteArray> NtfySubscriptionModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles.insert(SubscriptionIdRole, "subscriptionId");
    roles.insert(ServerIdRole, "serverId");
    roles.insert(ServerUrlRole, "serverUrl");
    roles.insert(ServerNameRole, "serverName");
    roles.insert(TopicRole, "topic");
    roles.insert(DisplayNameRole, "displayName");
    roles.insert(TitleRole, "title");
    roles.insert(MutedRole, "muted");
    roles.insert(UnreadRole, "unread");
    roles.insert(LastMessageRole, "lastMessage");
    roles.insert(LastTimeRole, "lastTime");
    return roles;
}

int NtfySubscriptionModel::indexOf(qlonglong subscriptionId) const
{
    for (int i = 0; i < m_subscriptions.size(); ++i) {
        if (idOf(m_subscriptions.at(i)) == subscriptionId)
            return i;
    }
    return -1;
}

QVariantMap NtfySubscriptionModel::get(int row) const
{
    QVariantMap result;
    if (row < 0 || row >= m_subscriptions.size())
        return result;
    const QHash<int, QByteArray> roles = roleNames();
    for (auto it = roles.constBegin(); it != roles.constEnd(); ++it)
        result.insert(QString::fromLatin1(it.value()), data(index(row), it.key()));
    return result;
}

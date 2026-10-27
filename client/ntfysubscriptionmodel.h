// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef NTFYSUBSCRIPTIONMODEL_H
#define NTFYSUBSCRIPTIONMODEL_H

#include "ntfydaemonclient.h"

#include <QAbstractListModel>
#include <QJsonArray>
#include <QPointer>

// All subscriptions, kept up to date from the daemon's change signals.
class NtfySubscriptionModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(NtfyDaemonClient *client READ client WRITE setClient NOTIFY clientChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int totalUnread READ totalUnread NOTIFY totalUnreadChanged)
    Q_PROPERTY(bool loaded READ isLoaded NOTIFY loadedChanged)
public:
    enum Roles {
        SubscriptionIdRole = Qt::UserRole + 1,
        ServerIdRole,
        ServerUrlRole,
        // "ntfy.sh", or the host (and path) of a self-hosted server
        ServerNameRole,
        TopicRole,
        DisplayNameRole,
        // display name, falling back to the topic
        TitleRole,
        MutedRole,
        UnreadRole,
        LastMessageRole, // notification-style summary and body of the newest message
        LastTimeRole
    };
    Q_ENUM(Roles)

    explicit NtfySubscriptionModel(QObject *parent = nullptr);

    NtfyDaemonClient *client() const { return m_client; }
    void setClient(NtfyDaemonClient *client);

    int totalUnread() const;
    bool isLoaded() const { return m_loaded; }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Row of a subscription, or -1.
    Q_INVOKABLE int indexOf(qlonglong subscriptionId) const;
    Q_INVOKABLE QVariantMap get(int row) const;

public slots:
    void refresh();

signals:
    void clientChanged();
    void countChanged();
    void totalUnreadChanged();
    void loadedChanged();

private:
    void setSubscriptions(const QJsonArray &subscriptions);

    QPointer<NtfyDaemonClient> m_client;
    QJsonArray m_subscriptions;
    bool m_loaded = false;
};

#endif // NTFYSUBSCRIPTIONMODEL_H

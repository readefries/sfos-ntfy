// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef NTFYMESSAGEMODEL_H
#define NTFYMESSAGEMODEL_H

#include "ntfydaemonclient.h"
#include "ntfymessage.h"

#include <QAbstractListModel>
#include <QPointer>

// Message history of one subscription, newest first. New messages are added as they arrive;
// older ones are paged in with loadMore().
class NtfyMessageModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(NtfyDaemonClient *client READ client WRITE setClient NOTIFY clientChanged)
    Q_PROPERTY(qlonglong subscriptionId READ subscriptionId WRITE setSubscriptionId NOTIFY subscriptionIdChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(bool loading READ isLoading NOTIFY loadingChanged)
    Q_PROPERTY(bool canLoadMore READ canLoadMore NOTIFY canLoadMoreChanged)
public:
    enum Roles {
        RowIdRole = Qt::UserRole + 1,
        MessageIdRole,
        TitleRole, // with emoji for tags; empty if the message has no title
        BodyRole,  // message text plus a "Tags:" line for non-emoji tags
        TimeRole,
        PriorityRole,
        ClickRole,
        AttachmentNameRole,
        AttachmentUrlRole,
        ReadRole
    };
    Q_ENUM(Roles)

    explicit NtfyMessageModel(QObject *parent = nullptr);

    NtfyDaemonClient *client() const { return m_client; }
    void setClient(NtfyDaemonClient *client);
    qlonglong subscriptionId() const { return m_subscriptionId; }
    void setSubscriptionId(qlonglong subscriptionId);
    bool isLoading() const { return m_loading; }
    bool canLoadMore() const { return m_canLoadMore; }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    static const int PageSize = 50;

public slots:
    void reload();
    void loadMore();

signals:
    void clientChanged();
    void subscriptionIdChanged();
    void countChanged();
    void loadingChanged();
    void canLoadMoreChanged();

private:
    struct Item {
        qlonglong rowId = 0;
        bool read = false;
        NtfyEvent event;
    };

    void onMessagesLoaded(qlonglong subscriptionId, qlonglong beforeId, const QJsonArray &messages);
    void onMessageReceived(qlonglong subscriptionId, const QJsonObject &message);
    void setLoading(bool loading);
    void setCanLoadMore(bool canLoadMore);
    static Item itemFromJson(const QJsonObject &message);

    QPointer<NtfyDaemonClient> m_client;
    qlonglong m_subscriptionId = 0;
    QList<Item> m_items;
    bool m_loading = false;
    bool m_canLoadMore = false;
};

#endif // NTFYMESSAGEMODEL_H

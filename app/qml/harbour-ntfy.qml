// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.ntfy 1.0
import "pages"

ApplicationWindow {
    id: app

    property alias subscriptions: subscriptionModel

    SubscriptionModel {
        id: subscriptionModel
        client: daemon
    }

    Connections {
        target: daemon
        onFailed: Notices.show(error, Notice.Long, Notice.Center)
    }

    initialPage: Component { SubscriptionsPage { } }
    cover: Qt.resolvedUrl("cover/CoverPage.qml")
    allowedOrientations: defaultAllowedOrientations
}

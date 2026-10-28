// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.ntfy 1.0
import "pages"

ApplicationWindow {
    id: app

    property alias subscriptions: subscriptionModel
    // A notification tap that arrived before the subscriptions were loaded.
    property var pendingSubscription: 0

    SubscriptionModel {
        id: subscriptionModel
        client: daemon
        onLoadedChanged: {
            if (loaded && pendingSubscription > 0)
                showSubscription(pendingSubscription)
        }
    }

    function showSubscription(subscriptionId) {
        activate()
        if (!subscriptionModel.loaded) {
            pendingSubscription = subscriptionId
            return
        }
        pendingSubscription = 0
        var row = subscriptionModel.indexOf(subscriptionId)
        if (row < 0)
            return // unsubscribed in the meantime

        var root = pageStack.find(function(page) { return page.objectName === "subscriptionsPage" })
        pageStack.pop(root, PageStackAction.Immediate)
        pageStack.push(Qt.resolvedUrl("pages/MessagesPage.qml"),
                       { subscriptionId: subscriptionId, title: subscriptionModel.get(row).title },
                       PageStackAction.Immediate)
    }

    Connections {
        target: uiService
        onSubscriptionRequested: showSubscription(subscriptionId)
        onActivationRequested: activate()
    }

    Connections {
        target: daemon
        onFailed: Notices.show(error, Notice.Long, Notice.Center)
    }

    initialPage: Component { SubscriptionsPage { } }
    cover: Qt.resolvedUrl("cover/CoverPage.qml")
    allowedOrientations: defaultAllowedOrientations
}

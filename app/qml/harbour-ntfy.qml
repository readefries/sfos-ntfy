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
    // After a reboot the daemon is locked. Opening the app is the moment to ask for the device
    // lock code, but only once: after a cancel the subscriptions page offers it.
    property bool unlockAsked: false

    function unlockOnce() {
        if (daemon.locked && !unlockAsked && Qt.application.active) {
            unlockAsked = true
            daemon.unlock()
        }
    }

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
        onUnlockFailed: Notices.show(qsTr("Could not unlock: %1").arg(error), Notice.Long, Notice.Center)
        onLockedChanged: unlockOnce()
    }

    Connections {
        target: Qt.application
        onActiveChanged: unlockOnce()
    }

    initialPage: Component { SubscriptionsPage { } }
    cover: Qt.resolvedUrl("cover/CoverPage.qml")
    allowedOrientations: defaultAllowedOrientations
}

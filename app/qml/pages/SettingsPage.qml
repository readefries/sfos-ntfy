// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page

    readonly property var modes: ["instant", "balanced", "battery-saver"]
    property var delivery: ({})
    property var stats: ({})
    property var servers: []

    readonly property string mode: delivery.mode || ""
    readonly property var intervals: delivery.options && mode ? delivery.options[mode] : []

    function modeName(mode) {
        switch (mode) {
        case "instant": return qsTr("Instant")
        case "balanced": return qsTr("Balanced")
        case "battery-saver": return qsTr("Battery saver")
        }
        return mode
    }

    // Based on measurements on a Jolla phone: incoming data on an open connection wakes the
    // phone, so the streaming modes deliver right away; the interval is about how soon a dropped
    // connection gets noticed.
    function modeDescription(mode) {
        switch (mode) {
        case "instant":
            return qsTr("Messages arrive within seconds. If the connection drops, for example when switching " +
                        "networks, that's noticed within the chosen interval. Uses the most battery.")
        case "balanced":
            return qsTr("Messages arrive within seconds as long as the connection holds. After it drops, they can " +
                        "take up to the chosen interval. Recommended for most people.")
        case "battery-saver":
            return qsTr("No permanent connection: messages are fetched at the chosen interval, so they can arrive " +
                        "that much later. Uses the least battery.")
        }
        return ""
    }

    function intervalText(seconds) {
        if (seconds < 60)
            return qsTr("Every %1 seconds").arg(seconds)
        if (seconds < 3600)
            return qsTr("Every %1 minutes").arg(Number(seconds / 60).toLocaleString(Qt.locale(), "f", seconds % 60 ? 1 : 0))
        return seconds === 3600 ? qsTr("Every hour") : qsTr("Every %1 hours").arg(seconds / 3600)
    }

    function stateText(state) {
        switch (state) {
        case "connected": return qsTr("Connected")
        case "connecting": return qsTr("Connecting")
        case "waiting": return qsTr("Waiting to reconnect")
        }
        return page.mode === "battery-saver" ? qsTr("Checked at intervals") : qsTr("Not connected")
    }

    function refresh() {
        daemon.fetchDelivery()
        daemon.fetchDeliveryStats()
        daemon.fetchServers()
    }

    Component.onCompleted: refresh()

    Connections {
        target: daemon
        onDeliveryLoaded: page.delivery = delivery
        onDeliveryStatsLoaded: page.stats = stats
        onServersLoaded: page.servers = servers
        onDeliveryChanged: daemon.fetchDelivery()
        onSubscriptionsChanged: daemon.fetchServers()
        onServerStateChanged: daemon.fetchServers()
        onServerError: daemon.fetchServers()
        onServerUpdated: daemon.fetchServers()
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            MenuItem {
                text: qsTr("Refresh")
                onClicked: page.refresh()
            }
        }

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: qsTr("Settings")
            }

            SectionHeader {
                text: qsTr("Delivery")
            }

            ComboBox {
                label: qsTr("Mode")
                currentIndex: modes.indexOf(page.mode)
                menu: ContextMenu {
                    Repeater {
                        model: modes
                        MenuItem {
                            text: modeName(modelData)
                            onClicked: {
                                if (modelData !== page.mode)
                                    daemon.setDelivery(modelData, delivery.defaults[modelData])
                            }
                        }
                    }
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                text: modeDescription(page.mode)
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
            }

            ComboBox {
                label: page.mode === "battery-saver" ? qsTr("Check") : qsTr("Check connection")
                currentIndex: intervals.indexOf(delivery.interval)
                menu: ContextMenu {
                    Repeater {
                        model: intervals
                        MenuItem {
                            text: intervalText(modelData)
                            onClicked: daemon.setDelivery(page.mode, modelData)
                        }
                    }
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                visible: stats.messages > 0 || stats.wakeups > 0
                text: qsTr("Since the service started: %1 messages, on average %2 s after they were sent (at most %3 s). " +
                           "%4 wake-ups.")
                      .arg(stats.messages)
                      .arg(Number(stats.averageDelay || 0).toLocaleString(Qt.locale(), "f", 1))
                      .arg(stats.maxDelay)
                      .arg(stats.wakeups)
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
            }

            SectionHeader {
                text: qsTr("Servers")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                visible: servers.length === 0
                text: qsTr("Servers show up here once you subscribe to a topic.")
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
            }

            Repeater {
                model: servers

                ListItem {
                    id: serverItem
                    contentHeight: serverColumn.height + 2 * Theme.paddingMedium

                    menu: ContextMenu {
                        MenuItem {
                            text: qsTr("Change login")
                            onClicked: pageStack.push(Qt.resolvedUrl("ServerLoginDialog.qml"), { serverUrl: modelData.url })
                        }
                        MenuItem {
                            text: qsTr("Remove server")
                            onClicked: {
                                var id = modelData.id
                                serverItem.remorseAction(qsTr("Removing server and its subscriptions"),
                                                         function() { daemon.removeServer(id) })
                            }
                        }
                    }

                    Column {
                        id: serverColumn
                        x: Theme.horizontalPageMargin
                        width: parent.width - 2 * Theme.horizontalPageMargin
                        anchors.verticalCenter: parent.verticalCenter

                        Label {
                            width: parent.width
                            text: modelData.url
                            truncationMode: TruncationMode.Fade
                            color: modelData.url.indexOf("http://") === 0 ? Theme.errorColor
                                                                          : (serverItem.highlighted ? Theme.highlightColor
                                                                                                    : Theme.primaryColor)
                        }
                        Label {
                            width: parent.width
                            text: stateText(modelData.state)
                            font.pixelSize: Theme.fontSizeExtraSmall
                            color: serverItem.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                        }
                        Label {
                            width: parent.width
                            visible: modelData.error.length > 0
                            text: modelData.error
                            wrapMode: Text.Wrap
                            textFormat: Text.PlainText
                            font.pixelSize: Theme.fontSizeExtraSmall
                            color: Theme.errorColor
                        }
                    }
                }
            }

            SectionHeader {
                text: qsTr("About")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                text: qsTr("ntfy for Sailfish OS %1").arg(Qt.application.version) + "\n"
                      + qsTr("Free software under the GNU Affero General Public License, version 3 or later.")
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
            }

            Item { width: 1; height: Theme.paddingLarge }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Source code")
                onClicked: {
                    var url = "https://github.com/readefries/sfos-ntfy"
                    var dialog = pageStack.push(Qt.resolvedUrl("OpenLinkDialog.qml"), { url: url })
                    dialog.accepted.connect(function() { Qt.openUrlExternally(url) })
                }
            }
        }

        VerticalScrollDecorator { }
    }
}

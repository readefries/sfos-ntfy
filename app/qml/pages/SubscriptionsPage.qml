// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page
    objectName: "subscriptionsPage"

    SilicaListView {
        id: list
        anchors.fill: parent
        model: app.subscriptions

        header: PageHeader {
            title: qsTr("Subscriptions")
        }

        PullDownMenu {
            MenuItem {
                text: qsTr("Settings")
                onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
            }
            MenuItem {
                text: qsTr("Add subscription")
                onClicked: pageStack.push(Qt.resolvedUrl("AddSubscriptionDialog.qml"))
            }
        }

        ViewPlaceholder {
            enabled: app.subscriptions.loaded && list.count === 0
            text: qsTr("No subscriptions")
            hintText: qsTr("Pull down to subscribe to a topic")
        }

        delegate: ListItem {
            id: item
            contentHeight: Math.max(Theme.itemSizeMedium, column.height + 2 * Theme.paddingMedium)

            onClicked: pageStack.push(Qt.resolvedUrl("MessagesPage.qml"),
                                      { subscriptionId: model.subscriptionId, title: model.title })

            menu: ContextMenu {
                MenuItem {
                    text: model.muted ? qsTr("Unmute") : qsTr("Mute")
                    onClicked: daemon.setMuted(model.subscriptionId, !model.muted)
                }
                MenuItem {
                    text: qsTr("Unsubscribe")
                    onClicked: {
                        var id = model.subscriptionId
                        item.remorseAction(qsTr("Unsubscribing"), function() { daemon.unsubscribe(id) })
                    }
                }
            }

            Column {
                id: column
                anchors {
                    left: parent.left
                    right: badge.left
                    leftMargin: Theme.horizontalPageMargin
                    rightMargin: Theme.paddingMedium
                    verticalCenter: parent.verticalCenter
                }

                Label {
                    width: parent.width
                    text: model.title
                    truncationMode: TruncationMode.Fade
                    font.bold: model.unread > 0 && !model.muted
                    color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
                }
                Label {
                    width: parent.width
                    visible: text.length > 0
                    text: model.lastMessage
                    maximumLineCount: 2
                    wrapMode: Text.Wrap
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    font.pixelSize: Theme.fontSizeSmall
                    color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                }
                Label {
                    width: parent.width
                    // Only worth showing for self-hosted servers.
                    visible: model.serverName !== "ntfy.sh" || model.muted
                    text: {
                        var parts = []
                        if (model.serverName !== "ntfy.sh")
                            parts.push(model.serverName)
                        if (model.muted)
                            parts.push(qsTr("Muted"))
                        return parts.join(" · ")
                    }
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: model.serverName.indexOf("http://") === 0 ? Theme.errorColor
                                                                   : (item.highlighted ? Theme.secondaryHighlightColor
                                                                                       : Theme.secondaryColor)
                }
            }

            Label {
                id: badge
                anchors {
                    right: parent.right
                    rightMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }
                visible: model.unread > 0
                text: model.unread
                color: model.muted ? Theme.secondaryColor : Theme.highlightColor
                font.pixelSize: Theme.fontSizeLarge
            }
        }

        VerticalScrollDecorator { }
    }
}

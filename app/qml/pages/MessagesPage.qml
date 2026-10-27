// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.ntfy 1.0

Page {
    id: page

    property alias subscriptionId: messageModel.subscriptionId
    property string title

    MessageModel {
        id: messageModel
        client: daemon
    }

    // Whatever is on screen counts as read.
    onStatusChanged: {
        if (status === PageStatus.Active)
            daemon.markRead(subscriptionId)
    }
    Connections {
        target: daemon
        onMessageReceived: {
            if (subscriptionId === page.subscriptionId && page.status === PageStatus.Active && Qt.application.active)
                daemon.markRead(page.subscriptionId)
        }
    }
    Connections {
        target: Qt.application
        onActiveChanged: {
            if (Qt.application.active && page.status === PageStatus.Active)
                daemon.markRead(page.subscriptionId)
        }
    }

    SilicaListView {
        id: list
        anchors.fill: parent
        model: messageModel

        header: PageHeader {
            title: page.title
        }

        ViewPlaceholder {
            enabled: list.count === 0 && !messageModel.loading
            text: qsTr("No messages yet")
            hintText: qsTr("New messages on this topic show up here")
        }

        onAtYEndChanged: {
            if (atYEnd && messageModel.canLoadMore)
                messageModel.loadMore()
        }

        delegate: ListItem {
            id: item
            contentHeight: column.height + 2 * Theme.paddingMedium

            menu: ContextMenu {
                MenuItem {
                    text: qsTr("Copy text")
                    onClicked: Clipboard.text = model.title.length > 0 ? model.title + "\n" + model.body : model.body
                }
            }

            // High (4) and max (5) priority get a bar in the margin.
            Rectangle {
                visible: model.priority >= 4
                width: Theme.paddingSmall
                height: column.height
                anchors {
                    left: parent.left
                    leftMargin: (Theme.horizontalPageMargin - width) / 2
                    verticalCenter: parent.verticalCenter
                }
                radius: width / 2
                color: model.priority === 5 ? Theme.errorColor : Theme.highlightColor
            }

            Column {
                id: column
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.paddingSmall

                Label {
                    width: parent.width
                    visible: text.length > 0
                    text: model.title
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                    font.bold: true
                    color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
                }
                Label {
                    width: parent.width
                    text: model.body
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                    color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
                }
                Label {
                    width: parent.width
                    text: Format.formatDate(model.time, Formatter.TimepointRelative)
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                }
            }
        }

        VerticalScrollDecorator { }
    }
}

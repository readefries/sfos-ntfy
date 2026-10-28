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

    function openLink(url, label) {
        var dialog = pageStack.push(Qt.resolvedUrl("OpenLinkDialog.qml"), { url: url, label: label || "" })
        dialog.accepted.connect(function() { Qt.openUrlExternally(url) })
    }

    function runAction(rowId, index, action) {
        if (action.action === "view") {
            openLink(action.url, action.label)
            return
        }
        var dialog = pageStack.push(Qt.resolvedUrl("OpenLinkDialog.qml"),
                                    { url: action.url, label: action.label, mode: "send", method: action.method })
        dialog.accepted.connect(function() { daemon.runAction(rowId, index) })
    }

    Connections {
        target: daemon
        onActionFinished: Notices.show(ok ? qsTr("Sent (%1)").arg(result) : qsTr("Failed: %1").arg(result),
                                       Notice.Short, Notice.Center)
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

            readonly property var actionList: model.actions
            readonly property var rowId: model.rowId
            contentHeight: column.height + 2 * Theme.paddingMedium
            // Tapping a message follows its link, like the notification would on other platforms.
            onClicked: {
                if (model.click.length > 0)
                    openLink(model.click, "")
            }

            menu: ContextMenu {
                MenuItem {
                    text: qsTr("Copy text")
                    onClicked: Clipboard.text = model.title.length > 0 ? model.title + "\n" + model.body : model.body
                }
                MenuItem {
                    visible: model.click.length > 0
                    text: qsTr("Copy link")
                    onClicked: Clipboard.text = model.click
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
                BackgroundItem {
                    id: attachment
                    visible: model.attachmentUrl.length > 0
                    width: parent.width
                    height: attachmentLabel.height + 2 * Theme.paddingSmall
                    enabled: !model.attachmentExpired
                    onClicked: openLink(model.attachmentUrl, model.attachmentName)

                    Label {
                        id: attachmentLabel
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        text: "📎 " + (model.attachmentName.length > 0 ? model.attachmentName : qsTr("Attachment"))
                              + (model.attachmentSize > 0 ? " · " + Format.formatFileSize(model.attachmentSize) : "")
                              + (model.attachmentExpired ? " · " + qsTr("expired") : "")
                        truncationMode: TruncationMode.Fade
                        font.pixelSize: Theme.fontSizeSmall
                        color: model.attachmentExpired ? Theme.secondaryColor
                                                       : (attachment.highlighted ? Theme.highlightColor
                                                                                 : Theme.secondaryHighlightColor)
                    }
                }

                Flow {
                    width: parent.width
                    spacing: Theme.paddingMedium
                    visible: actionRepeater.count > 0

                    Repeater {
                        id: actionRepeater
                        // "broadcast" actions are for Android intents.
                        model: {
                            var actions = []
                            for (var i = 0; i < item.actionList.length; ++i) {
                                if (item.actionList[i].action === "view" || item.actionList[i].action === "http")
                                    actions.push({ index: i, action: item.actionList[i] })
                            }
                            return actions
                        }
                        Button {
                            text: modelData.action.label
                            preferredWidth: Theme.buttonWidthSmall
                            onClicked: runAction(item.rowId, modelData.index, modelData.action)
                        }
                    }
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

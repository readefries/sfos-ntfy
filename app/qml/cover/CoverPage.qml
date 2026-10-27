// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick 2.0
import Sailfish.Silica 1.0

CoverBackground {
    Column {
        visible: !placeholder.visible
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.paddingLarge
        spacing: Theme.paddingSmall

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: app.subscriptions.totalUnread
            font.pixelSize: Theme.fontSizeHuge
            color: app.subscriptions.totalUnread > 0 ? Theme.highlightColor : Theme.secondaryColor
        }
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("unread", "", app.subscriptions.totalUnread)
            wrapMode: Text.Wrap
            color: Theme.secondaryColor
        }
    }

    CoverPlaceholder {
        id: placeholder
        visible: app.subscriptions.loaded && app.subscriptions.count === 0
        text: "ntfy"
        icon.source: "/usr/share/icons/hicolor/86x86/apps/harbour-ntfy.png"
    }
}

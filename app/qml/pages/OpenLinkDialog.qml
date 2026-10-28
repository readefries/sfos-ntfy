// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick 2.0
import Sailfish.Silica 1.0

// Shows where a link or action goes before anything happens: messages come from whoever can
// publish to the topic.
Dialog {
    id: dialog

    property string url
    // "open" for links and view actions, "send" for http actions
    property string mode: "open"
    property string method: "POST"
    property string label

    readonly property string scheme: {
        var match = /^([a-z][a-z0-9+.-]*):/i.exec(url)
        return match ? match[1].toLowerCase() : ""
    }
    readonly property bool allowed: mode === "send" ? (scheme === "https" || scheme === "http")
                                                    : ["https", "http", "mailto", "tel", "geo"].indexOf(scheme) >= 0

    canAccept: allowed

    Column {
        width: parent.width
        spacing: Theme.paddingLarge

        DialogHeader {
            acceptText: mode === "send" ? qsTr("Send") : qsTr("Open")
            title: label.length > 0 ? label : (mode === "send" ? qsTr("Send request") : qsTr("Open link"))
        }

        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * Theme.horizontalPageMargin
            text: mode === "send" ? qsTr("This sends a %1 request to:").arg(method)
                                  : qsTr("This opens:")
            wrapMode: Text.Wrap
            color: Theme.highlightColor
        }

        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * Theme.horizontalPageMargin
            text: url
            wrapMode: Text.WrapAnywhere
            textFormat: Text.PlainText
            font.family: "monospace"
            font.pixelSize: Theme.fontSizeSmall
        }

        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * Theme.horizontalPageMargin
            visible: scheme === "http"
            text: qsTr("Unencrypted: others on the network can see and change it.")
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.errorColor
        }

        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * Theme.horizontalPageMargin
            visible: !allowed
            text: qsTr("Links of this type can't be opened.")
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.errorColor
        }
    }
}

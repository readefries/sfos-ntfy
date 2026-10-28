// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick 2.0
import Sailfish.Silica 1.0

Dialog {
    id: dialog

    property string serverUrl

    readonly property int auth: authBox.currentIndex // 0 none, 1 token, 2 username and password
    canAccept: auth === 0 || (auth === 1 && tokenField.text.length > 0) || (auth === 2 && userField.text.length > 0)

    onAccepted: daemon.updateServer(serverUrl,
                                    auth === 1 ? tokenField.text : "",
                                    auth === 2 ? userField.text : "",
                                    auth === 2 ? passwordField.text : "")

    Column {
        width: parent.width

        DialogHeader {
            acceptText: qsTr("Save")
            title: qsTr("Login")
        }

        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * Theme.horizontalPageMargin
            text: serverUrl
            truncationMode: TruncationMode.Fade
            color: Theme.highlightColor
        }

        Item { width: 1; height: Theme.paddingLarge }

        ComboBox {
            id: authBox
            label: qsTr("Login")
            description: auth === 0 ? qsTr("Removes the stored login for this server.")
                                    : qsTr("Replaces the stored login for this server.")
            menu: ContextMenu {
                MenuItem { text: qsTr("No login") }
                MenuItem { text: qsTr("Access token") }
                MenuItem { text: qsTr("Username and password") }
            }
        }

        PasswordField {
            id: tokenField
            visible: auth === 1
            width: parent.width
            label: qsTr("Access token")
            placeholderText: "tk_…"
        }

        TextField {
            id: userField
            visible: auth === 2
            width: parent.width
            label: qsTr("Username")
            placeholderText: qsTr("Username")
            inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            EnterKey.iconSource: "image://theme/icon-m-enter-next"
            EnterKey.onClicked: passwordField.focus = true
        }

        PasswordField {
            id: passwordField
            visible: auth === 2
            width: parent.width
        }

        Label {
            visible: auth !== 0
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * Theme.horizontalPageMargin
            text: qsTr("Stored encrypted in Sailfish Secrets and only sent to this server.")
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryHighlightColor
        }
    }
}

// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick 2.0
import Sailfish.Silica 1.0

Dialog {
    id: dialog

    readonly property bool customServer: serverBox.currentIndex === 1
    readonly property string serverUrl: customServer ? urlField.text.trim() : "https://ntfy.sh"
    readonly property bool insecure: customServer && serverUrl.toLowerCase().indexOf("http://") === 0
    readonly property bool urlValid: /^https?:\/\/[^\s\/]+/i.test(serverUrl)
    readonly property int auth: authBox.currentIndex // 0 none, 1 token, 2 username and password
    readonly property bool authValid: auth === 0
                                      || (auth === 1 && tokenField.text.length > 0)
                                      || (auth === 2 && userField.text.length > 0)

    canAccept: topicField.acceptableInput && urlValid && authValid

    onAccepted: daemon.subscribe(serverUrl, topicField.text, nameField.text.trim(),
                                 auth === 1 ? tokenField.text : "",
                                 auth === 2 ? userField.text : "",
                                 auth === 2 ? passwordField.text : "")

    // Hard to guess, like the ntfy apps suggest: on public servers the topic name is the password.
    function randomTopic() {
        var chars = "abcdefghijklmnopqrstuvwxyz0123456789"
        var name = ""
        for (var i = 0; i < 20; ++i)
            name += chars.charAt(Math.floor(Math.random() * chars.length))
        return name
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width

            DialogHeader {
                acceptText: qsTr("Subscribe")
            }

            TextField {
                id: topicField
                width: parent.width
                label: qsTr("Topic")
                placeholderText: qsTr("Topic")
                validator: RegExpValidator { regExp: /^[-_A-Za-z0-9]{1,64}$/ }
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhPreferLowercase
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: nameField.focus = true
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Generate a name")
                onClicked: topicField.text = randomTopic()
            }

            Item { width: 1; height: Theme.paddingMedium }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                text: qsTr("Letters, digits, - and _. On public servers anyone who knows a topic name can read " +
                           "and send its messages, so pick one that's hard to guess.")
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
            }

            Item { width: 1; height: Theme.paddingMedium }

            TextField {
                id: nameField
                width: parent.width
                label: qsTr("Display name (optional)")
                placeholderText: qsTr("Display name (optional)")
                EnterKey.iconSource: "image://theme/icon-m-enter-close"
                EnterKey.onClicked: focus = false
            }

            ComboBox {
                id: serverBox
                label: qsTr("Server")
                menu: ContextMenu {
                    MenuItem { text: "ntfy.sh" }
                    MenuItem { text: qsTr("Other server") }
                }
            }

            TextField {
                id: urlField
                visible: customServer
                width: parent.width
                label: qsTr("Server URL")
                placeholderText: "https://ntfy.example.com"
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                EnterKey.iconSource: "image://theme/icon-m-enter-close"
                EnterKey.onClicked: focus = false
            }

            Label {
                visible: insecure
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                text: qsTr("Unencrypted connection: anyone on the network can read your topics, messages " +
                           "and credentials. Use https:// if the server supports it.")
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.errorColor
            }

            ComboBox {
                id: authBox
                label: qsTr("Login")
                description: qsTr("Only for servers or topics that need it. Leave it at \"None\" to keep " +
                                  "the login you already set up for this server.")
                menu: ContextMenu {
                    MenuItem { text: qsTr("None") }
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

        VerticalScrollDecorator { }
    }
}

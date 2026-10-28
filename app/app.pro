include(../common.pri)

TARGET = harbour-ntfy
CONFIG += sailfishapp
QT += dbus

include(../client/client.pri)
include(../core/core.pri)

HEADERS += src/ntfyuiservice.h
SOURCES += \
    src/main.cpp \
    src/ntfyuiservice.cpp

dbus.files = nl.readefries.harbourntfy.ui.service
dbus.path = /usr/share/dbus-1/services
INSTALLS += dbus

SAILFISHAPP_ICONS = 86x86 108x108 128x128 172x172

DISTFILES += \
    harbour-ntfy.desktop \
    nl.readefries.harbourntfy.ui.service \
    qml/pages/OpenLinkDialog.qml \
    qml/pages/ServerLoginDialog.qml \
    qml/pages/SettingsPage.qml \
    qml/harbour-ntfy.qml \
    qml/cover/CoverPage.qml \
    qml/pages/AddSubscriptionDialog.qml \
    qml/pages/MessagesPage.qml \
    qml/pages/SubscriptionsPage.qml

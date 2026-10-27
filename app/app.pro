include(../common.pri)

TARGET = harbour-ntfy
CONFIG += sailfishapp
QT += dbus

include(../client/client.pri)
include(../core/core.pri)

SOURCES += src/main.cpp

SAILFISHAPP_ICONS = 86x86 108x108 128x128 172x172

DISTFILES += \
    harbour-ntfy.desktop \
    qml/harbour-ntfy.qml \
    qml/cover/CoverPage.qml \
    qml/pages/AddSubscriptionDialog.qml \
    qml/pages/MessagesPage.qml \
    qml/pages/SubscriptionsPage.qml

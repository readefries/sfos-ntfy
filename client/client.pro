include(../common.pri)
include(../core/core.pri)

TEMPLATE = lib
TARGET = ntfyclient
CONFIG += staticlib
QT = core network dbus

HEADERS += \
    ntfydaemonclient.h \
    ntfymessagemodel.h \
    ntfysubscriptionmodel.h

SOURCES += \
    ntfydaemonclient.cpp \
    ntfymessagemodel.cpp \
    ntfysubscriptionmodel.cpp

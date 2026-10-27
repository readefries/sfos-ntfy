include(../common.pri)
include(../core/core.pri)

TEMPLATE = lib
TARGET = ntfyservice
CONFIG += staticlib
QT = core network sql dbus

HEADERS += \
    ntfydatabase.h \
    ntfydbusservice.h \
    ntfydeliverycontroller.h \
    ntfysubscriptionmanager.h

SOURCES += \
    ntfydatabase.cpp \
    ntfydbusservice.cpp \
    ntfydeliverycontroller.cpp \
    ntfysubscriptionmanager.cpp

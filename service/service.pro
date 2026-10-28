include(../common.pri)
include(../core/core.pri)

TEMPLATE = lib
TARGET = ntfyservice
CONFIG += staticlib
QT = core network sql dbus

HEADERS += \
    ntfyactionrunner.h \
    ntfydatabase.h \
    ntfydatabasekey.h \
    ntfydbusservice.h \
    ntfydeliverycontroller.h \
    ntfysubscriptionmanager.h

SOURCES += \
    ntfyactionrunner.cpp \
    ntfydatabase.cpp \
    ntfydatabasekey.cpp \
    ntfydbusservice.cpp \
    ntfydeliverycontroller.cpp \
    ntfysubscriptionmanager.cpp

include(../common.pri)
include(../core/core.pri)

TEMPLATE = lib
TARGET = ntfyservice
CONFIG += staticlib
QT = core network sql

HEADERS += \
    ntfydatabase.h \
    ntfysubscriptionmanager.h

SOURCES += \
    ntfydatabase.cpp \
    ntfysubscriptionmanager.cpp

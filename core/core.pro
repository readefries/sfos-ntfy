include(../common.pri)

TEMPLATE = lib
TARGET = ntfycore
CONFIG += staticlib
QT = core network

HEADERS += \
    ntfymessage.h \
    ntfyserver.h \
    ntfystream.h \
    ntfypoller.h

SOURCES += \
    ntfymessage.cpp \
    ntfyserver.cpp \
    ntfystream.cpp \
    ntfypoller.cpp

include(../common.pri)

TEMPLATE = lib
TARGET = ntfycore
CONFIG += staticlib
QT = core network

HEADERS += \
    ntfycredentials.h \
    ntfymessage.h \
    ntfynotificationcontent.h \
    ntfyserver.h \
    ntfystream.h \
    ntfypoller.h

SOURCES += \
    ntfycredentials.cpp \
    ntfymessage.cpp \
    ntfynotificationcontent.cpp \
    ntfyserver.cpp \
    ntfystream.cpp \
    ntfypoller.cpp

include(../common.pri)
include(../core/core.pri)

TEMPLATE = lib
TARGET = ntfynotify
CONFIG += staticlib link_pkgconfig
PKGCONFIG += nemonotifications-qt5
QT = core network dbus gui

HEADERS += ntfynotifier.h
SOURCES += ntfynotifier.cpp

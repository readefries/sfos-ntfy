include(../common.pri)
include(../core/core.pri)

TEMPLATE = lib
TARGET = ntfysecrets
CONFIG += staticlib link_pkgconfig
PKGCONFIG += sailfishsecrets
QT = core network dbus

HEADERS += ntfysecretscredentialstore.h
SOURCES += ntfysecretscredentialstore.cpp

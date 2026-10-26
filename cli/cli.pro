include(../common.pri)

TEMPLATE = app
TARGET = harbour-ntfy-cli
CONFIG += console
CONFIG -= app_bundle
QT = core network

packagesExist(nemonotifications-qt5): include(../notify/notify.pri)
include(../core/core.pri)

SOURCES += main.cpp

target.path = /usr/bin
INSTALLS += target

include(../common.pri)
include(../core/core.pri)

TEMPLATE = app
TARGET = harbour-ntfy-cli
CONFIG += console
CONFIG -= app_bundle
QT = core network

SOURCES += main.cpp

target.path = /usr/bin
INSTALLS += target

include(../../common.pri)

TEMPLATE = app
TARGET = secrets-check
CONFIG += console
CONFIG -= app_bundle
QT = core network

include(../../secrets/secrets.pri)
include(../../core/core.pri)

SOURCES += main.cpp

# Development aid only: not installed, copy it to the device by hand.

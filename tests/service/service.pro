include(../../common.pri)
include(../../service/service.pri)
include(../../core/core.pri)

TEMPLATE = app
TARGET = tst_service
CONFIG += testcase no_testcase_installs console
CONFIG -= app_bundle
QT = core network sql dbus testlib

INCLUDEPATH += ../common
HEADERS += ../common/fakentfyserver.h
SOURCES += tst_service.cpp

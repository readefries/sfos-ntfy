include(../../common.pri)
include(../../client/client.pri)
include(../../service/service.pri)
include(../../core/core.pri)

TEMPLATE = app
TARGET = tst_client
CONFIG += testcase no_testcase_installs console
CONFIG -= app_bundle
QT = core network sql dbus testlib

SOURCES += tst_client.cpp

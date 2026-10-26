include(../common.pri)
include(../core/core.pri)

TEMPLATE = app
TARGET = tst_ntfy
CONFIG += testcase no_testcase_installs console
CONFIG -= app_bundle
QT = core network testlib

SOURCES += tst_ntfy.cpp

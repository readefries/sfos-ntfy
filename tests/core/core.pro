include(../../common.pri)
include(../../core/core.pri)

TEMPLATE = app
TARGET = tst_ntfy
CONFIG += testcase no_testcase_installs console
CONFIG -= app_bundle
QT = core network testlib

INCLUDEPATH += ../common
HEADERS += ../common/fakentfyserver.h
SOURCES += tst_ntfy.cpp

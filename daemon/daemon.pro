include(../common.pri)

TEMPLATE = app
TARGET = harbour-ntfy-daemon
CONFIG += console
CONFIG -= app_bundle
QT = core network sql dbus

include(../notify/notify.pri)
include(../secrets/secrets.pri)
include(../service/service.pri)
include(../core/core.pri)

CONFIG += link_pkgconfig
PKGCONFIG += keepalive

HEADERS += ntfybackgroundactivityscheduler.h
SOURCES += \
    main.cpp \
    ntfybackgroundactivityscheduler.cpp

target.path = /usr/bin

systemd.files = harbour-ntfy-daemon.service
systemd.path = /usr/lib/systemd/user

dbus.files = nl.readefries.harbourntfy.service
dbus.path = /usr/share/dbus-1/services

INSTALLS += target systemd dbus
OTHER_FILES += $$systemd.files $$dbus.files

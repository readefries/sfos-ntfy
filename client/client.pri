# Include (before core.pri) from projects that talk to the daemon through ntfyclient.
INCLUDEPATH += $$PWD
DEPENDPATH += $$PWD
NTFYCLIENT_BUILD_DIR = $$shadowed($$PWD)
LIBS += -L$$NTFYCLIENT_BUILD_DIR -lntfyclient
PRE_TARGETDEPS += $$NTFYCLIENT_BUILD_DIR/libntfyclient.a
QT += dbus

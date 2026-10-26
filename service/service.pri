# Include (before core.pri) from projects that use the subscription manager and database.
INCLUDEPATH += $$PWD
DEPENDPATH += $$PWD
NTFYSERVICE_BUILD_DIR = $$shadowed($$PWD)
LIBS += -L$$NTFYSERVICE_BUILD_DIR -lntfyservice
PRE_TARGETDEPS += $$NTFYSERVICE_BUILD_DIR/libntfyservice.a
QT += sql dbus

# Include from projects that keep credentials in Sailfish Secrets through ntfysecrets.
INCLUDEPATH += $$PWD
DEPENDPATH += $$PWD
NTFYSECRETS_BUILD_DIR = $$shadowed($$PWD)
LIBS += -L$$NTFYSECRETS_BUILD_DIR -lntfysecrets
PRE_TARGETDEPS += $$NTFYSECRETS_BUILD_DIR/libntfysecrets.a
CONFIG += link_pkgconfig
PKGCONFIG += sailfishsecrets
QT += dbus
DEFINES += NTFY_HAVE_SECRETS

# Include from projects that post Sailfish notifications through ntfynotify.
INCLUDEPATH += $$PWD
DEPENDPATH += $$PWD
NTFYNOTIFY_BUILD_DIR = $$shadowed($$PWD)
LIBS += -L$$NTFYNOTIFY_BUILD_DIR -lntfynotify
PRE_TARGETDEPS += $$NTFYNOTIFY_BUILD_DIR/libntfynotify.a
CONFIG += link_pkgconfig
PKGCONFIG += nemonotifications-qt5
QT += dbus gui
DEFINES += NTFY_HAVE_NOTIFY

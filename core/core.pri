# Include from projects that link against the static ntfycore library.
INCLUDEPATH += $$PWD
DEPENDPATH += $$PWD
NTFYCORE_BUILD_DIR = $$shadowed($$PWD)
LIBS += -L$$NTFYCORE_BUILD_DIR -lntfycore
PRE_TARGETDEPS += $$NTFYCORE_BUILD_DIR/libntfycore.a
QT += network

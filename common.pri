# Shared settings. Sailfish OS ships Qt 5.6, so stick to APIs available there.
CONFIG += c++11
DEFINES += QT_NO_CAST_FROM_ASCII

isEmpty(VERSION): VERSION = 0.1.0
DEFINES += NTFY_VERSION=\\\"$$VERSION\\\"

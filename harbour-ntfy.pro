TEMPLATE = subdirs

SUBDIRS = core service client cli tests
service.depends = core
client.depends = core
cli.depends = core
tests.depends = core service client

# Sailfish notifications are only available when building for the device.
packagesExist(nemonotifications-qt5) {
    SUBDIRS += notify
    notify.depends = core
    cli.depends += notify
}
packagesExist(sailfishsecrets) {
    SUBDIRS += secrets
    secrets.depends = core
    SUBDIRS += secretscheck
    secretscheck.subdir = tools/secrets-check
    secretscheck.depends = core secrets
}

packagesExist(nemonotifications-qt5):packagesExist(sailfishsecrets) {
    SUBDIRS += daemon
    daemon.depends = core service notify secrets
}

# The Silica UI.
packagesExist(sailfishapp) {
    SUBDIRS += app
    app.depends = core client
}

OTHER_FILES += rpm/harbour-ntfy.spec README.md

license.files = LICENSE
license.path = /usr/share/licenses/harbour-ntfy
INSTALLS += license

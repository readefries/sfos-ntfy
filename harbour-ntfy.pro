TEMPLATE = subdirs

SUBDIRS = core service cli tests
service.depends = core
cli.depends = core
tests.depends = core service

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

OTHER_FILES += rpm/harbour-ntfy.spec README.md

license.files = LICENSE
license.path = /usr/share/licenses/harbour-ntfy
INSTALLS += license

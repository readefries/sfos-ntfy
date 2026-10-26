TEMPLATE = subdirs

SUBDIRS = core cli tests
cli.depends = core
tests.depends = core

OTHER_FILES += rpm/harbour-ntfy.spec README.md

license.files = LICENSE
license.path = /usr/share/licenses/harbour-ntfy
INSTALLS += license

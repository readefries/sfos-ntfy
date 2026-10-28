#!/bin/sh
# Runs semantic-release (configured in .releaserc.json) with pinned versions; arguments are
# passed on, e.g. --dry-run. Used by CI.
#
# The preset's major version must match the changelog writer semantic-release bundles
# (25.x: conventional-changelog-writer 8, so preset 9.x); preset 10.x fails to render notes.
set -eu
exec npx --yes \
    -p semantic-release@25.0.9 \
    -p @semantic-release/exec@7.1.0 \
    -p conventional-changelog-conventionalcommits@9.3.1 \
    semantic-release "$@"

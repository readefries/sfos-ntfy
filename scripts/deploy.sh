#!/bin/sh
# Builds the RPM with the Sailfish SDK (tests included) and copies it to the phone.
#
#   scripts/deploy.sh [ssh-host]        default host: sailfish
#   SFOS_TARGET=SailfishOS-5.1.0.11-armv7hl scripts/deploy.sh
#
# Every build gets its own release (time and git commit), so the phone never mistakes it for one
# that's already installed, and `rpm -q harbour-ntfy` there shows which build it runs.
# Works from the host and from inside the sailfish-sdk distrobox.
set -eu

host=${1:-sailfish}
target=${SFOS_TARGET:-SailfishOS-5.1.0.11-aarch64}
repo=$(cd "$(dirname "$0")/.." && pwd)
# sfdk only accepts paths as it sees them in its workspace: /home/..., not /var/home/...
repo=$(printf '%s' "$repo" | sed 's#^/var/home/#/home/#')
build="$repo/build-${target##*-}"
commit=$(git -C "$repo" rev-parse --short HEAD 2>/dev/null || echo unknown)
dirty=$(git -C "$repo" status --porcelain 2>/dev/null | grep -q . && echo .dirty || true)
suffix=".dev$(date +%Y%m%d%H%M%S).g$commit$dirty"

mkdir -p "$build"
rm -f "$build"/RPMS/harbour-ntfy-*.rpm
sfdk_build="cd '$build' && ~/SailfishOS/bin/sfdk -c target=$target build .. -- --define 'dev_suffix $suffix'"
if [ -n "${CONTAINER_ID:-}" ]; then
    sh -c "$sfdk_build"
else
    distrobox enter sailfish-sdk -- sh -c "$sfdk_build"
fi

# The app and the CLI (a separate package since the Chum split), installed together.
set -- "$build"/RPMS/harbour-ntfy-*.rpm
scp -q "$@" "$host:"
names=
for rpm in "$@"; do
    names="$names ./$(basename "$rpm")"
done
echo
echo "Copied$names to $host. On the phone:"
echo "  devel-su pkcon install-local$names && systemctl --user restart harbour-ntfy-daemon"

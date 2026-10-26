# harbour-ntfy

Native [ntfy](https://ntfy.sh) client for Sailfish OS. Work in progress.

## Layout

| Path    | What |
|---------|------|
| `core/` | Static library: message parsing, server config/auth, `NtfyStream` (long-lived JSON stream with keepalive watchdog, backoff and `since=` resume) and `NtfyPoller` (one-shot `poll=1` catch-up). Qt Core/Network only, so it can be shared by the app and a future daemon. |
| `cli/`  | `harbour-ntfy-cli`, a command line spike to try the core on desktop or on the phone over SSH. |
| `notify/`| Sailfish only: posts messages as system notifications (nemo-qml-plugin-notifications). |
| `secrets/`| Sailfish only: credential store on Sailfish Secrets (device-lock encrypted collection). |
| `tests/`| Qt Test suite, including a fake HTTP server for stream/poll behaviour. |
| `tools/secrets-check/`| Device check for the credential store; built but not packaged, copy it over by hand. |

Note that sailfish-secretsd identifies applications by their full command line, not their executable
(its `/proc/<pid>/exe` lookup always fails), so whatever owns the credentials must always be started
with exactly the same command line.

Sailfish OS ships Qt 5.6: don't use newer Qt APIs. Deprecation warnings when building against
desktop Qt 5.15 (e.g. `endl`) are expected.

## Building

With the Sailfish SDK:

```sh
sfdk config --global --push target SailfishOS-5.1.0.11-aarch64   # see `sfdk tools list`
mkdir -p build-aarch64 && cd build-aarch64
sfdk build ..                     # -> build-aarch64/RPMS/harbour-ntfy-*.aarch64.rpm
```

Run `sfdk` from a path under the SDK workspace as the SDK sees it (`/home/...`, not
`/var/home/...` on Fedora Atomic), or it refuses to build.

`sfdk build` also runs the test suite (`%check` in the spec) against the target's Qt 5.6,
emulated for ARM targets. Pass `--no-check` to skip it.

CI (`.github/workflows/build.yml`) does the same for aarch64, armv7hl and i486 on every push
and pull request, using the community [coderus/sailfishos-platform-sdk](https://hub.docker.com/r/coderus/sailfishos-platform-sdk)
images, and uploads the RPMs as build artifacts.

On a desktop with Qt 5 (e.g. Fedora `qt5-qtbase-devel`):

```sh
mkdir build && cd build
qmake-qt5 ../harbour-ntfy.pro && make -j
./tests/tst_ntfy
```

## Trying it

```sh
harbour-ntfy-cli --verbose mytopic                    # stream
harbour-ntfy-cli --since all --poll mytopic           # fetch cached messages and exit
NTFY_TOKEN=tk_... harbour-ntfy-cli -s https://ntfy.example.com alerts,backups
curl -d "Hello from curl" ntfy.sh/mytopic             # publish something to see it arrive
```

## License

AGPL-3.0-or-later, see [LICENSE](LICENSE).

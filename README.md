# harbour-ntfy

Native [ntfy](https://ntfy.sh) client for Sailfish OS. Work in progress.

## Layout

| Path    | What |
|---------|------|
| `core/` | Static library: message parsing, server config/auth, `NtfyStream` (long-lived JSON stream with keepalive watchdog, backoff and `since=` resume) and `NtfyPoller` (one-shot `poll=1` catch-up). Qt Core/Network only, so it can be shared by the app and a future daemon. |
| `service/`| Everything behind the UI: SQLite storage (servers, subscriptions, history; never credentials) and `NtfySubscriptionManager`, which runs one stream per server and resumes after restarts. |
| `daemon/`| `harbour-ntfy-daemon`: systemd user service (started with the session, or by D-Bus activation) that owns credentials, storage, connections and notifications, and serves the UI on the session bus as `nl.readefries.harbourntfy`. |
| `client/`| The UI's side of the D-Bus API: `NtfyDaemonClient` plus list models for subscriptions and messages. |
| `app/`  | `harbour-ntfy`, the Silica UI. Sandboxed by Sailjail without any permissions: its `OrganizationName.ApplicationName` is the daemon's bus name, which is what lets it talk to the daemon. Note that Sailjail doesn't verify those names, so another sandboxed app claiming them could talk to the daemon too. |
| `cli/`  | `harbour-ntfy-cli`, a command line spike to try the core on desktop or on the phone over SSH. |
| `notify/`| Sailfish only: posts messages as system notifications (nemo-qml-plugin-notifications). |
| `secrets/`| Sailfish only: credential store on Sailfish Secrets (device-lock encrypted collection). |
| `tests/`| Qt Test suites for `core` and `service`, with a fake HTTP ntfy server. |
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

`scripts/deploy.sh` does that and copies the RPM to the phone (ssh host `sailfish` by default). Each of
its builds gets a unique release (`…dev<time>.g<commit>`), so `pkcon install-local` never skips it as
already installed.

`sfdk build` also runs the test suite (`%check` in the spec) against the target's Qt 5.6,
emulated for ARM targets. Pass `--no-check` to skip it.

`scripts/docker-build.sh [release] [arch]` does the same in a community
[coderus/sailfishos-platform-sdk](https://hub.docker.com/r/coderus/sailfishos-platform-sdk) image,
with RPMs in `build-docker/<release>-<arch>/`. CI (`.github/workflows/build.yml`) runs it on every
push and pull request for 5.1.0.11 (aarch64, armv7hl, i486) and 5.2.0.15 (aarch64), and uploads
the RPMs as build artifacts.

5.1.0.11 is the oldest supported release and the newest official SDK target; there is no official
5.2 target yet, so 5.2 is covered by the community image. Both ship Qt 5.6.3.

On a desktop with Qt 5 (e.g. Fedora `qt5-qtbase-devel`):

```sh
mkdir build && cd build
qmake-qt5 ../harbour-ntfy.pro && make -j
./tests/core/tst_ntfy
dbus-run-session -- ./tests/service/tst_service
dbus-run-session -- ./tests/client/tst_client
```

## Trying it

```sh
harbour-ntfy-cli --verbose mytopic                    # stream
harbour-ntfy-cli --since all --poll mytopic           # fetch cached messages and exit
NTFY_TOKEN=tk_... harbour-ntfy-cli -s https://ntfy.example.com alerts,backups
curl -d "Hello from curl" ntfy.sh/mytopic             # publish something to see it arrive
```

## Releasing

Commits follow [Conventional Commits](https://www.conventionalcommits.org/) (`feat:`, `fix:`, `ci:`,
...). After every green build on `main`, CI runs [semantic-release](https://semantic-release.gitbook.io/):
it picks the next version from the commits since the last `vX.Y.Z` tag (`fix:` patch, `feat:` minor,
`BREAKING CHANGE:` major), builds the RPMs for aarch64, armv7hl and i486 with that version, and
creates the tag plus a GitHub release with the notes, the RPMs, `SHA256SUMS` and a build provenance
attestation. It commits nothing; the version in the spec is a placeholder that release builds (and
OBS) replace with the tag's.

### Installing a release from GitHub

Download `harbour-ntfy-<version>-1.<arch>.rpm` for your device (`uname -m`: `aarch64` on recent
devices) and check where it came from before installing it:

```sh
sha256sum -c --ignore-missing SHA256SUMS
gh attestation verify harbour-ntfy-<version>-1.aarch64.rpm -R readefries/sfos-ntfy  # built here, by CI
devel-su pkcon install-local ./harbour-ntfy-<version>-1.aarch64.rpm
```

Packages installed this way don't update by themselves. Note that zypper sticks to a package's
vendor: once installed from GitHub, the SailfishOS:Chum package (vendor `chum`) won't replace it
automatically; uninstall it first if you switch to Chum.

Releases can't be changed afterwards: the repository has GitHub's immutable releases enabled (with
release attestations) and a ruleset that lets `v*` tags be created but not moved or deleted.

### SailfishOS:Chum

Packages for [SailfishOS:Chum](https://github.com/sailfishos-chum/main#readme) are built from source
on the [Sailfish OS OBS](https://build.sailfishos.org/):

1. In your home project, create the package `harbour-ntfy` with this `_service` file. `revision` is
   the **commit hash** of the release, so OBS builds exactly that code even if a tag ever moved;
   `tar_git` still takes the version from the tag on that commit.
   ```xml
   <services>
     <!-- v0.1.0 -->
     <service name="tar_git">
       <param name="url">https://github.com/readefries/sfos-ntfy.git</param>
       <param name="branch">main</param>
       <param name="revision">COMMIT-HASH-OF-THE-RELEASE</param>
     </service>
   </services>
   ```
   Get the hash with `git rev-parse vX.Y.Z^{commit}` and check it against the release's attestation
   on GitHub.
2. Add build targets for the supported Sailfish OS releases and architectures in the project's
   meta configuration (see Chum's [Getting Started](https://github.com/sailfishos-chum/main/blob/main/GettingStarted.md)),
   and check that it builds, tests included.
3. "Submit package" to `sailfishos:chum:testing`. Its maintainers promote it to `sailfishos:chum`.
4. Later releases: update the hash (and comment) in `_service` at `sailfishos:chum:testing`, check
   the build, then "Submit package" to `sailfishos:chum`.

The Chum metadata (title, icon, links, AI use) is the last paragraph of `%description` in
`rpm/harbour-ntfy.spec`.

## License

AGPL-3.0-or-later, see [LICENSE](LICENSE).

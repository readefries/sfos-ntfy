Name:       harbour-ntfy
Summary:    Native ntfy push notification client
# Placeholder: on OBS (and so SailfishOS:Chum) the tar_git service replaces it with the version
# of the newest vX.Y.Z tag, which semantic-release creates. Local builds keep 0.1.0.
Version:    0.1.0
Release:    1%{?dev_suffix}
License:    AGPLv3+
URL:        https://github.com/readefries/sfos-ntfy
Source0:    %{name}-%{version}.tar.bz2

BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Network)
BuildRequires:  pkgconfig(Qt5Sql)
BuildRequires:  pkgconfig(Qt5Test)
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  pkgconfig(nemonotifications-qt5)
BuildRequires:  pkgconfig(sailfishsecrets)
BuildRequires:  pkgconfig(keepalive)
BuildRequires:  pkgconfig(sailfishapp) >= 1.0.2
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  desktop-file-utils
# For the encrypted database tests (run on build).
BuildRequires:  qt5-plugin-sqldriver-sqlcipher
Requires:       sailfishsilica-qt5 >= 0.10.9
Requires:       sailfishsecretsdaemon-secretsplugins-default
Requires:       qt5-plugin-sqldriver-sqlite
Requires:       qt5-plugin-sqldriver-sqlcipher

# The last paragraph of %%description is metadata for SailfishOS:Chum (only shown there), see
# https://github.com/sailfishos-chum/main/blob/main/Metadata.md. It must stay contiguous: no
# empty or comment lines in it, nothing after it.
%description
Native client for ntfy (https://ntfy.sh): subscribe to topics on ntfy.sh or
your own server and get their messages as Sailfish OS notifications.

A background service keeps the subscriptions connected, with a choice
between instant delivery and saving battery. Logins are kept in Sailfish
Secrets and the message history is stored encrypted.

%if 0%{?_chum}
Title: ntfy
Type: desktop-application
DeveloperName: Hendrik Bruinsma
Categories:
 - Network
 - Utility
Custom:
  Repo: https://github.com/readefries/sfos-ntfy
PackageIcon: https://github.com/readefries/sfos-ntfy/raw/main/app/icons/harbour-ntfy.svg
AIRating: C
AINote: Largely written with Claude (Anthropic); design, testing on device and code review by the developer
Links:
  Homepage: https://github.com/readefries/sfos-ntfy
  Bugtracker: https://github.com/readefries/sfos-ntfy/issues
%endif

%package cli
Summary:    Command line ntfy client

%description cli
harbour-ntfy-cli subscribes to ntfy topics and prints incoming messages,
optionally posting them as notifications too. Meant for testing and
debugging; it doesn't use the app's subscriptions or logins.

%prep
%autosetup -n %{name}-%{version}

%build
%qmake5 VERSION=%{version}
%make_build

%check
make check
desktop-file-validate %{buildroot}%{_datadir}/applications/%{name}.desktop || :

%install
%qmake5_install
# Start the daemon with the user session.
mkdir -p %{buildroot}%{_prefix}/lib/systemd/user/post-user-session.target.wants
ln -s ../harbour-ntfy-daemon.service %{buildroot}%{_prefix}/lib/systemd/user/post-user-session.target.wants/

# The daemon runs in the user's session, out of reach of plain systemctl here (we run as root):
# systemctl-user finds that session. Without the restart an update only takes effect after a
# reboot. A fresh install doesn't start it: opening the app does (D-Bus activation).
%post
if [ "$1" -ge 1 ]; then
    systemctl-user daemon-reload || :
    systemctl-user try-restart harbour-ntfy-daemon.service || :
fi

%preun
if [ "$1" -eq 0 ]; then
    systemctl-user stop harbour-ntfy-daemon.service || :
fi

%postun
if [ "$1" -eq 0 ]; then
    systemctl-user daemon-reload || :
fi

%files
%license %{_datadir}/licenses/%{name}/LICENSE
%{_bindir}/%{name}
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
%{_bindir}/%{name}-daemon
%{_prefix}/lib/systemd/user/harbour-ntfy-daemon.service
%{_prefix}/lib/systemd/user/post-user-session.target.wants/harbour-ntfy-daemon.service
%{_datadir}/dbus-1/services/nl.readefries.harbourntfy.service
%{_datadir}/dbus-1/services/nl.readefries.harbourntfy.ui.service

%files cli
%license %{_datadir}/licenses/%{name}/LICENSE
%{_bindir}/%{name}-cli

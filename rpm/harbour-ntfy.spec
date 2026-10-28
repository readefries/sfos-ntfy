Name:       harbour-ntfy
Summary:    ntfy client for Sailfish OS
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
Requires:       sailfishsilica-qt5 >= 0.10.9
Requires:       sailfishsecretsdaemon-secretsplugins-default
Requires:       qt5-plugin-sqldriver-sqlite
Requires:       qt5-plugin-sqldriver-sqlcipher
# For the encrypted database tests (run on build).
BuildRequires:  qt5-plugin-sqldriver-sqlcipher

%description
Native ntfy (https://ntfy.sh) push notification client for Sailfish OS.

harbour-ntfy-daemon keeps subscriptions connected in the background, stores
messages and posts notifications; the harbour-ntfy app is its UI. Also
contains harbour-ntfy-cli, a command line tool to subscribe to topics and
print incoming messages.

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

%files
%license %{_datadir}/licenses/%{name}/LICENSE
%{_bindir}/%{name}
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
%{_bindir}/%{name}-cli
%{_bindir}/%{name}-daemon
%{_prefix}/lib/systemd/user/harbour-ntfy-daemon.service
%{_prefix}/lib/systemd/user/post-user-session.target.wants/harbour-ntfy-daemon.service
%{_datadir}/dbus-1/services/nl.readefries.harbourntfy.service
%{_datadir}/dbus-1/services/nl.readefries.harbourntfy.ui.service

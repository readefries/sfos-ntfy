Name:       harbour-ntfy
Summary:    ntfy client for Sailfish OS
Version:    0.1.0
Release:    1
License:    AGPLv3+
URL:        https://github.com/readefries/sfos-ntfy
Source0:    %{name}-%{version}.tar.bz2

BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Network)
BuildRequires:  pkgconfig(Qt5Test)

%description
Native ntfy (https://ntfy.sh) push notification client for Sailfish OS.

This early build only contains harbour-ntfy-cli, a command line tool to
subscribe to topics and print incoming messages.

%prep
%autosetup -n %{name}-%{version}

%build
%qmake5 VERSION=%{version}
%make_build

%check
make check

%install
%qmake5_install

%files
%license %{_datadir}/licenses/%{name}/LICENSE
%{_bindir}/%{name}-cli

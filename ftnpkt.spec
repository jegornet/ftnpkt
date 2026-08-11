Name:    ftnpkt
Version: 0.1
Release: 1%{?dist}
Summary: Utility for working with FidoNet packet (FTS-0001/FSC-0039/FSC-0048) files
License: Public Domain
URL:     https://github.com/jegornet/ftnpkt
Source0: %{name}-%{version}.tar.gz

BuildRequires: cmake
BuildRequires: gcc
BuildRequires: make
# iconv() is part of glibc on Fedora/RHEL (no separate -devel package needed).
# Static musl builds would instead require libiconv-devel.

%description
ftnpkt creates, appends messages to and dumps FidoNet packet (.pkt) files
conforming to FTS-0001 (Type-2 "Stone Age"), FSC-0039 (Type-2e) and
FSC-0048 (Type-2+). Character set conversion for message bodies and header
string fields is performed with iconv.

%prep
%setup -q

%build
%cmake -DCMAKE_BUILD_TYPE=Release
%cmake_build

%install
%cmake_install

%files
%license %{_docdir}/%{name}/LICENSE
%{_bindir}/%{name}

%changelog
* Fri Jul 31 2026 Jegor - 0.1
- Initial version

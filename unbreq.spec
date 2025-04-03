%global git_ref 9f5295c098912863a205f7e9a318b1980a2a15b9
%global git_short_ref %(echo %{git_ref} | cut -b -7)

Name:           unbreq
Version:        0^20250403.%{git_short_ref}
Release:        %autorelease
Summary:        Mock plugin - detector of unneeded BuildRequires
License:        Apache-2.0
URL:            https://github.com/mkoncek/unbreq

Source0:        https://github.com/mkoncek/unbreq/archive/%{git_ref}.tar.gz#/%{name}-%{version}.tar.gz

BuildRequires:  gcc-c++
BuildRequires:  make

BuildRequires:  python3
BuildRequires:  python3-rpm-macros

BuildRequires:  pkgconfig(rpm)
BuildRequires:  pkgconfig(libdnf5)
BuildRequires:  pkgconfig(libdnf5-cli)

%description
%{summary}.

%prep
%autosetup -p1 -C

%build
%{make_build}

%install
export buildroot=%{buildroot}
export libexecdir=%{_libexecdir}
export python3_sitelib=%{python3_sitelib}

make install

%files
%license LICENSE
%doc README.adoc
%{_libexecdir}/unbreq
%pycached %{python3_sitelib}/mockbuild/plugins/unbreq.py

%changelog
%autochangelog

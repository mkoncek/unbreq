%global git_ref 3712216475faf3d6257c19acc69775a9aabcc21f
%global git_short_ref %(echo %{git_ref} | cut -b -7)

Name:           unbreq
Version:        0^20250327.%{git_short_ref}
Release:        %autorelease
Summary:        Mock plugin - detector of unneeded BuildRequires
License:        Apache-2.0
URL:            https://github.com/mkoncek/unbreq

Source0:        https://github.com/mkoncek/unbreq/archive/%{git_ref}.tar.gz#/%{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  make

BuildRequires:  python3
BuildRequires:  python3-rpm-macros

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

%global git_ref 63926aa04372d9fe196c93c865fb2cda76fd6ef8
%global git_short_ref %(echo %{git_ref} | cut -b -7)

Name:           unbreq
Version:        0^20250918.%{git_short_ref}
Release:        %autorelease
Summary:        Mock plugin - detector of unused BuildRequires
License:        Apache-2.0
URL:            https://github.com/mkoncek/unbreq

Source0:        https://github.com/mkoncek/unbreq/archive/%{git_ref}.tar.gz#/%{name}-%{version}.tar.gz

BuildRequires:  python3
BuildRequires:  python3-rpm-macros

%description
%{summary}.

%prep
%autosetup -p1 -C

%build
%{make_build}

%install
install -m 755 -D -t %{buildroot}%{python3_sitelib}/mockbuild/plugins src/unbreq.py

%files
%license LICENSE
%doc README.adoc
%pycached %{python3_sitelib}/mockbuild/plugins/unbreq.py

%changelog
%autochangelog

%global git_ref c3a1d812c1ca55ac38b6c25d782d3c9253c26e49
%global git_short_ref %(echo %{git_ref} | cut -b -7)

Name:           unbreq
Version:        0^20250918.%{git_short_ref}
Release:        %autorelease
Summary:        Mock plugin - detector of unused BuildRequires
License:        Apache-2.0
URL:            https://github.com/mkoncek/unbreq
BuildArch:      noarch

Source0:        https://github.com/mkoncek/unbreq/archive/%{git_ref}.tar.gz#/%{name}-%{version}.tar.gz

BuildRequires:  python3
BuildRequires:  python3-rpm-macros

%description
%{summary}.

%prep
%autosetup -p1 -C

%install
install -m 755 -D -t %{buildroot}%{python3_sitelib}/mockbuild/plugins src/unbreq.py

%files
%license LICENSE
%doc README.adoc
%pycached %{python3_sitelib}/mockbuild/plugins/unbreq.py

%changelog
%autochangelog

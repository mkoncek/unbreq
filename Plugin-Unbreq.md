---
layout: default
title: Plugin Unbreq
---

Detector of unused `BuildRequires` of RPM builds.

# Usage

> [!Note]
> The current implementation requires that the mock chroot is **not** on a filesystem mounted with the `noatime` option.
> You may need to remount the relevant directory with options `strictatime,lazytime`.

Enable it upon `mock` execution via a flag:
```
mock --enable-plugin=unbreq ...
```

In logs you should see messages like:
```
INFO: enabled unbreq plugin(prebuild)
```

If Unbreq detects an unneeded `BuildRequire` it prints a message like:
```
WARNING: unbreq plugin: the following BuildRequires were not used:
...
```

## Configuration
The mock plugin reads these mock configuration fields for `config_opts`:

`['plugin_conf']['unbreq_opts']['exclude_accessed_files']` : `List[str]`
: A list of regular expressions which are used to ignore file accesses of certain files.
Example: `xmvn` always reads all files inside `/usr/share/maven-metadata/`, the exclusion filter `^/usr/share/maven-metadata/` excludes these files from the listing.
The command line syntax is: `--plugin-option='unbreq:exclude_accessed_files=[${python_regexes...}]'`

## Issues
* Currently does not work with `--isolation=simple`

## How it works
The tool marks a timestamp before executing the build.
After executing the build, relevant files have their access time compared to the saved timestamp.

1. The tool runs `dnf --assumeno remove ${BuildRequire}` for each field in the SRPM file to get the list of RPMs that would be removed along with the `BuildRequire`.
*  Then the tool checks the files owned by all the RPMs, if they were accessed during the build.
*  If no, then the `BuildRequire` is added to the list of removable fields and in the next iteration `dnf` is executed with multiple arguments.

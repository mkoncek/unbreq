#!/usr/bin/python3
# python library imports
import codecs

# our imports
from mockbuild.trace_decorator import getLog, traceLog
import mockbuild.util
import mockbuild.mounts

import rpm
import pathlib
import subprocess
import os
import re

requires_api_version = "1.1"

class AtimeDict(dict):
    def __missing__(self, key):
        result = os.stat(key).st_atime
        self[key] = result
        return result

# plugin entry point
@traceLog()
def init(plugins, conf, buildroot):
    Unbreq(plugins, conf, buildroot)

def get_buildrequires(rpm_file):
    result = list()
    ts = rpm.TransactionSet()
    ts.setFlags(rpm.RPMVSF_NOHDRCHK | rpm.RPMVSF_NOSHA1HEADER | rpm.RPMVSF_NODSAHEADER | rpm.RPMVSF_NORSAHEADER | rpm.RPMVSF_NOMD5 | rpm.RPMVSF_NODSA | rpm.RPMVSF_NORSA)
    try:
        fd = rpm.fd.open(rpm_file)
        h = ts.hdrFromFdno(fd.fileno())
        ds = rpm.ds(h, rpm.RPMTAG_REQUIRENAME)
        for ds in ds:
            name = ds.N()
            if name.startswith("rpmlib(") and name.endswith(")"):
                continue
            br = ds.DNEVR()
            if br[: 2] == "R ":
                result.append(br[2 :])
    finally:
        fd.close()
    return result

class Unbreq(object):
    @traceLog()
    def __init__(self, plugins, conf, buildroot):
        self.buildroot = buildroot
        self.showrc_opts = conf
        self.config = buildroot.config
        self.min_time = None
        self.USE_NSPAWN = mockbuild.util.USE_NSPAWN
        self.exclude_accessed_files = [re.compile(r) for r in self.config.get("plugin_conf", {}).get("unbreq_opts", {}).get("exclude_accessed_files", [])]
        self.accessed_files = AtimeDict()

        plugins.add_hook("prebuild", self._PreBuildHook)
        plugins.add_hook("postbuild", self._PostBuildHook)

    @traceLog()
    def resolve_buildrequires(self):
        if self.USE_NSPAWN:
            chroot_command = ["/usr/bin/systemd-nspawn", "--quiet", "--pipe", "-D", self.buildroot.bootstrap_buildroot.rootdir, "--bind", self.buildroot.rootdir]
        else:
            chroot_command = ["/usr/bin/chroot", self.buildroot.bootstrap_buildroot.rootdir]
        chroot_pkg_manager_command = chroot_command + ["/usr/bin/" + self.buildroot.pkg_manager.name, "--installroot", self.buildroot.rootdir]
        srpm_dir = pathlib.Path(self.buildroot.rootdir + os.path.join(self.buildroot.builddir, "SRPMS"))

        def get_files(packages):
            if len(packages) == 0:
                return list()
            process = subprocess.run(chroot_command + ["/usr/bin/rpm", "--root", self.buildroot.rootdir, "-ql"] + packages,
                stdin = subprocess.DEVNULL, stdout = subprocess.PIPE, stderr = subprocess.PIPE,
            )
            if process.returncode != 0:
                raise RuntimeError("process {} returned {}: {}".format(
                    process.args, process.returncode, process.stderr.decode("utf-8").rstrip()
                ))
            else:
                return process.stdout.decode("ascii").splitlines()

        br_providers = dict()
        rev_br_providers = dict()
        for srpm in srpm_dir.iterdir():
            for br in get_buildrequires(str(srpm)):
                process = subprocess.run(
                    chroot_pkg_manager_command + ["repoquery", "--installed", "--whatprovides", br],
                    stdin = subprocess.DEVNULL, stdout = subprocess.PIPE, stderr = subprocess.PIPE,
                )
                if process.returncode != 0:
                    raise RuntimeError("process {} returned {}: {}".format(
                        process.args, process.returncode, process.stderr.decode("utf-8").strip()
                    ))
                br_providers_br = process.stdout.decode("ascii").splitlines()
                br_providers[br] = br_providers_br
                for provider in br_providers_br:
                    rev_br_providers.setdefault(provider, list()).append(br)
        # attempt to resolve providers so that each BR is provided by only one provider
        sorted_br_providers = sorted(br_providers, key = lambda k: len(br_providers[k]))
        if len(sorted_br_providers) != 0 and len(sorted_br_providers[-1]) > 1:
            for br in sorted_br_providers:
                br_providers_br = br_providers[br]
                if len(br_providers_br) == 1:
                    for rev_br in rev_br_providers[br_providers_br[0]]:
                        if rev_br != br:
                            br_providers_rev_br = br_providers[rev_br]
                            if len(br_providers_rev_br) > 1:
                                try:
                                    br_providers_rev_br.remove(br_providers_br[0])
                                except ValueError:
                                    pass

################################################################################

        brs_can_be_removed = list()
        for br, providers in br_providers.items():
            process = subprocess.run(chroot_pkg_manager_command + ["--assumeno", "remove"] + [v for vs in brs_can_be_removed for v in vs[1]] + providers,
                stdin = subprocess.DEVNULL, stdout = subprocess.PIPE, stderr = subprocess.PIPE,
            )
            if process.returncode != 1:
                raise RuntimeError("process {} returned {}: {}".format(
                    process.args, process.returncode, process.stderr.decode("utf-8").rstrip()
                ))
            removed_packages = list()
            for line in process.stdout.decode("ascii").splitlines():
                if not line.startswith(" "):
                    continue
                nvr = line.split()
                if len(nvr) != 6:
                    continue
                nvr = nvr[0] + "-" + nvr[2] + "." + nvr[1]
                removed_packages.append(nvr)
            can_be_removed = True
            for path in get_files(removed_packages):
                path = self.buildroot.rootdir + path
                try:
                    atime = self.accessed_files[path]
                except FileNotFoundError:
                    continue
                if atime > self.min_time:
                    short_path = path[len(self.buildroot.rootdir):]
                    skip = False
                    for r in self.exclude_accessed_files:
                        if r.search(short_path) is not None:
                            skip = True
                            break
                    else:
                        can_be_removed = False
                        break
            if can_be_removed:
                brs_can_be_removed.append((br, providers))
        if len(brs_can_be_removed) != 0:
            brs = list(map(lambda t: t[0], brs_can_be_removed))
            getLog().warning("unbreq plugin: the following BuildRequires were not used:\n\t%s", "\n\t".join(brs))

    @traceLog()
    def _PreBuildHook(self):
        getLog().info("enabled unbreq plugin (prebuild)")
        # NOTE maybe find a better example file to touch to get an atime?
        path = os.path.join(self.buildroot.rootdir, "dev", "null")
        subprocess.run(["touch", path], check = True)
        self.min_time = os.stat(path).st_atime

    @traceLog()
    def _PostBuildHook(self):
        if self.buildroot.state.result != "success":
            return
        getLog().info("enabled unbreq plugin (postbuild)")

        try:
            mount_options_process = subprocess.run(["findmnt", "-n", "-o", "OPTIONS", "--target", self.buildroot.rootdir], check = True, text = True, capture_output = True)
            if mount_options_process:
                for option in mount_options_process.stdout.rstrip().split(","):
                    if option == "relatime" or option == "noatime":
                        getLog().warning("unbreq plugin: chroot %s is on a filesystem mounted with the '%s' option; detection will not work correctly, you may want to remount the proper directory with mount options 'strictatime,lazytime'", self.buildroot.rootdir, option)
        except FileNotFoundError:
            pass

        if self.USE_NSPAWN:
            self.resolve_buildrequires()
        else:
            with mockbuild.mounts.BindMountPoint(self.buildroot.rootdir,
                self.buildroot.bootstrap_buildroot.make_chroot_path(self.buildroot.rootdir)).having_mounted():
                self.resolve_buildrequires()

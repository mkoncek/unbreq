#!/usr/bin/python3
# python library imports
import codecs

# our imports
from mockbuild.trace_decorator import getLog, traceLog
import mockbuild.util

import rpm
import pathlib
import subprocess
import os
from concurrent.futures import ThreadPoolExecutor

requires_api_version = "1.1"

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

class Mounted_root:
    def __init__(self, mountpoint, target):
        self.mountpoint = mountpoint
        self.target = target
    def __enter__(self):
        p = subprocess.run(["umount", self.mountpoint], capture_output = True)
        subprocess.run(["rm", "-rf", self.mountpoint])
        os.mkdir(self.mountpoint)
        subprocess.run(["mount", "--bind", self.target, self.mountpoint])
    def __exit__(self, exc_type, exc_value, exc_traceback):
        subprocess.run(["umount", self.mountpoint])
        subprocess.run(["rm", "-rf", self.mountpoint])

class Unbreq(object):
    @traceLog()
    def __init__(self, plugins, conf, buildroot):
        self.buildroot = buildroot
        self.showrc_opts = conf
        self.config = buildroot.config

        self.files_output = None
        self.unbreq_process = None

        plugins.add_hook("prebuild", self._PreBuildHook)
        plugins.add_hook("postbuild", self._PostBuildHook)

    @traceLog()
    def resolve_buildrequires(self):
        chroot_command = ["chroot", self.buildroot.bootstrap_buildroot.rootdir]
        chroot_dnf_command = chroot_command + ["/usr/bin/dnf", "--installroot=/mnt/root"]
        srpm_dir = pathlib.Path(self.buildroot.rootdir + os.path.join(self.buildroot.builddir, "SRPMS"))

        def get_files(packages):
            process = subprocess.run(chroot_command + ["/usr/bin/rpm", "--root", "/mnt/root", "-ql"] + packages, capture_output = True)
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
                br_providers[br] = None
        for br in br_providers.keys():
            br_providers[br] = subprocess.Popen(
                chroot_dnf_command + ["repoquery", "--installed", "--whatprovides", br],
                stdout = subprocess.PIPE, stderr = subprocess.PIPE,
            )
        for br, providers_process in br_providers.items():
            with providers_process as providers_process:
                stdout, stderr = providers_process.communicate()
                if providers_process.wait() != 0:
                    raise RuntimeError("process {} returned {}: {}".format(
                        providers_process.args, providers_process.returncode, stderr.decode("utf-8").strip()
                    ))
                br_providers_br = stdout.decode("ascii").splitlines()
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
                                    br_providers[rev_br].remove(br_providers_br[0])
                                except ValueError:
                                    pass

################################################################################

        brs_can_be_removed = list()
        for br, providers in br_providers.items():
            process = subprocess.run(chroot_dnf_command + ["--assumeno", "remove"] + brs_can_be_removed + providers, capture_output = True)
            if process.returncode != 1:
                raise("process {} returned {}: {}".format(
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
                if path in self.accessed_files:
                    can_be_removed = False
            if can_be_removed:
                brs_can_be_removed.append(br)
        if len(brs_can_be_removed) != 0:
            getLog().warning("Unbreq plugin: the following BuildRequires were not used: {}", ", ".join(brs_can_be_removed))
            print("Unbreq plugin: the following BuildRequires were not used: {}".format(", ".join(brs_can_be_removed)))

    @traceLog()
    def _PreBuildHook(self):
        getLog().info("enabled unbreq plugin (prebuild)")
        self.files_output = os.memfd_create("accessed_files", 0)
        self.unbreq_process = subprocess.Popen(
            ["/usr/libexec/unbreq", self.buildroot.rootdir, str(self.files_output)],
            stdin = subprocess.PIPE, stdout = subprocess.PIPE, stderr = subprocess.PIPE,
            pass_fds = [self.files_output],
        )
        line = self.unbreq_process.stderr.readline()
        if line != b"[INFO] fanotify running...\n":
            getLog().error("Unbreq plugin: unexpected message: {}", line.decode("utf-8").rstrip())

    # TODO enable only for successful builds
    @traceLog()
    def _PostBuildHook(self):
        getLog().info("enabled unbreq plugin (postbuild)")
        if self.unbreq_process is None:
            return
        with self.unbreq_process as unbreq_process:
            stdout, stderr = unbreq_process.communicate("")
            if unbreq_process.wait() != 0:
                getLog().error("Unbreq plugin: process {} returned {}: {}",
                    unbreq_process.args, unbreq_process.returncode, stderr.decode("utf-8").rstrip()
                )
            else:
                stderr = stderr.decode("utf-8").rstrip()
                if len(stderr) != 0:
                    getLog().warning("Unbreq plugin: process {}: {}",
                        unbreq_process.args, stderr
                    )
        os.fsync(self.files_output)
        os.lseek(self.files_output, 0, os.SEEK_SET)
        self.accessed_files = set()
        with os.fdopen(self.files_output, "r") as accessed_files_stream:
            for line in accessed_files_stream:
                self.accessed_files.add(line.strip())

        mounted_root = os.path.join(self.buildroot.bootstrap_buildroot.rootdir, "mnt/root")
        with Mounted_root(mounted_root, self.buildroot.rootdir):
            self.resolve_buildrequires()

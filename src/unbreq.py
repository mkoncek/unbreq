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

class Unbreq(object):
    @traceLog()
    def __init__(self, plugins, conf, buildroot):
        self.buildroot = buildroot
        self.showrc_opts = conf
        self.config = buildroot.config

        self.files_output = None
        self.unbreq_process = None

        self.USE_NSPAWN = mockbuild.util.USE_NSPAWN
        
        self.exclude_accessed_files = self.config.get("plugin_conf", {}).get("unbreq_exclude_accessed_files", [])
        plugins.add_hook("prebuild", self._PreBuildHook)
        plugins.add_hook("postbuild", self._PostBuildHook)

    @traceLog()
    def resolve_buildrequires(self):
        if self.USE_NSPAWN:
            chroot_command = ["/usr/bin/systemd-nspawn", "--quiet", "--pipe", "-D", self.buildroot.bootstrap_buildroot.rootdir, "--bind", self.buildroot.rootdir]
        else:
            chroot_command = ["/usr/bin/chroot", self.buildroot.bootstrap_buildroot.rootdir]
        chroot_dnf_command = chroot_command + ["/usr/bin/dnf", "--installroot", self.buildroot.rootdir]
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
                    chroot_dnf_command + ["repoquery", "--installed", "--whatprovides", br],
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
            process = subprocess.run(chroot_dnf_command + ["--assumeno", "remove"] + [v for vs in brs_can_be_removed for v in vs[1]] + providers,
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
                if path in self.accessed_files:
                    can_be_removed = False
                    break
            if can_be_removed:
                brs_can_be_removed.append((br, providers))
        if len(brs_can_be_removed) != 0:
            brs = list(map(lambda t: t[0], brs_can_be_removed))
            getLog().warning("unbreq plugin: the following BuildRequires were not used: %s", ", ".join(brs))

    @traceLog()
    def _PreBuildHook(self):
        getLog().info("enabled unbreq plugin (prebuild)")
        self.files_output = os.memfd_create("accessed_files", 0)
        exclude_accessed_files_flags = [elem for regex in self.exclude_accessed_files for elem in ("-e", regex)]
        self.unbreq_process = subprocess.Popen(
            ["/usr/libexec/unbreq/fanotify", self.buildroot.rootdir, str(self.files_output)] + exclude_accessed_files_flags,
            stdin = subprocess.PIPE, stdout = subprocess.PIPE, stderr = subprocess.PIPE,
            pass_fds = [self.files_output],
        )
        getLog().info("unbreq plugin: started process:\n%s", self.unbreq_process.args)
        line = self.unbreq_process.stderr.readline()
        if line != b"INFO: ready to monitor file accesses...\n":
            getLog().error("unbreq plugin: unexpected message: %s", line.decode("utf-8").rstrip())

    # TODO enable only for successful builds
    @traceLog()
    def _PostBuildHook(self):
        getLog().info("enabled unbreq plugin (postbuild)")
        if self.unbreq_process is None:
            return
        with self.unbreq_process as unbreq_process:
            stdout, stderr = unbreq_process.communicate("")
            if unbreq_process.wait() != 0:
                getLog().error("unbreq plugin: process %s returned %s: %s",
                    unbreq_process.args, unbreq_process.returncode, stderr.decode("utf-8").rstrip(),
                )
            else:
                stderr = stderr.decode("utf-8").rstrip()
                if len(stderr) != 0:
                    getLog().warning("unbreq plugin: process %s: %s",
                        unbreq_process.args, stderr,
                    )

        process = subprocess.run(
            [
                "/usr/libexec/unbreq/resolve",
                str(self.files_output),
                self.buildroot.rootdir + os.path.join(self.buildroot.builddir, "SRPMS"),
                self.buildroot.rootdir,
            ],
            capture_output = True,
            pass_fds = [self.files_output],
        )
        if process.returncode == 0:
            brs = process.stdout.decode("utf-8").splitlines()
            if len(brs) > 0:
                getLog().warning("unbreq plugin: the following BuildRequires were not used: \n\t%s", "\n\t".join(brs))
        else:
            getLog().error("unbreq resolution failed with %s, command was:\n%s:\n%s",
                process.returncode, process.args, process.stderr.decode("utf-8").rstrip(),
            )

        # self.accessed_files = set()
        # with os.fdopen(self.files_output, "r") as accessed_files_stream:
        #     for line in accessed_files_stream:
        #         self.accessed_files.add(line.strip())
        # if self.USE_NSPAWN:
        #     self.resolve_buildrequires()
        # else:
        #     with mockbuild.mounts.BindMountPoint(self.buildroot.rootdir,
        #         self.buildroot.bootstrap_buildroot.make_chroot_path(self.buildroot.rootdir)).having_mounted():
        #         self.resolve_buildrequires()

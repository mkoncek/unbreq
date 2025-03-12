#!/usr/bin/python3

import rpm
import pathlib
import subprocess

brs = []
ts = rpm.TransactionSet()
ts.setFlags(rpm.RPMVSF_NOHDRCHK | rpm.RPMVSF_NOSHA1HEADER | rpm.RPMVSF_NODSAHEADER | rpm.RPMVSF_NORSAHEADER | rpm.RPMVSF_NOMD5 | rpm.RPMVSF_NODSA | rpm.RPMVSF_NORSA)
# srpm = next(pathlib.Path("/var/lib/mock/fedora-rawhide-x86_64/root/builddir/build/SRPMS").iterdir())
# srpm = "/home/mkoncek/Downloads/xerces-j2-2.12.2-10.el10.src.rpm"
# srpm = "/home/mkoncek/Downloads/javapackages-tools-6.4.0-4.fc42.src.rpm"
srpm = "/home/mkoncek/Downloads/javapackages-local-openjdk21-6.4.0-4.fc42.noarch.rpm"
try:
    fd = rpm.fd.open(srpm)
    h = ts.hdrFromFdno(fd.fileno())
    ds = rpm.ds(h, rpm.RPMTAG_REQUIRENAME)
    for ds in ds:
        name = ds.N()
        if name.startswith("rpmlib(") and name.endswith(")"):
            continue
        br = ds.DNEVR()
        if br[: 2] == "R ":
            brs.append(br[2 :])
finally:
    fd.close()

for br in brs:
    print(br)
    whatprovides = subprocess.run(["/var/lib/mock/fedora-rawhide-x86_64-bootstrap/root/usr/bin/dnf", "--installroot=/var/lib/mock/fedora-rawhide-x86_64/root", "repoquery", "--installed", "--whatprovides", br], capture_output = True)
    whatprovides = whatprovides.stdout.decode("ascii").split("\n")[: -1]
    print(whatprovides)
    
# ps = subprocess.run(["/var/lib/mock/fedora-rawhide-x86_64-bootstrap/root/usr/bin/dnf", "--installroot=/var/lib/mock/fedora-rawhide-x86_64/root", "repoquery", "--installed", "--requires", "junit-1:4.13.2-7.fc41.noarch")

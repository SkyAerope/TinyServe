#!/bin/sh
# SPDX-License-Identifier: MIT
# Build a Debian-quality source package for TinyServe and run the
# full QA stack expected by mentors.debian.net / ftp-master.
#
# Run on a Debian unstable (sid) chroot or a sid system. On Debian
# stable you'll get older lintian/debhelper than ftp-master uses.
#
# Required packages:
#   sudo apt install build-essential devscripts debhelper dh-make \
#       lintian fakeroot pristine-tar git-buildpackage \
#       sbuild piuparts autopkgtest blhc
#
# Outputs land in ../tinyserve_<ver>.* (parent of the source tree).

set -eu

cd "$(dirname "$0")/.."
SRCDIR="$PWD"
TAG=$(dpkg-parsechangelog -SVersion | sed 's/-.*//')
PKG=$(dpkg-parsechangelog -SSource)

echo ">>> packaging ${PKG} ${TAG}"

# ── 1. Generate a pristine upstream tarball from the v<ver> tag.
#    Strip debian/ (which lives in the same git tree) so the tarball
#    is genuine upstream-only — mandatory for 3.0 (quilt) format.
ORIG="../${PKG}_${TAG}.orig.tar.gz"
if [ ! -f "$ORIG" ]; then
    echo ">>> creating ${ORIG} from git tag v${TAG} (excluding debian/)"
    TMP=$(mktemp -d)
    git archive --format=tar --prefix="${PKG}-${TAG}/" "v${TAG}" \
        | tar -x -C "$TMP"
    rm -rf "$TMP/${PKG}-${TAG}/debian"
    ( cd "$TMP" && tar -czf "$OLDPWD/$ORIG" "${PKG}-${TAG}" )
    rm -rf "$TMP"
fi

# ── 2. Build source + binary packages.
echo ">>> dpkg-buildpackage (source + binary)"
rm -rf debian/.debhelper debian/files debian/${PKG} \
       debian/tmp debian/debhelper-build-stamp \
       ../${PKG}_${TAG}-*.changes ../${PKG}_${TAG}-*.buildinfo \
       ../${PKG}_${TAG}-*.deb     ../${PKG}_${TAG}-*.dsc       \
       ../${PKG}_${TAG}-*.debian.tar.* ../${PKG}_${TAG}-*_source.changes
dpkg-buildpackage -us -uc -F   # full: source + binary

# ── 3. lintian on the .changes (catches more than .deb alone).
echo
echo ">>> lintian"
lintian -EvIL +pedantic --pedantic ../${PKG}_${TAG}-*_*.changes || \
    echo "!!! lintian found issues, review above"

# ── 4. blhc (build-log hardening checker).
if command -v blhc >/dev/null 2>&1; then
    BLOG=$(ls -1t ../${PKG}_${TAG}-*.build 2>/dev/null | head -1 || true)
    if [ -n "$BLOG" ]; then
        echo
        echo ">>> blhc on $BLOG"
        blhc --all "$BLOG" || true
    fi
fi

# ── 5. piuparts (install / upgrade / removal QA).
if command -v piuparts >/dev/null 2>&1; then
    echo
    echo ">>> piuparts (needs sudo)"
    sudo piuparts -d unstable --no-eatmydata \
        ../${PKG}_${TAG}-*_amd64.deb || \
        echo "!!! piuparts reported issues"
fi

# ── 6. autopkgtest (the inline smoke test).
if command -v autopkgtest >/dev/null 2>&1; then
    echo
    echo ">>> autopkgtest (null runner)"
    autopkgtest ../${PKG}_${TAG}-*_amd64.deb -- null || \
        echo "!!! autopkgtest failed"
fi

# ── 7. sbuild (clean unstable chroot rebuild).
if command -v sbuild >/dev/null 2>&1; then
    echo
    echo ">>> sbuild against sid (will skip if no sid chroot)"
    sbuild -d unstable --arch=amd64 ../${PKG}_${TAG}-1.dsc || \
        echo "!!! sbuild failed (chroot missing? run mk-sbuild unstable)"
fi

echo
echo ">>> All artefacts:"
ls -la ../${PKG}_${TAG}*

cat <<EOF

Next steps:
  1. Replace #XXXXXX in debian/changelog with the real ITP bug number.
  2. dput mentors ../${PKG}_${TAG}-1_source.changes
  3. Send the RFS email — see debian/RFS_TEMPLATE.txt
EOF

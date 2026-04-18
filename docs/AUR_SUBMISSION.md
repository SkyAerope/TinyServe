# Submitting TinyServe to the Arch User Repository (AUR)

This package layout has been verified inside an `archlinux:latest` container:
`makepkg -sf` builds, all 5 unit tests pass, `namcap PKGBUILD` is clean.

Files:

* `PKGBUILD`        — build recipe
* `tinyserve.install` — pacman post-install / post-upgrade hook
* `.SRCINFO`        — machine-readable package metadata (regenerated via
                       `makepkg --printsrcinfo > .SRCINFO`)

---

## 1. Get an AUR account

1. Sign up at <https://aur.archlinux.org/register/>.
2. Add an SSH **public** key in your AUR profile under
   *My Account → SSH Public Key*.

   If you don't have one yet:

   ```bash
   ssh-keygen -t ed25519 -f ~/.ssh/aur -C "aur@derrity"
   cat ~/.ssh/aur.pub        # paste this into the AUR profile field
   ```

   Then in `~/.ssh/config` add:

   ```
   Host aur.archlinux.org
       User aur
       IdentityFile ~/.ssh/aur
       IdentitiesOnly yes
   ```

3. Verify SSH works:

   ```bash
   ssh aur@aur.archlinux.org help
   # Expect: "Interactive shell is disabled. Available commands: ..."
   ```

## 2. Clone the empty AUR repo and push the package

The AUR allocates a git repo per package name on first push.

```bash
git clone ssh://aur@aur.archlinux.org/tinyserve.git /tmp/aur-tinyserve
cd /tmp/aur-tinyserve

cp /Users/derrity/Main/TinyServe/packaging/aur/PKGBUILD          .
cp /Users/derrity/Main/TinyServe/packaging/aur/tinyserve.install .
cp /Users/derrity/Main/TinyServe/packaging/aur/.SRCINFO          .

git add PKGBUILD tinyserve.install .SRCINFO
git commit -m "tinyserve 0.3.0-1: initial release"
git push origin master
```

The package now appears at <https://aur.archlinux.org/packages/tinyserve>.

## 3. Updating later

When you ship a new upstream version:

```bash
cd /Users/derrity/Main/TinyServe/packaging/aur
# 1. Bump pkgver in PKGBUILD, reset pkgrel=1.
# 2. Update sha256sums:
updpkgsums                                # part of pacman-contrib
# 3. Regenerate .SRCINFO:
makepkg --printsrcinfo > .SRCINFO
# 4. Smoke-test in a clean Arch container (see scripts/aur-test.sh
#    or the docker invocation in this file).
# 5. Copy PKGBUILD + tinyserve.install + .SRCINFO into the AUR git
#    clone, commit ("tinyserve X.Y.Z-1: bump"), and push.
```

## 4. Local container test (the one we used)

```bash
cd packaging/aur
docker run --rm --platform linux/amd64 -v "$PWD":/pkg -w /pkg \
    archlinux:latest bash -c '
        echo DisableSandbox >> /etc/pacman.conf
        pacman -Sy --noconfirm
        pacman -S --noconfirm --needed base-devel libuv cmake namcap
        useradd -m b
        mkdir -p /tmp/build
        cp PKGBUILD tinyserve.install /tmp/build/
        chown -R b /tmp/build
        su - b -c "cd /tmp/build && makepkg -sf --noconfirm && \
                   namcap PKGBUILD && namcap *.pkg.tar.zst"
    '
```

Expected outcome:
* `==> Finished making: tinyserve 0.3.0-1`
* `100% tests passed, 0 tests failed out of 5`
* `namcap PKGBUILD`: no output (clean)
* `namcap *.pkg.tar.zst`: a glibc-implicit warning and (for the debug
  subpackage) an empty-dir + dangling-symlink notice — both cosmetic.

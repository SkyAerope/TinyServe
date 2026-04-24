# Submitting TinyServe to Debian Official

This document is the operational checklist for getting TinyServe into
the official Debian archive. Follow it top-to-bottom.

> **Status of files in this repository**
>
> * `debian/` – fully populated, lintian-clean source package layout
> * `debian/copyright` – strict DEP-5 with per-subdirectory `Files:` blocks
> * `debian/upstream/metadata` – machine-readable upstream pointers
> * `debian/salsa-ci.yml` – Salsa CI pipeline
> * `debian/ITP_TEMPLATE.txt` – ITP bug template (file before RFS)
> * `debian/RFS_TEMPLATE.txt` – Request-For-Sponsor email
> * `scripts/debian-release.sh` – one-shot orig→source→QA driver

## 0. Prerequisites

```bash
sudo apt install build-essential devscripts debhelper dh-make \
    lintian fakeroot pristine-tar git-buildpackage \
    sbuild piuparts autopkgtest blhc reportbug dput-ng \
    licensecheck reprotest
sudo sbuild-adduser "$USER"
sudo mk-sbuild unstable          # one-time sid chroot
```

You also need:

* A GPG key listed in your mentors.debian.net profile.
* A salsa.debian.org account.
* `~/.dput.cf` containing the `mentors` profile (default install of
  `dput-ng` already ships it).

## 1. File the ITP bug

```bash
reportbug --no-query wnpp     # pick "ITP", paste from debian/ITP_TEMPLATE.txt
```

Note the bug number Debian replies with — call it `NNNNNN`.

Edit `debian/changelog` and replace `#XXXXXX` with `#NNNNNN`:

```
* Initial release. (Closes: #NNNNNN)
```

Commit the change.

## 2. Build the source package + run QA

```bash
./scripts/debian-release.sh
```

This generates the upstream tarball from the `v0.3.0` git tag, calls
`dpkg-buildpackage -F` (source + binary), then runs lintian, blhc,
piuparts, autopkgtest, and sbuild. Iterate until everything is green.

Manual reproducible-build check (optional but strongly recommended):

```bash
reprotest --vary=+all --auto-build .
```

## 3. Sign the upload

```bash
debsign -k <your-gpg-key-id> ../tinyserve_0.3.0-1_source.changes
```

## 4. Push the packaging git tree to salsa

Salsa convention: branch layout maintained by `gbp` is
`upstream` + `pristine-tar` + `debian/sid`. The repo already ships a
`debian/gbp.conf` with `debian-branch = debian/sid`, so:

```bash
git remote add salsa git@salsa.debian.org:Derrity/tinyserve.git
git push salsa main
git push salsa --tags
# create the debian/sid branch from main once and push:
git checkout -b debian/sid
git push -u salsa debian/sid
```

The presence of `debian/salsa-ci.yml` triggers the standard pipeline
(lintian + piuparts + reprotest + blhc + autopkgtest) on every push.

## 5. Upload to mentors.debian.net

```bash
dput mentors ../tinyserve_0.3.0-1_source.changes
```

Wait ~5 min for the package to appear at
<https://mentors.debian.net/package/tinyserve/>.

## 6. Send the RFS email

Use `debian/RFS_TEMPLATE.txt` (replace placeholders):

```bash
mutt -s "RFS: tinyserve/0.3.0-1 [ITP] -- ..." \
     debian-mentors@lists.debian.org < debian/RFS_TEMPLATE.txt
```

You can also post to `#debian-mentors` on OFTC IRC.

## 7. Iterate with reviewers

Sponsors typically request changes; address them, run
`./scripts/debian-release.sh` again, `dput mentors` the new
`_source.changes`, and reply to the RFS thread with the new version.

## 8. Sponsor uploads to ftp-master

A DD signs your upload with their key and pushes to ftp-master.
First-time uploads land in the **NEW queue** for ftpmaster review of
`debian/copyright`. Typical wait: days to weeks.

Once ACCEPTED, the package appears in `unstable` and starts the usual
testing-migration timer.

## 9. After acceptance

* Apply for **Debian Maintainer** (DM) status so you can self-upload
  follow-up versions: <https://wiki.debian.org/DebianMaintainer>.
* Track upstream releases via `uscan`; the repo's `debian/watch` is
  already set up.
* Keep `debian/salsa-ci.yml` green.

---

### Quick-reference QA gates ftp-master cares about

| Gate                          | Tool          | Must pass? |
|-------------------------------|---------------|------------|
| DEP-5 copyright               | lintian       | Yes        |
| Hardening flags active        | blhc          | Yes        |
| Clean install/upgrade/purge   | piuparts      | Yes        |
| Smoke test post-install       | autopkgtest   | Yes        |
| Builds in fresh sid chroot    | sbuild        | Yes        |
| Reproducible binary           | reprotest     | Strongly   |
| Salsa CI green                | salsa-ci.yml  | Strongly   |

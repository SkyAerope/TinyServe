# Changelog

All notable changes to TinyServe are recorded here. The format is loosely
based on [Keep a Changelog](https://keepachangelog.com/) and the project
adheres to [Semantic Versioning](https://semver.org/).

## 0.3.1 — 2026-04-25

### Added
- AUR submission kit under `packaging/aur/` (PKGBUILD, .SRCINFO,
  install hook) verified against `archlinux:latest` (5/5 tests pass
  inside `makepkg -sf`, namcap PKGBUILD clean).
- `docs/AUR_SUBMISSION.md` and `docs/DEBIAN_SUBMISSION.md`
  operational checklists.
- Strict DEP-5 `debian/copyright`, `debian/upstream/metadata`,
  Salsa CI pipeline (`.gitlab-ci.yml` + `debian/salsa-ci.yml`),
  ITP / RFS email templates, and a `scripts/debian-release.sh`
  one-shot orig + source + lintian + piuparts + autopkgtest +
  sbuild driver.

### Fixed
- `src/proxy.c`: clear the `write_done_cb` self-pointer before the
  uv_close handle teardown to silence GCC `-Wuse-after-free`.

### Documentation
- README / README_CN: document the `-j` (workers) and `-n`
  (max-connections) flags.

## 0.3.0 — 2026-01-23

### Added
- `-j N` flag: run with N worker processes using SO_REUSEPORT load
  balancing on Linux (silently demoted to 1 worker on macOS).
- `-n N` flag: cap the number of concurrent connections; excess
  connections are rejected with 503.
- ETag and `Last-Modified` on file responses; honors
  `If-None-Match` and `If-Modified-Since` with a proper 304.
- `Date` and `Server` headers on every response.
- `tinyserve(1)` man page.
- Unit tests under CTest (`tests/test_*.c`) and an autopkgtest
  smoke test under `debian/tests/`.
- Full `debian/` packaging: hardened systemd unit, DEP-5 copyright,
  uscan watch file, lintian-clean .deb.
- GitHub Actions CI matrix on ubuntu-22.04 / 24.04 (build + ctest +
  debuild + lintian).

### Changed
- Listen backlog raised to `SOMAXCONN`.
- Per-connection fixed read buffer (no more 64 KiB malloc churn).
- Directory listings produced asynchronously via `uv_queue_work`;
  realloc paths are overflow-checked and capped at 16 MiB.
- File send is zero-copy: the same buffer is used for read and write.
- Multipart Range boundaries use `getrandom(2)` /
  `arc4random_buf(3)` / `/dev/urandom`, never `rand()`.
- Release builds enable `_FORTIFY_SOURCE=2`, stack-protector-strong,
  PIE, RELRO, BIND_NOW, stack-clash-protection (Linux).

### Security
- Reject CL+TE smuggling (`Content-Length` + `Transfer-Encoding`).
- Reject non-RFC-7230 *tchar* bytes in header field-names.
- Reject NUL / CR / LF in the request-target.
- Sanitize control bytes in log output.
- Symlink-escape protection via `realpath(3)` + root-prefix check.
- 431 on oversized request headers (distinct from 400).

### Fixed
- Idle keep-alive and request-read timeouts now actually fire and
  close the connection cleanly, with timer / TCP / pending-work
  handles all reaped via a per-client close counter (no
  use-after-free).
- HTTP parser correctly resumes at the saved offset when a header
  arrives in two TCP chunks.

## 0.1.0 — 2025-11-15

- Initial public release: file server, stub HTTP, TCP port-forward
  proxy, basic-auth + header-auth, single libuv loop.

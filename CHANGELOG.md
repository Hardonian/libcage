# Changelog

All notable changes to Libcage are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/), and this project adheres to
[Semantic Versioning](https://semver.org/).

## [0.1.0] - 2026-08-16

Initial public release.

### Added
- **Core agent** — pure-C (C11) autonomous code-repair loop. Reads a prompt +
  target file, sends to any OpenAI-compatible `/chat/completions` endpoint,
  parses the corrected file (or unified diff), writes it, compiles, tests, and
  loops up to `LIBCAGE_MAX_ITER` times on failure. Zero dependencies: libc +
  POSIX sockets only. (~21KB binary.)
- **Model compatibility** — works with Ollama, OpenAI, or any `/v1/chat/completions`
  endpoint via `LIBCAGE_API_BASE` / `LIBCAGE_API_KEY` / `LIBCAGE_MODEL`.
- **`--sbom`** (Pro) — emits a CycloneDX 1.5 SBOM with SHA-256 of the source and
  compiler version. Hand-rolled SHA-256, no OpenSSL.
- **`--policy FILE`** (Pro) — endpoint allowlist; calls to non-allowed hosts
  fail closed.
- **`--team N` + `--audit-log FILE`** (Team) — multi-seat license with a
  tamper-evident HMAC-SHA256-chained audit log of every session and repair.
- **`make test`** — self-repair self-test that asserts the agent produces a
  working binary (re-compiles + runs, requires expected output).
- **GitHub release pipeline** — `.github/workflows/release.yml` builds
  `linux-amd64` + `macos-amd64` static binaries on `v*` tags and publishes a
  GitHub Release.
- **`install.sh`** — `curl | sh` installer pulling the latest release binary.

### Licensing
- Free tier: autonomous repair (no license needed).
- Pro ($99): SBOM + policy engine (license-gated via `--pro`).
- Team ($299): Pro + multi-seat audit log (license-gated via `--pro`).

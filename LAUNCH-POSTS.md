# Libcage Launch — Tier-1 Post Drafts (ready to paste)

All links verified live:
- Repo/release: https://github.com/Hardonian/libcage (v0.1.0, linux + macos binaries)
- Product (free tier $29): https://aiautomatedsystems.ca/p/libcage
- Pro ($99): https://aiautomatedsystems.ca/p/libcage-pro
- Team ($299): https://aiautomatedsystems.ca/p/libcage-team
- Launch note: https://aiautomatedsystems.ca/landing/libcage-launch.html

====================================================================
1) HACKER NEWS — "Show HN"
====================================================================
Title: Show HN: Libcage – a 21KB pure-C LLM coding agent (no Python/Node)

Body:
Every autonomous coding agent today ships on Python or Node. That's fine on a
dev laptop, dead on an embedded box, an air-gapped CI runner, or a minimal
container with no venv you trust.

Libcage is the exception: a 21KB binary, zero dependencies (libc + POSIX
sockets only). It calls any OpenAI-compatible endpoint, reads your broken file,
applies the fix, compiles, and tests in a loop until it's green.

    libcage "Fix the off-by-one in parse()" broken.c "cc -o out broken.c" "./out"

Built + verified against local Ollama (qwen2.5-coder). The self-test asserts the
agent actually produces a working binary.

Tiers: free repair ($29), Pro adds SBOM + endpoint-policy ($99), Team adds a
tamper-evident audit log ($299). All one-time, local-first.

Repo + release (linux/macos binaries): https://github.com/Hardonian/libcage
Why pure C: https://aiautomatedsystems.ca/landing/libcage-launch.html

====================================================================
2) REDDIT r/selfhosted
====================================================================
Title: I built a coding agent that runs on a bare-metal box with no Python

Body:
Most "autonomous coding agents" need a full Python/Node runtime. I wanted one
that runs on my self-hosted/air-gapped gear, so I wrote it in pure C — 21KB,
libc + POSIX sockets, no dependencies.

It talks to any OpenAI-compatible API (I use Ollama locally), takes a broken
file + prompt, applies the fix, compiles, tests, and loops until green. Pro tier
adds a CycloneDX SBOM and an endpoint allowlist (fail-closed egress); Team adds
an HMAC-signed audit log.

Repo: https://github.com/Hardonian/libcage
Install: curl -fsSL https://raw.githubusercontent.com/Hardonian/libcage/main/install.sh | sh

Curious what people think — especially the SBOM/audit-angle for self-hosted
CI.

====================================================================
3) REDDIT r/C_Programming
====================================================================
Title: Pure-C project: a zero-dependency LLM agent for autonomous code repair

Body:
Wrote a small (~21KB) C11 program — libc + POSIX sockets only, no curl/jansson —
that drives an OpenAI-compatible /chat/completions endpoint to repair a source
file in a loop (write → compile → test → repeat).

Implementation notes:
- JSON parsing is minimal (extracts the fenced file block + a few fields).
- SHA-256 and HMAC-SHA256 are hand-rolled (no OpenSSL) for the SBOM + audit log.
- `make test` asserts the agent actually produces a compiling/running binary.

It's genuinely useful on embedded/air-gapped targets where Python agents can't
run. Feedback on the C welcome.

Repo + release: https://github.com/Hardonian/libcage

====================================================================
4) LOBSTERS
====================================================================
Title: Libcage: a 21KB pure-C LLM agent for autonomous code repair
URL: https://github.com/Hardonian/libcage
Tags: c, ai, show

Description:
Zero-dependency (libc + POSIX sockets) C11 coding agent. Calls any
OpenAI-compatible endpoint, repairs a file in a write/compile/test loop. Pro
adds CycloneDX SBOM + endpoint-policy; Team adds HMAC audit log. linux/macos
binaries via GitHub release.

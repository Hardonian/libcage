# Libcage

**A zero-dependency pure-C LLM agent runtime for autonomous code repair.**

Every coding agent today is Python or Node. Libcage runs where those can't —
air-gapped CI, embedded boxes, minimal containers, bare metal with no venv.
A single static binary, libc + POSIX sockets only.

## What it does

1. Reads a prompt + a target source file.
2. Sends them to an OpenAI-compatible `/chat/completions` endpoint.
3. Parses the model's corrected file (or unified diff) from the reply.
4. Writes it, compiles, and runs your test command.
5. On failure, feeds the error back to the model and loops (max N times).

## Build

```sh
make            # cc -O2 -std=c11 -o libcage agent.c  (no -l flags)
make test       # self-repair self-test against a broken C file via local Ollama
```

Binary is ~21KB, dynamically linked only against libc.

## Usage

```sh
export LIBCAGE_API_BASE=http://localhost:11434/v1   # Ollama (default)
export LIBCAGE_MODEL=qwen2.5-coder:7b
libcage "Fix the off-by-one in parse()" broken.c \
       "cc -o out broken.c" "./out"
```

### Environment

| Var | Default | Purpose |
|-----|---------|---------|
| `LIBCAGE_API_BASE` | `http://localhost:11434/v1` | OpenAI-compatible base URL |
| `LIBCAGE_API_KEY`  | `ollama` | Sent as `Authorization: Bearer` |
| `LIBCAGE_MODEL`    | `qwen2.5-coder:7b` | Model name |
| `LIBCAGE_MAX_ITER` | `5` | Repair-loop iterations |

Works against OpenAI, Ollama, or any `/v1/chat/completions` endpoint — including
a self-hosted inference lane on an EPYC GPU box.

## Why pure C

- **No supply chain.** One file, auditable by eye. No npm/pip transitive CVEs.
- **Portable.** `cc -O2 -std=c11` on Linux, macOS, BSD, minimal containers.
- **Future-proof.** C outlives language trends.

## License

See product terms. Source is provided for audit and modification.

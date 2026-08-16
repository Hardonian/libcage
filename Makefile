CC      ?= cc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra
BIN     ?= libcage
SRC     ?= agent.c

# Static, dependency-free binary. No -l flags: uses only libc + POSIX sockets.
$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $(BIN) $(SRC)

# Self-test: build a deliberately broken C file, ask libcage (via local Ollama if
# present) to repair it, then ASSERT it compiles + runs. Fails the build if the
# agent did not produce a working file. Falls back to compile-only if no endpoint.
test: $(BIN)
	@echo "libcage: running self-repair self-test..."
	@printf '#include <stdio.h>\nint main(){ printf("hello\\n"; return 0; }\n' > /tmp/broken.c
	@if curl -s -m2 http://localhost:11434/api/tags >/dev/null 2>&1; then \
		LIBCAGE_API_BASE=http://localhost:11434/v1 LIBCAGE_MODEL=qwen2.5-coder:7b \
		./$(BIN) "Fix the syntax error so this compiles and prints hello" /tmp/broken.c \
		"cc -o /tmp/broken_out /tmp/broken.c" "/tmp/broken_out" || exit 1; \
		if cc -o /tmp/broken_out /tmp/broken.c 2>/dev/null && /tmp/broken_out | grep -q hello; then \
			echo "SELF-TEST PASS"; \
		else \
			echo "SELF-TEST FAIL: repair did not produce a working binary" >&2; exit 1; \
		fi; \
	else \
		echo "libcage: no local LLM endpoint — compiled OK, skipping live repair test"; \
	fi

clean:
	rm -f $(BIN)

.PHONY: test clean

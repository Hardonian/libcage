# Libcage Launch — Backlink & Outreach Plan

Goal: drive the first qualified visitors to `/p/libcage` and `/p/libcage-pro`.
Constraint: zero ad spend, local-first, honest. Leverage novelty (only pure-C LLM agent).

## Angle (why it's link-worthy)
- "Every AI coding agent needs Python/Node. This one is 21KB of pure C."
- Demonstrable: `curl | sh` a binary, watch it fix a broken file in one loop.
- Novel: nobody else ships a dependency-free C coding agent.

## Tier 1 — High-authority, free (do first, ~2h total)
1. **GitHub README + Releases** — the repo IS the proof. Pin the launch post link.
   Reuse: already at github.com/Hardonian/libcage.
2. **Hacker News** — Show HN: "I built a 21KB pure-C LLM coding agent (no Python/Node)".
   Post the GitHub link + a 20s demo. Best window: 9–11am ET weekday.
3. **r/selfhosted + r/C_Programming + r/devops** — "Pure-C autonomous code-repair agent,
   runs air-gapped" with the demo gif. No spam; answer questions.
4. **Lobsters** — tag `c`, `ai`, `show`. Single high-quality post.

## Tier 2 — Backlinks that stick (do over week 1)
5. **Dev.to / Hashnode** — repost the launch note (already written: libcage-launch.html →
   markdown). Link to /p/libcage.
6. **Awesome lists** — submit to `awesome-c`, `awesome-selfhosted`, `awesome-ai-agents`
   via PR. One-line description + repo link.
7. **Tool directories** — AlternativeTo (vs Aider/Claude Code), Product Hunt (launch day).
8. **Reply-to-relevant** — find "how do I run a coding agent on embedded/air-gapped" threads;
   answer with Libcage + link. Never off-topic.

## Tier 3 — Partner / reciprocal (week 2+)
9. **Ollama community** — "Libcage + Ollama: autonomous repair with a local model".
10. **Sovereign/self-host newsletters** — pitch the local-first angle.

## Measurement (honest)
- Track in existing analytics: visits to `/p/libcage`, `/p/libcage-pro`, buy-link clicks.
- The revenue-truth gate remains the only source of "did we make money" — no synthetic claims.
- Backlinks: check `site:aiautomatedsystems.ca` growth + referral paths in GSC after 7 days.

## Kill criteria
- If <50 visits/week after 3 weeks of Tier 1+2, pivot the angle to "air-gapped CI repair"
  (more enterprise pull) rather than "pure C novelty".

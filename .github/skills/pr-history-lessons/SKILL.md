<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->
<!-- Copyright (C) 2026 G4OCCT Contributors -->

---
name: pr-history-lessons
description: >
  Review merged pull request history and update AGENTS.md with lessons learned.
  Use this when asked to extract lessons from PR history, codify recurring
  reviewer corrections, or update AGENTS.md from PR feedback.
---

This skill reviews merged pull requests, extracts recurring reviewer
corrections, and codifies them as new or updated rules in `AGENTS.md`.
Run it periodically (e.g. every ~100 merged PRs) to keep contributor
instructions current with hard-won project conventions.

## Step 1 — Determine the PR range

Find the highest merged PR number and the PR where the previous lesson update
ended (look for a title matching `docs: add lessons from PRs #N–#M to AGENTS.md`):

```bash
gh pr list --state merged --limit 1 --json number -q '.[0].number'
gh pr list --state merged --search "lessons from PRs" \
  --json number,title --limit 5
```

## Step 2 — Fetch reviewer comments

For each PR in the range, collect conversation-level and inline review comments:

```bash
gh pr view <N> --comments --json reviews,comments
gh api /repos/eic/G4OCCT/pulls/<N>/comments \
  --jq '[.[] | {path, body, user: .user.login}]'
```

Focus on comments requesting changes (not plain approvals).  Collect all
feedback into one file for synthesis.

## Step 3 — Synthesise recurring lessons

Group feedback thematically.  Only include lessons that:

- Appear as reviewer corrections in **two or more** separate PRs.
- Are actionable (an agent can apply the rule without human judgement).
- Are **not** already documented in `AGENTS.md`.

Discard one-off style preferences or subjective comments.

## Step 4 — Map lessons to AGENTS.md sections

| Topic | Section |
|---|---|
| SPDX / license headers | §1 License |
| Include ordering, IWYU | §2 Include Style |
| C++ standard, CMake versions | §3 Build Requirements |
| CMake variable naming | §4 CMake Conventions |
| Test fixtures, xfail | §5 Testing |
| CI jobs, containers, pip | §6 CI |
| Doxygen, docsite | §7 Documentation |
| Material names | §8 Material Bridging |
| G4VSolid navigation, kInfinity | §9 Geometry and Navigation |
| Formatting, `using` | §10 Code Style |
| pre-commit, codespell | §11 Code Quality Tools |
| Sanitiser suppressions | §12 Sanitizers |
| Benchmark reports | §13 Report and Script Conventions |
| Geant4 example files | §14 Examples |

Add a new numbered section when a topic does not fit any existing section.

## Step 5 — Apply edits and verify

After editing `AGENTS.md`:

```bash
codespell AGENTS.md
pre-commit run --files AGENTS.md
```

Fix any issues.  Add legitimate technical terms (Geant4 commands, math
vocabulary) to `.codespell-ignore` rather than altering the source.

## Step 6 — Commit via a worktree branch and file a PR

Create a dedicated branch using a git worktree so the main worktree is never
disturbed.  **Never `git checkout` in the main worktree.**

```bash
git fetch origin
git worktree add -b docs/agents-lessons-pr<start>-<end> \
  .worktrees/agents-lessons-pr<start>-<end> origin/main
cd .worktrees/agents-lessons-pr<start>-<end>
# Copy the edited AGENTS.md here, then commit.
```

Branch name: `docs/agents-lessons-pr<start>-<end>`

Commit message format:

```
docs: add lessons from PRs #<start>–#<end> to AGENTS.md

- §N <section name>: <what was added>
- §M <section name>: <what was added>
...

Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>
```

File as a ready-for-review (non-draft) PR and enable auto-merge:

```bash
gh pr create --title "docs: add lessons from PRs #<start>–#<end> to AGENTS.md" \
  --body "<description>" --head docs/agents-lessons-pr<start>-<end>
gh pr merge <number> --auto --squash
```

After the PR is filed, clean up the worktree:

```bash
cd /path/to/main/repo
git worktree remove .worktrees/agents-lessons-pr<start>-<end>
```

## Rules and Pitfalls

- **Only document recurring patterns** — do not add one-off reviewer preferences.
- **Update both** `AGENTS.md` **and** `.github/copilot-instructions.md` in the
  same commit when the Quick Reference summary is affected.
- `kInfinity ≈ 1e100` is **finite** — never use `std::isinf()` to test it.
- GDML files are XML — do **not** add them to the SPDX `ignore-paths`.
- Assembly fixtures use TCL/DRAWEXE scripts, not custom C++.
- pip fails in the eic-shell container — use `python3 -m venv /tmp/venv`.
- FetchContent HTTP downloads may fail in the container — prefer `find_package`
  first, cache downloaded artifacts via `actions/cache`.

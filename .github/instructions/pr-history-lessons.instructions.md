<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->
<!-- Copyright (C) 2026 G4OCCT Contributors -->

---
description: Review merged PR history and update AGENTS.md with lessons learned
applyTo: AGENTS.md
---

# Skill: Update AGENTS.md from PR History Lessons

This skill reviews merged pull request history, extracts recurring reviewer
corrections, and codifies them as new or updated rules in `AGENTS.md`.  Run it
periodically (e.g., every 100 merged PRs) to keep the contributor instructions
current with hard-won project conventions.

## When to Use

- After a significant batch of PRs has merged and you suspect new conventions
  have emerged that are not yet documented.
- When a reviewer asks to "codify this as a rule in AGENTS.md."
- As a scheduled maintenance task to prevent the same mistakes recurring.

## Steps

### 1. Determine the PR range

Find the last merged PR number and the PR number where the previous lesson
update ended.  The previous update PR will have a title like
`docs: add lessons from PRs #N–#M to AGENTS.md`.

```bash
# Find the highest merged PR number
gh pr list --state merged --limit 1 --json number -q '.[0].number'

# Find the last lesson-update PR to establish the start of the new range
gh pr list --state merged --search "lessons from PRs" \
  --json number,title --limit 5
```

### 2. Fetch reviewer comments for each PR in the range

Use parallel background agents or sequential `gh` calls.  For each PR:

```bash
# Conversation-level review comments
gh pr view <N> --comments --json reviews,comments

# Inline code-review comments
gh api /repos/eic/G4OCCT/pulls/<N>/comments \
  --jq '[.[] | {path, body, user: .user.login}]'
```

Collect all reviewer feedback into a single file for synthesis.  Focus on:
- Comments requesting changes (not just approvals).
- Lines where a reviewer asks to "fix", "change", "remove", "add", or "prefer".
- Patterns that appear in more than one PR.

### 3. Synthesise recurring lessons

Group the feedback thematically.  Only include lessons that:
- Appear as reviewer corrections in **two or more** separate PRs.
- Are actionable (an AI agent can apply the rule without human judgement).
- Are not already documented in `AGENTS.md`.

Discard one-off style preferences or subjective comments.

### 4. Draft edits to AGENTS.md

Map each lesson to the relevant section of `AGENTS.md`:

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

Add a new section (e.g., §15) when a topic does not fit an existing section.

### 5. Apply and verify

After editing `AGENTS.md`:

```bash
# Spell-check
codespell AGENTS.md

# Run pre-commit hooks on the file
pre-commit run --files AGENTS.md
```

Fix any issues before committing.

### 6. Commit via a worktree branch and file a PR

Follow the worktree workflow described in `AGENTS.md` (§15 / custom
instructions).  Use a branch name of the form
`docs/agents-lessons-pr<start>-<end>` and a commit message of the form:

```
docs: add lessons from PRs #<start>–#<end> to AGENTS.md

<bulleted summary of each new rule, grouped by section>

Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>
```

File the PR as ready for review (not draft) and enable auto-merge:

```bash
gh pr create --title "docs: add lessons from PRs #<start>–#<end> to AGENTS.md" \
  --body "<description>" --head <branch>
gh pr merge <number> --auto --squash
```

## Important Rules

- **Only document recurring patterns** — do not add one-off preferences.
- **Be precise** — quote the corrected code or rule verbatim where possible.
- **Update both** `AGENTS.md` **and** `.github/copilot-instructions.md` in the
  same commit when the Quick Reference summary is affected.
- **Do not reset `main`** — always use a worktree branch; never `git checkout`
  in the main worktree when other uncommitted work is present.
- Verify codespell passes before committing; add any legitimate technical terms
  (Geant4 commands, math vocabulary) to `.codespell-ignore`.

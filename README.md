<table>
    <thead>
        <tr>
            <th style="text-align:center">English</th>
            <th style="text-align:center"><a href="README_ja.md">日本語</a></th>
        </tr>
    </thead>
</table>

# llmls

LLM-optimized file listing CLI.

llmls lists files the way LLM agents need them — no color codes, no alignment padding, no icon decorations. Just filenames, sizes, ages, and git status in the minimum tokens possible. Designed for Claude Code, Cursor, Codex, and any LLM coding agent.

## Why llmls?

`ls -la` wastes tokens on information LLMs don't need:

```
drwxrwxr-x  8 hi-noguchi hi-noguchi  4096 Mar  9 21:44 .
-rw-rw-r--  1 hi-noguchi hi-noguchi 42891 Mar  8 23:47 CLAUDE.md
```

Permissions, owner, group, link count, `.git/` entries — all noise. llmls strips it:

```
dir project/ files:12 dirs:3 size:180K
  file CLAUDE.md size:43K age:2d git:modified
  dir src/ files:8
```

| | ls -la | llmls | llmls --dense |
|---|---|---|---|
| Output (27 entries) | 2,016 bytes | **855 bytes** | **599 bytes** |
| Token reduction | — | **57%** | **70%** |
| Git status | no | **yes** | **yes** |
| Self-explanatory | no | **yes** | no |

## Features

- **Self-explanatory output** — any LLM can parse it without reading docs
- **Git status integration** — changed files surface first, using `git status --porcelain=v1`
- **Smart sorting** — git:changed > important files > regular files > directories
- **Built-in ignore** — `.git/`, `node_modules/`, `__pycache__/`, `.env`, etc. filtered by default
- **Three output modes** — default (self-explanatory), dense (compressed), JSON (machine-readable)
- **63 KB static binary** — zero dependencies, instant startup

## Install

**Pre-built binary** (Linux x86_64):

```bash
curl -L -o llmls https://github.com/Kimi-Yori/llmls/releases/latest/download/llmls
chmod +x llmls
sudo mv llmls /usr/local/bin/
```

**Build from source** (requires musl-gcc for minimal static binary):

```bash
git clone https://github.com/Kimi-Yori/llmls.git
cd llmls
make static       # 63 KB static binary
sudo make install
```

**Or with system cc:**

```bash
make              # 27 KB dynamic binary
```

## Usage

```bash
# Default: self-explanatory format
llmls
# dir ./ files:12 dirs:3 size:180K
#   file CLAUDE.md size:43K age:2d git:modified
#   file README.md size:8K age:5d
#   dir src/ files:8

# Target a specific directory
llmls /path/to/project

# Dense format (for agents that know the spec)
llmls --dense
# . d=3 f=12 sz=180K
# f M. CLAUDE.md 43K 2d
# f .. README.md 8K 5d
# d .. src/ f=8

# JSON format (machine-readable)
llmls --json
# {"path":"./","files":12,"dirs":3,"size":184320,"entries":[...]}

# Show all files (include .git/, node_modules/, etc.)
llmls --all
```

## Output Format

### Default (self-explanatory)

```
dir project/ files:14 dirs:3 size:180K
  file CLAUDE.md size:43K age:2d git:modified
  file README.md size:8K age:5d
  dir src/ files:8
  dir tests/ files:3
```

Design decisions:
- `file`/`dir`/`symlink` type prefix — zero-shot readable
- `size:`/`age:`/`git:` key:value pairs — order-independent parsing
- `git:clean` omitted — absence means clean (noise reduction)
- No column alignment — saves whitespace tokens

### Dense (`--dense`)

```
. d=3 f=14 sz=180K
f M. CLAUDE.md 43K 2d
f .. README.md 8K 5d
d .. src/ f=8
```

- Single char type: `f`/`d`/`l`
- Git status: porcelain 2-char format (`M.`, `A.`, `??`, `..`)
- Positional fields, no keys

### JSON (`--json`)

```json
{
  "path": "project/",
  "files": 14,
  "dirs": 3,
  "size": 184320,
  "entries": [
    {"type": "file", "name": "CLAUDE.md", "size": 44032, "age_seconds": 172800, "git": "modified"},
    {"type": "dir", "name": "src/", "files": 8}
  ]
}
```

- Sizes in bytes, ages in seconds (precise, computable)
- Non-UTF-8 filenames safely escaped as `\uXXXX`

## Sort Order

Entries are sorted by priority (highest first):

1. **git:changed** — modified, added, deleted, renamed, conflicted, untracked
2. **Important files** — README, CLAUDE.md, package.json, go.mod, Cargo.toml, Makefile, Dockerfile, etc.
3. **Regular files** — alphabetical
4. **Directories** — alphabetical

This ensures the most actionable information appears first, even if context is truncated.

## Default Ignore

These are hidden by default (use `--all` to show):

```
.git/  node_modules/  __pycache__/  .venv/  .idea/  .vscode/
.DS_Store  .env  *.pyc
```

## Git Status Labels

| Label | Meaning |
|-------|---------|
| `git:modified` | Working tree changes |
| `git:added` | Staged new file |
| `git:untracked` | Not tracked by git |
| `git:renamed` | Renamed |
| `git:deleted` | Deleted |
| `git:conflicted` | Merge conflict |
| *(omitted)* | Clean |

Git status works from any subdirectory within a repository — llmls finds the repo root automatically via `git rev-parse --show-toplevel`.

## Options

| Option | Description |
|--------|-------------|
| *(none)* | Self-explanatory single-depth listing |
| `-a`, `--all` | Show all files (include ignored) |
| `-d`, `--dense` | Dense output format |
| `-j`, `--json` | JSON output format |
| `--depth N` | Listing depth (default: 1) |
| `-h`, `--help` | Show help |
| `-v`, `--version` | Show version |

## How It Works

```
target dir → [Walk] → [Git Status] → [Sort] → [Render]
                │           │            │          │
           openat/      pipe+exec    priority    3 output
           fstatat      porcelain    qsort       modes
```

- **Walk**: Uses `openat`/`fdopendir`/`fstatat` (no PATH_MAX assumptions)
- **Git**: Runs `git status --porcelain=v1 -z` via `pipe`+`fork`+`exec` (no shell injection)
- **Sort**: Priority-based with case-insensitive alphabetical tiebreak
- **Render**: Direct `printf` — no intermediate serialization

## Positioning

```
ls (human, verbose) < llmls < repo-map / indexer (heavy)
```

llmls answers: "What files are here, what changed, and what matters?" — the decision before reading code. It's not a repository indexer or a code search tool. It's the lightweight reconnaissance step.

## Build Targets

```bash
make              # Dynamic release (system cc, ~27 KB)
make static       # Static release (musl-gcc, ~63 KB)
make clean        # Remove build artifacts
make install      # Install to /usr/local/bin
```

## Benchmarks

Measured on Linux x86_64.

| Metric | Value |
|--------|-------|
| Binary size (static) | **63 KB** |
| Binary size (dynamic) | **27 KB** |
| Startup (non-git dir) | **~2 ms** |
| Startup (git repo) | **~5 ms** |
| Output reduction vs `ls -la` | **57-70%** |

The git-repo overhead (~3 ms) comes from `fork`+`exec` of the git process — unavoidable when integrating with git, and still well within interactive latency.

## Limitations

- Single depth by default (recursive walk planned for v0.2)
- No `.gitignore` parsing (uses built-in ignore list only)
- Requires `git` in PATH for git status features
- Linux x86_64 only (other platforms untested)

## Related Projects

- [nanojq](https://github.com/Kimi-Yori/nanojq) — ultra-lightweight JSON selector for LLM pipelines (same design philosophy)
- [cachit](https://github.com/Kimi-Yori/cachit) — command result cache for LLM agents
- [necache](https://github.com/Kimi-Yori/necache) — negative knowledge cache for LLM search
- [smart-trunc](https://github.com/Kimi-Yori/smart-trunc) — output truncation for LLM agents

## License

MIT

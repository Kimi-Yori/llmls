<table>
    <thead>
        <tr>
            <th style="text-align:center"><a href="README.md">English</a></th>
            <th style="text-align:center">日本語</th>
        </tr>
    </thead>
</table>

# llmls

LLMエージェント向けに最適化されたファイルリスティングCLI。

カラーコード、桁揃え、アイコン装飾なし。ファイル名、サイズ、経過時間、git statusを最小トークンで出力します。Claude Code、Cursor、Codex、その他あらゆるLLMコーディングエージェント向けに設計されています。

## なぜ llmls？

`ls -la` はLLMに不要な情報でトークンを浪費します：

```
drwxrwxr-x  8 hi-noguchi hi-noguchi  4096 Mar  9 21:44 .
-rw-rw-r--  1 hi-noguchi hi-noguchi 42891 Mar  8 23:47 CLAUDE.md
```

パーミッション、オーナー、グループ、リンク数、`.git/`エントリ — 全てノイズ。llmlsはこうなります：

```
dir project/ files:12 dirs:3 size:180K
  file CLAUDE.md size:43K age:2d git:modified
  dir src/ files:8
```

| | ls -la | llmls | llmls --dense |
|---|---|---|---|
| 出力量（27エントリ） | 2,016 bytes | **855 bytes** | **599 bytes** |
| トークン削減率 | — | **57%** | **70%** |
| Git status | なし | **あり** | **あり** |
| 初見で読める | いいえ | **はい** | いいえ |

## 特徴

- **自己説明的な出力** — どのLLMでもドキュメントなしで理解可能
- **Git status統合** — 変更ファイルが最上位に浮上、`git status --porcelain=v1` ベース
- **スマートソート** — git:changed > 重要ファイル > 通常ファイル > ディレクトリ
- **ビルトイン除外** — `.git/`、`node_modules/`、`__pycache__/`、`.env` 等をデフォルト除外
- **3つの出力モード** — デフォルト（自己説明的）、dense（圧縮）、JSON（機械可読）
- **63 KB staticバイナリ** — 外部依存ゼロ、瞬時起動

## インストール

**ソースからビルド**（最小staticバイナリにはmusl-gccが必要）：

```bash
git clone https://github.com/Kimi-Yori/llmls.git
cd llmls
make static       # 63 KB staticバイナリ
sudo make install
```

**システムccでも可：**

```bash
make              # 27 KB dynamicバイナリ
```

## 使い方

```bash
# デフォルト：自己説明的フォーマット
llmls
# dir ./ files:12 dirs:3 size:180K
#   file CLAUDE.md size:43K age:2d git:modified
#   file README.md size:8K age:5d
#   dir src/ files:8

# ディレクトリ指定
llmls /path/to/project

# denseフォーマット（仕様を共有済みのエージェント向け）
llmls --dense
# . d=3 f=12 sz=180K
# f M. CLAUDE.md 43K 2d
# f .. README.md 8K 5d
# d .. src/ f=8

# JSONフォーマット（機械可読）
llmls --json
# {"path":"./","files":12,"dirs":3,"size":184320,"entries":[...]}

# 全ファイル表示（.git/, node_modules/ 等も含む）
llmls --all
```

## 出力フォーマット

### デフォルト（自己説明的）

```
dir project/ files:14 dirs:3 size:180K
  file CLAUDE.md size:43K age:2d git:modified
  file README.md size:8K age:5d
  dir src/ files:8
  dir tests/ files:3
```

設計判断：
- `file`/`dir`/`symlink` の種別プレフィックス — 初見のLLMでも即理解
- `size:`/`age:`/`git:` のkey:value形式 — 並び順に依存しないパース
- `git:clean` は省略 — 省略時がclean（ノイズ削減）
- 桁揃えなし — 空白トークンの節約

### Dense（`--dense`）

```
. d=3 f=14 sz=180K
f M. CLAUDE.md 43K 2d
f .. README.md 8K 5d
d .. src/ f=8
```

- 1文字種別：`f`/`d`/`l`
- Git status：porcelain 2文字形式（`M.`、`A.`、`??`、`..`）
- 位置ベースフィールド、キーなし

### JSON（`--json`）

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

- サイズはバイト単位、経過時間は秒単位（正確、計算可能）
- 非UTF-8ファイル名は `\uXXXX` にエスケープ

## ソート順

エントリは優先度順にソート（上が高優先）：

1. **git:changed** — modified, added, deleted, renamed, conflicted, untracked
2. **重要ファイル** — README, CLAUDE.md, package.json, go.mod, Cargo.toml, Makefile, Dockerfile 等
3. **通常ファイル** — アルファベット順
4. **ディレクトリ** — アルファベット順

コンテキストが途中で打ち切られても、最も重要な情報が先に届きます。

## デフォルト除外

以下はデフォルトで非表示（`--all` で表示）：

```
.git/  node_modules/  __pycache__/  .venv/  .idea/  .vscode/
.DS_Store  .env  *.pyc
```

## Git Statusラベル

| ラベル | 意味 |
|--------|------|
| `git:modified` | ワーキングツリーの変更 |
| `git:added` | ステージ済み新規ファイル |
| `git:untracked` | 未追跡 |
| `git:renamed` | リネーム |
| `git:deleted` | 削除 |
| `git:conflicted` | マージコンフリクト |
| *（省略）* | clean |

Git statusはリポジトリ内のどのサブディレクトリからでも動作します。`git rev-parse --show-toplevel` でrepo rootを自動検出します。

## オプション

| オプション | 説明 |
|-----------|------|
| *（なし）* | 自己説明的1階層リスティング |
| `-a`, `--all` | 全ファイル表示（除外なし） |
| `-d`, `--dense` | 圧縮フォーマット |
| `-j`, `--json` | JSON出力 |
| `--depth N` | 表示深度（デフォルト: 1） |
| `-h`, `--help` | ヘルプ表示 |
| `-v`, `--version` | バージョン表示 |

## 仕組み

```
対象ディレクトリ → [Walk] → [Git Status] → [Sort] → [Render]
                     │           │             │          │
                openat/      pipe+exec      優先度     3つの出力
                fstatat      porcelain      qsort      モード
```

- **Walk**: `openat`/`fdopendir`/`fstatat` 使用（PATH_MAX前提なし）
- **Git**: `git status --porcelain=v1 -z` を `pipe`+`fork`+`exec` で実行（shell injection なし）
- **Sort**: 優先度ベース、大文字小文字無視のアルファベット順でタイブレイク
- **Render**: `printf` 直接出力 — 中間シリアライズなし

## ポジショニング

```
ls（人間向け、冗長）< llmls < repo-map / indexer（重い）
```

llmlsが答える問い：「ここにどんなファイルがあり、何が変わっていて、何が重要か？」— コードを読む前の判断材料。リポジトリインデクサでもコード検索ツールでもなく、軽量な偵察ステップです。

## ビルドターゲット

```bash
make              # dynamicリリース（システムcc、~27 KB）
make static       # staticリリース（musl-gcc、~63 KB）
make clean        # ビルド成果物削除
make install      # /usr/local/bin にインストール
```

## ベンチマーク

Linux x86_64上にて計測。

| 指標 | 値 |
|------|-----|
| バイナリサイズ（static） | **63 KB** |
| バイナリサイズ（dynamic） | **27 KB** |
| 起動時間（gitなし） | **~2 ms** |
| 起動時間（gitリポジトリ） | **~5 ms** |
| `ls -la` 比の出力削減率 | **57〜70%** |

gitリポジトリでのオーバーヘッド（~3 ms）はgitプロセスの `fork`+`exec` コスト — git連携では不可避であり、インタラクティブな遅延として十分許容範囲です。

## 制限事項

- デフォルトは1階層のみ（再帰走査はv0.2で予定）
- `.gitignore` パースなし（ビルトイン除外リストのみ）
- Git status機能にはPATH上の `git` が必要
- Linux x86_64のみ（他プラットフォームは未テスト）

## 関連プロジェクト

- [nanojq](https://github.com/Kimi-Yori/nanojq) — LLMパイプライン向け超軽量JSONセレクタ（同じ設計思想）
- [cachit](https://github.com/Kimi-Yori/cachit) — LLMエージェント向けコマンド結果キャッシュ
- [necache](https://github.com/Kimi-Yori/necache) — LLM検索向け否定知識キャッシュ
- [smart-trunc](https://github.com/Kimi-Yori/smart-trunc) — LLMエージェント向け出力トランケーション

## ライセンス

MIT

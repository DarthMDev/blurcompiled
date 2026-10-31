# Publishing and development trails

## Release prerequisites

Source destination: https://github.com/DarthMDev/blurcompiled

Entire destination: https://entire.com/et/blurcompiled/blurcompiled

Project license: GPL-3.0-or-later for original contributions.

Before publishing:

1. Preserve the GPL license and third-party notices.
2. Audit the exact file manifest. Exclude original binaries, assets, captures,
   generated translations and captured game shader payloads. Check source/header
   arrays too; a text extension is not proof a file is safe to redistribute.
3. Remove private machine paths and credentials from publishable docs/configs.
4. Verify the documented fresh-clone support build; describe the missing game
   generation pipeline explicitly.
5. Commit an audited source manifest, then push to the selected repository.

Do not run `git add .` on this research workspace. Some folders live on a volume
with unreliable directory type reporting; use explicit paths and inspect the
resulting Git index, including ignored-path checks.

## Entire

[Entire CLI](https://github.com/entireio/cli) captures agent development trails.
It is installed locally, but was not enabled when this preparation started.
A Git source repository and an Entire repository/mirror are separate publishing
steps; pushing source does not prove that trails were uploaded or indexed.

Choose the repository and checkpoint destination before enabling uploads.
Historical session import is separate and must not silently publish old chats,
private paths, captures or credentials. Start with fresh trails and inspect the
configured agent support and local `entire --help` for the installed version.
Record the final source remote, checkpoint remote and Entire project URL here
once configured. Do not put tokens in this file.

## Initial publication validation

The public support tree builds independently of the private SDK workspace.
The local full Plume configuration passed all 14 CTest checks; 103 Python tests
ran with one skip. Captured game shader fixtures are excluded from Git, so the
live frontend is intentionally not enabled in a fresh-clone support build.

Entire Codex hooks are configured locally without historical imports. The CLI
reports that hook approval through `/hooks` is still needed. Session uploads
remain disabled until the destination is authenticated and trails are reviewed.

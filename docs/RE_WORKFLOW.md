# Reproducible reverse engineering workflow

1. Generate `reports/install.json` and check the Blur.exe hash against the baseline
   in INITIAL_FINDINGS.md. New hashes create a new analysis baseline, not a silent
   replacement of old addresses.
2. Use [Ghidra](https://github.com/NationalSecurityAgency/ghidra) for decompilation.
   Follow its release-specific Java requirements. The local setup used for the archive pass is recorded below. Store its databases in `local/ghidra/` (ignored).
3. Import the PE files separately, preserving their loaded image bases. Start
   with the small presence DLL for a tractable state-machine investigation, and
   the game asset loader for the first reusable native game component.
4. For each function, record the program hash, RVA, proposed name, calling
   convention, input/output types, call references, assumptions and confidence.
   Mark pseudocode and unverified types as such. Never call inferred names symbols
   recovered from the original source.
5. Port bounded pure behavior with synthetic fixtures and reference outputs.
   Build native adapters for platform calls. Compare a reconstructed function's
   result against the original before expanding the slice.

The first local disassembly was generated with:

```sh
cd /path/to/user/github/blur-native
xcrun llvm-objdump -d --x86-asm-syntax=intel \
  '/path/to/Blur/amax/dlls/blur_rpc.dll' \
  > reports/blur_rpc.disasm.txt
```

This is disassembly, not a completed decompilation. Archive extraction and runtime reference captures remain future work. Narrow
archive research labels and executable evidence now appear in PAK_FORMAT.md.

## Evidence record template

```text
program / SHA-256:
RVA / preferred VA:
proposed name:
evidence (call sites, strings, reference behavior):
types / calling convention:
confidence (observed / inferred / validated against original):
native replacement and comparison fixture:
remaining uncertainty:
```

Do not check in original binaries, whole disassemblies, game assets, accounts,
save data, or packet captures. The source project's licensing choice remains open;
record licenses and attribution before incorporating third-party source.

## Local Ghidra setup for archive research

Official [Ghidra 12.1.4 release](https://github.com/NationalSecurityAgency/ghidra/releases/tag/Ghidra_12.1.4_build)
was unpacked into `local/tools/ghidra_12.1.4_PUBLIC/`. ZIP SHA-256 matched the
published value:
`ddac49f903da9d5bac833e5cc79395098b9c33cfd3279be5f31bd00387d2d4db`.
Release metadata and the ZIP remain local. Consult the release's
[Getting Started guide](https://github.com/NationalSecurityAgency/ghidra/blob/Ghidra_12.1.4_build/GhidraDocs/GettingStarted.md)
for its JDK and platform requirements.

Temurin ARM64 JDK 21.0.12.1+1 was downloaded from Adoptium's official API and
unpacked at `local/tools/jdk-21.0.12.1+1/Contents/Home`; no system Java install or
shell profile was changed. Downloaded tarball SHA-256:
`3623232f33a9c3baadf304480b2535f9a3cba8a58d42ecbb438ba267315d9998`
(computed locally, not independently checked against vendor checksum metadata).
The existing system JDKs were 25, 19, 17, 11 and 8; the local JDK pins the release's
specified major version.

Ghidra's official bundled Gradle wrapper (9.7.1) built its macOS ARM64 native
components successfully using AppleClang. Its downloads/cache stay in
`local/tools/gradle-cache`. From the repository root:

```sh
cd local/tools/ghidra_12.1.4_PUBLIC/support/gradle
JAVA_HOME='/path/to/user/github/blur-native/local/tools/jdk-21.0.12.1+1/Contents/Home' \
GRADLE_USER_HOME='/path/to/user/github/blur-native/local/tools/gradle-cache' \
  bash ./gradlew buildNatives
```

Before running, the local `support/launch.properties` was extended with:

```properties
VMARGS=-Dapplication.settingsdir=/path/to/user/github/blur-native/local/ghidra/settings
VMARGS=-Dapplication.cachedir=/path/to/user/github/blur-native/local/ghidra/cache
VMARGS=-Dapplication.tempdir=/path/to/user/github/blur-native/local/ghidra/tmp
```

The source installation was opened for PE import only. No original executable was
run. Missing Windows library/PDB notices do not establish game behavior. Review
analysis logs rather than assuming automatic analysis has correct types or a
complete call graph.

The tracked `tools/ghidra/ArchiveEvidence.java` exports instruction listings,
references and explicitly labeled inferred pseudocode into a local output file.
It rejects programs whose executable SHA-256 differs from this baseline.
For the initial import, run from the repository root:

```sh
JAVA_HOME='/path/to/user/github/blur-native/local/tools/jdk-21.0.12.1+1/Contents/Home' \
local/tools/ghidra_12.1.4_PUBLIC/support/analyzeHeadless \
  local/ghidra blur-dba0264b \
  -import '/path/to/Blur/Blur.exe' \
  -analysisTimeoutPerFile 600 -max-cpu 4 \
  -scriptPath tools/ghidra \
  -postScript ArchiveEvidence.java reports/ghidra-archive.txt \
  cb87f0 cb8890 cb89d0 cb9180 cb84e0 cb6b90 cb6ba0 cb6b70 \
  -log reports/ghidra-analysis.log -scriptlog reports/ghidra-script.log
```

For later inspection, replace `-import ...` with `-process Blur.exe -noanalysis`
and use a different output filename. Addresses passed to the exporter are
preferred VAs; findings should record RVAs by subtracting `0x400000`.
The small presence DLL has not been imported in this archive-focused pass.


Initial analysis completed successfully in 281 seconds; import and database save
succeeded. The exporter ran successfully both during import and in later
`-process ... -noanalysis` passes. Relevant follow-up target groups were:

```text
reports/ghidra-archive-callers.txt:
  cb7010 cb8ad0 cb8c70 cb9390 cb88f0 cb95d0 efab60
reports/ghidra-archive-layout.txt:
  cb8e80 cb8d50 cb8f60 cb8560 cb3390 cb3300 cb68b0 cb69b0 cb85d0
reports/ghidra-archive-strings.txt:
  e67828 e67af8 efa9f8 cb61b0 cb6cb0
```

The initial `cb8890` target had no automatically discovered function or xrefs;
its standalone disassembly is not used to name a recovered routine. Several
initial string targets pointed inside/near strings; the final strings pass uses
the exact starts above. See PAK_FORMAT.md for the reviewed conclusions.

The repository had no commits and all source files were already untracked at
start of this pass. No commit or publication was performed. Local tools, analysis
artifacts and asset metadata reports were verified ignored by Git.

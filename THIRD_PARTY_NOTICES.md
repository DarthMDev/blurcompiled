# Licensing and third-party notices

Original BlurCompiled project code is licensed under **GPL-3.0-or-later**:
you may redistribute and modify it under GNU GPL version 3 or, at your option,
any later version. The full GPLv3 text is in [LICENSE](LICENSE).
SPDX-License-Identifier: GPL-3.0-or-later

This declaration applies to original project contributions, not to proprietary
game content or independently licensed upstream code. Preserve existing notices.
No rights in Blur, its executable, assets or captured shaders are granted here.

## Dependencies and adaptations

- Plume: pinned upstream revision and exact file hashes in `recomp/plume-pin.json`.
  Its fetched source retains its MIT license; Apple metal-cpp includes retain
  Apple's notices. The setup tool includes the pinned upstream LICENSE.
- ReXGlue / Xenia: SDK dependencies are external local inputs. Existing upstream
  copyright and BSD notices remain applicable to source adapted from them.
  `recomp/rex-plume-state-pin.json` identifies the SDK files used by adapters.
- SDL3 and zlib are external dependencies; retain their upstream license terms
  when distributing them. The optional movie viewer uses separately installed
  FFmpeg libraries; comply with the licenses of the actual FFmpeg configuration.

Fetched dependency trees, game translations, game binaries and shader payloads
are not source-release inputs. Do not remove attribution in a contribution.

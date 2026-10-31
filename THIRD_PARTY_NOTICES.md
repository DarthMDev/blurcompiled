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
  `recomp/metal/sdk.patch` contains the BSD-licensed native Metal adaptation;
  `sdk-pin.json` pins its public base and patched output hashes. The upstream
  license is preserved in [LICENSE.ReXGlue](recomp/metal/LICENSE.ReXGlue). It incorporates
  native Metal work from [Xenia Alloy](https://github.com/Igggy-code/xenia-alloy)
  through the local Forza/ReXGlue adaptation. Preserve all file-level notices.
- SDK dependency source revisions and archive hashes are recorded in
  `recomp/metal/dependencies.json`; retain their upstream licenses, including
  SDL3, FFmpeg and the Vulkan toolchain.
- Metal compiler/header dependencies are pinned in
  `recomp/metal/converter-dependencies.json`: DirectXShaderCompiler (LLVM/NCSA
  and MIT notices), DirectX-Headers (MIT), SPIRV-Cross (Apache-2.0), and Apple's
  metal-cpp headers (their included license). The DXC portability changes follow
  Alloy's `cmake/patches/dxilconv-macos.patch`.
- Apple's Metal Shader Converter is a separately installed, user-supplied
  dependency. This repository does not redistribute its proprietary dylib.
  Its installation and any binary distribution must follow Apple's terms.

Fetched dependency trees, game translations, game binaries and shader payloads
are not source-release inputs. Do not remove attribution in a contribution.

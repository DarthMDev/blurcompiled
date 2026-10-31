// SPDX-License-Identifier: GPL-3.0-or-later
#include "generated/default/blur_x360_init.h"
#include <rex/rex_app.h>
#include <cstdio>
#include <string_view>

class BlurCompiledApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;
  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<BlurCompiledApp>(new BlurCompiledApp(ctx, "blur_x360", PPCImageConfig));
  }
};
REX_DEFINE_APP(blur_x360, BlurCompiledApp::Create)

#if defined(__APPLE__)
int blurcompiled_sdk_main(int argc, char** argv);
int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--help" || arg == "-h") {
      std::puts("BlurCompiled - experimental Xbox 360 recompilation\n"
                "Usage: blur_x360 --game_data_root=DIR [SDK options]\n"
                "  --no-fullscreen  --window_width=960  --window_height=540\n"
                "See docs/BUILD_GAME.md. Graphics and gameplay validation remain incomplete.");
      return 0;
    }
  }
  return blurcompiled_sdk_main(argc, argv);
}
#endif

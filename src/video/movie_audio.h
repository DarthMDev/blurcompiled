#pragma once
#include <filesystem>
#include <vector>

// New viewer implementation, not recovered game audio logic. Decode one chosen
// soundtrack to bounded stereo PCM before presentation so playback cannot starve
// while video decoding/demuxing advances. Tracks are alternatives, not mixed.
struct MovieAudio {
  static constexpr int rate = 48000;
  static constexpr int channels = 2;
  std::vector<float> samples;
  int track = -1;
  int tracks = 0;
};
MovieAudio decode_movie_audio(const std::filesystem::path &path, int track,
                             double video_origin);

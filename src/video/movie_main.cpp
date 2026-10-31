// New native movie resource slice. This does not execute the game's startup
// state machine or establish that its menu has been reached.
#include "movie_audio.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
#include <charconv>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <zlib.h>

namespace fs = std::filesystem;
static void require(bool ok, const std::string &why) {
  if (!ok)
    throw std::runtime_error(why);
}
static void avcheck(int code, const char *operation) {
  if (code >= 0)
    return;
  char error[AV_ERROR_MAX_STRING_SIZE];
  av_strerror(code, error, sizeof error);
  throw std::runtime_error(std::string(operation) + ": " + error);
}
struct Movie {
  AVFormatContext *input = nullptr;
  AVCodecContext *codec = nullptr;
  AVFrame *frame = av_frame_alloc();
  AVPacket *packet = av_packet_alloc();
  int video = -1;
  bool flushing = false;
  ~Movie() {
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&codec);
    avformat_close_input(&input);
  }
  void open(const fs::path &path) {
    require(frame && packet, "Cannot allocate decoder state");
    avcheck(avformat_open_input(&input, path.c_str(), nullptr, nullptr),
            "open movie");
    avcheck(avformat_find_stream_info(input, nullptr), "inspect streams");
    video = av_find_best_stream(input, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    avcheck(video, "find video");
    auto *parameters = input->streams[video]->codecpar;
    const auto *decoder = avcodec_find_decoder(parameters->codec_id);
    require(decoder, "No decoder for this movie");
    codec = avcodec_alloc_context3(decoder);
    require(codec, "Cannot allocate codec context");
    avcheck(avcodec_parameters_to_context(codec, parameters),
            "copy codec parameters");
    avcheck(avcodec_open2(codec, decoder, nullptr), "open decoder");
    std::cout << "Decoder: " << decoder->name << " (native resource viewer)\n";
  }
  bool next() {
    for (;;) {
      int code = avcodec_receive_frame(codec, frame);
      if (code == 0)
        return true;
      if (code == AVERROR_EOF)
        return false;
      if (code != AVERROR(EAGAIN))
        avcheck(code, "decode frame");
      require(!flushing, "Decoder did not finish after draining");
      do {
        av_packet_unref(packet);
        code = av_read_frame(input, packet);
        if (code == AVERROR_EOF) {
          flushing = true;
          avcheck(avcodec_send_packet(codec, nullptr), "drain decoder");
          break;
        }
        avcheck(code, "read packet");
      } while (packet->stream_index != video);
      if (!flushing)
        avcheck(avcodec_send_packet(codec, packet), "send video packet");
    }
  }
};
struct Display {
  SDL_Window *window = nullptr;
  SDL_Renderer *renderer = nullptr;
  SDL_Texture *texture = nullptr;
  ~Display() {
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
  }
};

struct AudioPlayback {
  SDL_AudioStream *stream = nullptr;
  size_t total_frames = 0;
  double latency = 0;
  Uint64 started = 0, empty_since = 0;
  ~AudioPlayback() { SDL_DestroyAudioStream(stream); }
  void prepare(const MovieAudio &clip) {
    if (clip.samples.empty()) return;
    require(SDL_InitSubSystem(SDL_INIT_AUDIO), SDL_GetError());
    SDL_AudioSpec source{SDL_AUDIO_F32, MovieAudio::channels, MovieAudio::rate};
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                       &source, nullptr, nullptr);
    require(stream, SDL_GetError());
    SDL_AudioSpec device;
    int frames = 0;
    require(SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream), &device, &frames), SDL_GetError());
    latency = double(frames) / device.freq;
    total_frames = clip.samples.size() / 2;
    require(SDL_PutAudioStreamData(stream, clip.samples.data(),
                                  static_cast<int>(clip.samples.size() * sizeof(float))), SDL_GetError());
    require(SDL_FlushAudioStream(stream), SDL_GetError());
    std::cout << "Audio output: " << device.freq << " Hz, " << device.channels
              << " channels, device buffer " << frames << " frames\n";
  }
  void start() {
    started = SDL_GetTicksNS();
    if (stream) require(SDL_ResumeAudioStreamDevice(stream), SDL_GetError());
  }
  double seconds() {
    if (!stream) return double(SDL_GetTicksNS() - started) / 1e9;
    const int queued = SDL_GetAudioStreamQueued(stream);
    require(queued >= 0, SDL_GetError());
    const size_t remaining = static_cast<size_t>(queued) / (2 * sizeof(float));
    double clock = double(total_frames - std::min(total_frames, remaining)) / MovieAudio::rate - latency;
    if (queued == 0) {
      if (!empty_since) empty_since = SDL_GetTicksNS();
      // Continue the timeline after the last buffered samples, permitting a
      // silent video tail. The device buffer is an estimate of output latency.
      clock += double(SDL_GetTicksNS() - empty_since) / 1e9;
    }
    return std::max(0.0, clock);
  }
};

int main(int argc, char **argv) {
  try {
    fs::path assets, name, snapshot;
    int limit = 0;
    int audio_track = -1;
    bool decode_only = false, mute = false;
    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--help") {
        std::cout
            << "Native movie resource viewer (no game startup/menu)\n"
               "--assets INSTALL --movie FILENAME [--frames N] [--decode-only] "
               "[--snapshot BMP] [--mute] [--audio-track N]\n"
               "Audio tracks are zero-based; default is first stereo track, else first audio track.\n";
        return 0;
      }
      if (arg == "--decode-only") {
        decode_only = true;
        continue;
      }
      if (arg == "--mute") { mute = true; continue; }
      require(i + 1 < argc, "Missing option value");
      std::string value = argv[++i];
      if (arg == "--assets")
        assets = value;
      else if (arg == "--movie")
        name = value;
      else if (arg == "--snapshot")
        snapshot = value;
      else if (arg == "--audio-track") {
        auto result = std::from_chars(value.data(), value.data() + value.size(), audio_track);
        require(result.ec == std::errc{} && result.ptr == value.data() + value.size() && audio_track >= 0,
                "--audio-track requires a nonnegative integer");
      }
      else if (arg == "--frames") {
        auto result =
            std::from_chars(value.data(), value.data() + value.size(), limit);
        require(result.ec == std::errc{} &&
                    result.ptr == value.data() + value.size() && limit > 0,
                "--frames requires a positive integer");
      } else
        throw std::runtime_error("Unknown option: " + arg);
    }
    require(!assets.empty() && !name.empty() && name == name.filename() &&
                name != "." && name != "..",
            "Provide an installation and a movie filename under UI/movies");
    assets = fs::canonical(assets);
    const auto path = fs::canonical(assets / "UI/movies" / name);
    const auto relative = path.lexically_relative(assets);
    require(!relative.empty() && *relative.begin() != "..",
            "Movie escapes the installation");
    if (!snapshot.empty()) {
      require(!decode_only, "Snapshot requires rendering");
      snapshot = fs::weakly_canonical(snapshot);
      auto rel = snapshot.lexically_relative(assets);
      require(!rel.empty() && *rel.begin() == "..",
              "Snapshot must be outside the installation");
    }
    Movie movie;
    movie.open(path);
    Display display;
    AudioPlayback playback;
    MovieAudio audio;
    int width = 0, height = 0, decoded = 0;
    std::set<uLong> checksums;
    uLong stream_crc = crc32(0L, Z_NULL, 0);
    double first_pts = 0;
    double last_seconds = 0, max_lateness = 0;
    auto rate = av_guess_frame_rate(movie.input, movie.input->streams[movie.video], nullptr);
    const double frame_duration = rate.num > 0 && rate.den > 0 ? av_q2d(av_inv_q(rate)) : 1.0 / 30;
    bool running = true;
    auto events = [&]() {
      SDL_Event event;
      while (SDL_PollEvent(&event))
        if (event.type == SDL_EVENT_QUIT ||
            (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) running = false;
    };
    auto wait_until = [&](double target) {
      while (running && playback.seconds() < target) {
        events();
        SDL_Delay(2);
      }
    };
    while (running && (!limit || decoded < limit) && movie.next()) {
      auto *f = movie.frame;
      require(f->format == AV_PIX_FMT_YUV420P && f->width > 0 &&
                  f->height > 0 && f->width <= 8192 && f->height <= 8192,
              "Unsupported movie pixel format/dimensions");
      if (!decoded) {
        width = f->width;
        height = f->height;
        first_pts = f->best_effort_timestamp == AV_NOPTS_VALUE ? 0 :
            f->best_effort_timestamp * av_q2d(movie.input->streams[movie.video]->time_base);
        if (!mute) audio = decode_movie_audio(path, audio_track, first_pts);
        if (!audio.samples.empty()) {
          uLong audio_crc = crc32(0L, Z_NULL, 0);
          audio_crc = crc32(audio_crc, reinterpret_cast<const Bytef *>(audio.samples.data()),
                            static_cast<uInt>(audio.samples.size() * sizeof(float)));
          float peak = 0;
          for (float sample : audio.samples) peak = std::max(peak, std::abs(sample));
          std::cout << "Audio: track " << audio.track << '/' << audio.tracks
                    << ", " << audio.samples.size() / 2 << " stereo frames at 48000 Hz"
                    << ", PCM CRC32 " << std::hex << audio_crc << std::dec << ", peak " << peak << '\n';
        } else std::cout << (mute ? "Audio: muted\n" : "Audio: no samples\n");
        if (!decode_only) {
          require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
          display.window =
              SDL_CreateWindow("Blur Native | movie viewer",
                               width, height, SDL_WINDOW_RESIZABLE);
          require(display.window, SDL_GetError());
#ifdef __APPLE__
          const char *backend = "metal";
#else
          const char *backend = nullptr;
#endif
          display.renderer = SDL_CreateRenderer(display.window, backend);
          require(display.renderer, SDL_GetError());
          display.texture =
              SDL_CreateTexture(display.renderer, SDL_PIXELFORMAT_IYUV,
                                SDL_TEXTUREACCESS_STREAMING, width, height);
          require(display.texture, SDL_GetError());
          require(SDL_SetRenderLogicalPresentation(
                      display.renderer, width, height,
                      SDL_LOGICAL_PRESENTATION_LETTERBOX),
                  SDL_GetError());
          std::cout << "Renderer: " << SDL_GetRendererName(display.renderer)
                    << '\n';
          playback.prepare(audio);
        }
      }
      require(f->width == width && f->height == height,
              "Movie dimensions changed");
      uLong crc = crc32(0L, Z_NULL, 0);
      for (int plane = 0; plane < 3; ++plane) {
        int w = plane ? (width + 1) / 2 : width;
        int h = plane ? (height + 1) / 2 : height;
        require(f->data[plane] && std::abs(f->linesize[plane]) >= w,
                "Invalid video plane");
        for (int y = 0; y < h; ++y) {
          crc = crc32(
              crc, f->data[plane] + std::ptrdiff_t(y) * f->linesize[plane], w);
          stream_crc =
              crc32(stream_crc,
                    f->data[plane] + std::ptrdiff_t(y) * f->linesize[plane], w);
        }
      }
      checksums.insert(crc);
      last_seconds = f->best_effort_timestamp == AV_NOPTS_VALUE ? decoded * frame_duration :
          f->best_effort_timestamp * av_q2d(movie.input->streams[movie.video]->time_base) - first_pts;
      if (!decode_only) {
        events();
        if (!running)
          break;
        if (decoded) wait_until(last_seconds);
        if (!running) break;
        require(SDL_UpdateYUVTexture(display.texture, nullptr, f->data[0],
                                     f->linesize[0], f->data[1], f->linesize[1],
                                     f->data[2], f->linesize[2]),
                SDL_GetError());
        if (!decoded) playback.start();
        max_lateness = std::max(max_lateness, playback.seconds() - last_seconds);
        require(SDL_SetRenderDrawColor(display.renderer, 0, 0, 0, 255) &&
                    SDL_RenderClear(display.renderer) &&
                    SDL_RenderTexture(display.renderer, display.texture,
                                      nullptr, nullptr) &&
                    SDL_RenderPresent(display.renderer),
                SDL_GetError());
      }
      ++decoded;
    }
    require(decoded > 0, "No video frames decoded");
    if (!decode_only && !limit && running) {
      wait_until(std::max(last_seconds + frame_duration,
                          double(audio.samples.size() / 2) / MovieAudio::rate));
    }
    if (!snapshot.empty()) {
      require(SDL_RenderClear(display.renderer) &&
                  SDL_RenderTexture(display.renderer, display.texture, nullptr,
                                    nullptr),
              SDL_GetError());
      auto *surface = SDL_RenderReadPixels(display.renderer, nullptr);
      require(surface, SDL_GetError());
      bool ok = SDL_SaveBMP(surface, snapshot.c_str());
      SDL_DestroySurface(surface);
      require(ok, SDL_GetError());
    }
    std::cout << "Decoded " << decoded << " frames, " << width << 'x' << height
              << ", distinct YUV CRCs " << checksums.size() << ", stream CRC32 "
              << std::hex << stream_crc << std::dec << '\n';
    if (!decode_only)
      std::cout << "Presentation clock: " << (playback.stream ? "audio consumed frames" : "monotonic")
                << ", max video lateness " << max_lateness << " seconds\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Movie slice: " << e.what() << '\n';
    return 1;
  }
}

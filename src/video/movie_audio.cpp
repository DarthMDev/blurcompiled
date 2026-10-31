#include "movie_audio.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace {
constexpr size_t max_samples = 64 * 1024 * 1024 / sizeof(float);
void check(int code, const char *what) {
  if (code >= 0) return;
  char error[AV_ERROR_MAX_STRING_SIZE];
  av_strerror(code, error, sizeof error);
  throw std::runtime_error(std::string(what) + ": " + error);
}
struct Decoder {
  AVFormatContext *input = nullptr;
  AVCodecContext *codec = nullptr;
  AVFrame *frame = av_frame_alloc();
  AVPacket *packet = av_packet_alloc();
  SwrContext *resample = nullptr;
  ~Decoder() {
    swr_free(&resample);
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&codec);
    avformat_close_input(&input);
  }
};
}

MovieAudio decode_movie_audio(const std::filesystem::path &path, int requested,
                             double origin) {
  MovieAudio result;
  Decoder d;
  if (!d.frame || !d.packet) throw std::runtime_error("Cannot allocate audio decoder");
  check(avformat_open_input(&d.input, path.c_str(), nullptr, nullptr), "open movie audio");
  check(avformat_find_stream_info(d.input, nullptr), "inspect movie audio");
  std::vector<int> streams;
  for (unsigned i = 0; i < d.input->nb_streams; ++i)
    if (d.input->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
      streams.push_back(static_cast<int>(i));
  result.tracks = static_cast<int>(streams.size());
  if (requested >= result.tracks) throw std::runtime_error("Audio track is out of range");
  if (streams.empty()) return result;
  result.track = requested;
  if (result.track < 0) {
    result.track = 0;
    for (int i = 0; i < result.tracks; ++i)
      if (d.input->streams[streams[i]]->codecpar->ch_layout.nb_channels == 2) {
        result.track = i;
        break;
      }
  }
  const int stream = streams[result.track];
  auto *parameters = d.input->streams[stream]->codecpar;
  auto *decoder = avcodec_find_decoder(parameters->codec_id);
  if (!decoder) throw std::runtime_error("Movie audio decoder is unavailable");
  d.codec = avcodec_alloc_context3(decoder);
  if (!d.codec) throw std::runtime_error("Cannot allocate audio codec");
  check(avcodec_parameters_to_context(d.codec, parameters), "copy audio parameters");
  check(avcodec_open2(d.codec, decoder, nullptr), "open audio decoder");
  if (d.codec->sample_rate <= 0 || d.codec->ch_layout.nb_channels <= 0)
    throw std::runtime_error("Invalid audio format");
  AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
  check(swr_alloc_set_opts2(&d.resample, &stereo, AV_SAMPLE_FMT_FLT, MovieAudio::rate,
                           &d.codec->ch_layout, d.codec->sample_fmt, d.codec->sample_rate,
                           0, nullptr), "configure audio resampler");
  check(swr_init(d.resample), "initialize audio resampler");
  bool first = true;
  size_t trim = 0;
  auto append = [&](const AVFrame *frame) {
    if (frame && first) {
      first = false;
      const auto pts = frame->best_effort_timestamp;
      double offset = pts == AV_NOPTS_VALUE ? 0 :
          pts * av_q2d(d.input->streams[stream]->time_base) - origin;
      if (!std::isfinite(offset) || std::abs(offset) > 170)
        throw std::runtime_error("Audio timestamp exceeds viewer bound");
      long long frames = std::llround(offset * MovieAudio::rate);
      if (frames > 0) result.samples.resize(static_cast<size_t>(frames) * 2, 0);
      else trim = static_cast<size_t>(-frames);
    }
    const int count = swr_get_out_samples(d.resample, frame ? frame->nb_samples : 0);
    check(count, "size audio conversion");
    if (count > static_cast<int>(max_samples / 2))
      throw std::runtime_error("Audio conversion exceeds viewer bound");
    std::vector<float> converted(static_cast<size_t>(count) * 2);
    uint8_t *output[] = {reinterpret_cast<uint8_t *>(converted.data())};
    int got = swr_convert(d.resample, output, count,
                         frame ? const_cast<const uint8_t **>(frame->extended_data) : nullptr,
                         frame ? frame->nb_samples : 0);
    check(got, "convert audio");
    const size_t skip = std::min(trim, static_cast<size_t>(got));
    trim -= skip;
    const size_t add = (static_cast<size_t>(got) - skip) * 2;
    if (add > max_samples - result.samples.size())
      throw std::runtime_error("Soundtrack exceeds 64 MiB viewer PCM limit");
    result.samples.insert(result.samples.end(), converted.begin() + skip * 2,
                          converted.begin() + static_cast<size_t>(got) * 2);
    return got;
  };
  auto receive = [&]() {
    for (;;) {
      int code = avcodec_receive_frame(d.codec, d.frame);
      if (code == AVERROR_EOF || code == AVERROR(EAGAIN)) return;
      check(code, "decode audio frame");
      if (d.frame->sample_rate != d.codec->sample_rate ||
          d.frame->format != d.codec->sample_fmt ||
          av_channel_layout_compare(&d.frame->ch_layout, &d.codec->ch_layout) != 0)
        throw std::runtime_error("Audio format changed");
      append(d.frame);
      av_frame_unref(d.frame);
    }
  };
  for (;;) {
    int code = av_read_frame(d.input, d.packet);
    if (code == AVERROR_EOF) break;
    check(code, "read audio packet");
    if (d.packet->stream_index == stream) {
      check(avcodec_send_packet(d.codec, d.packet), "send audio packet");
      receive();
    }
    av_packet_unref(d.packet);
  }
  check(avcodec_send_packet(d.codec, nullptr), "drain audio decoder");
  receive();
  while (append(nullptr) > 0) {}
  return result;
}

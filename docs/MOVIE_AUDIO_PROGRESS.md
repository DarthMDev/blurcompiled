# Native startup movie viewer with sound — October 19

The standalone ARM64 resource viewer now decodes a selected soundtrack with
FFmpeg/libswresample, queues stereo float PCM to SDL3, and schedules Metal video
presentation against consumed audio frames. This is a new implementation. It
does not execute Blur's startup state machine or implement its Bink adapter.
The translated host still uses its no-decoder placeholder; the menu and racing
remain unverified.

## Watch it

From the repository root:

```sh
./tools/play-intro.command
```

The executable launcher can also be opened from Finder. Its default is the
installed `atvi.bik`; Escape or closing the window stops playback. To choose a
different installation/movie, pass them as its first and second arguments:

```sh
./tools/play-intro.command '/path/to/Blur' BizarreSplash.bik
```

Or invoke the viewer directly:

```sh
./build/blur-movie --assets '/path/to/Blur' --movie atvi.bik
./build/blur-movie --assets '/path/to/Blur' --movie atvi.bik --audio-track 0
```

Tracks are numbered from zero among audio streams. Default selection is the
first stereo stream, or the first audio stream if none is stereo. This is a
viewer choice, not a recovered original surround mix. `--mute` skips audio;
`--decode-only` verifies decoded PCM/video without opening devices. `--frames N`
limits presentation and stops the soundtrack at that limit. Snapshot outputs
remain restricted to paths outside the reference installation.

The supplied Activision movie has four audio streams; default track 1 is stereo.
The supplied BizarreSplash movie has no audio stream, so silence there is the
file's content rather than a disabled audio implementation.

## Bounds, timing, and verification

One selected soundtrack is decoded before playback to a maximum of 64 MiB of
48 kHz stereo float PCM (about 175 seconds including leading silence). Oversized
tracks fail explicitly. Video continues to decode frame-by-frame; all reference
files are opened read-only. The first audio timestamp is aligned with the first
video timestamp, padding or trimming PCM at the start. SDL stream consumption
is the presentation clock, adjusted by one reported device buffer. After audio
ends, the timeline continues monotonically for any silent video tail. Event
polling remains active while waiting. This is a buffer-based timing estimate,
not a measurement of acoustic output latency.

The real `atvi.bik` playback check presented 225 1280×720 frames through Metal
and opened a 48 kHz stereo device with a 1,024-frame buffer. Track 1 decoded
360,960 stereo frames with nonzero peak 0.341051 and PCM CRC32 `96a08d53`.
Video stream CRC32 was `80fa6919`; 223 frame CRCs were distinct. Maximum video
lateness relative to the estimated audio clock was 0.0306333 seconds, about one
29.97-fps frame. These observations are in `reports/movie-audio-atvi-playback.log`.
Sound was routed to the Mac's playback device; perceptual synchronization is
for a listener to assess.

Synthetic fixtures compare PCM bytes/CRCs with the installed FFmpeg CLI for
stereo selection and mono 44.1-to-48 kHz conversion, check delayed-audio padding,
mute/invalid tracks, video CRCs, EOF and reference-write refusal. The explicit
desktop test uses a dummy audio device and a real video window to exercise
audio-clock pacing and drain. It passes separately; ordinary headless tests
skip that one display-dependent check. FFmpeg parity checks wrapper and
conversion behavior, not an independent implementation of the Bink codecs.

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/homebrew -DBLUR_VIDEO_SLICE=ON
cmake --build build -j 4
python3 -m unittest discover -s tests -v
ctest --test-dir build --output-on-failure
BLUR_MOVIE_PLAYBACK_TEST=1 python3 tests/test_native_movie.py build/blur-movie
```

The optional target now also needs the already-installed libswresample development
library. It downloads no dependencies. Desktop tests require macOS display
access outside the restricted tool sandbox.

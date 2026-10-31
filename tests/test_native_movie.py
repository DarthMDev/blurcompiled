"""Synthetic movie fixtures; no game installation is needed."""
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
EXPLICIT = len(sys.argv) > 1 and not sys.argv[1].startswith('-') and Path(sys.argv[1]).is_file()
BINARY = Path(sys.argv.pop(1)).resolve() if EXPLICIT else ROOT / 'build/blur-movie'
FFMPEG = shutil.which('ffmpeg')


@unittest.skipUnless(BINARY.is_file() and FFMPEG, 'Enable BLUR_VIDEO_SLICE and install FFmpeg to test the optional movie slice')
class TestNativeMovie(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.assets = self.root / 'reference'
        movies = self.assets / 'UI/movies'
        movies.mkdir(parents=True)
        self.movie = movies / 'synthetic.mkv'
        subprocess.run([FFMPEG, '-v', 'error', '-f', 'lavfi', '-i', 'testsrc2=size=32x24:rate=2',
                        '-frames:v', '4', '-c:v', 'ffv1', '-pix_fmt', 'yuv420p', str(self.movie)],
                       check=True, capture_output=True, timeout=10)

    def run_movie(self, *args):
        return subprocess.run([str(BINARY), '--assets', str(self.assets), '--movie', self.movie.name, *args],
                              text=True, capture_output=True, timeout=10)

    def test_decode_eof_and_frame_bound_match_packed_yuv(self):
        raw = subprocess.run([FFMPEG, '-v', 'error', '-i', str(self.movie), '-an',
                              '-f', 'rawvideo', '-pix_fmt', 'yuv420p', '-'],
                             check=True, capture_output=True, timeout=10).stdout
        frame_size = 32 * 24 * 3 // 2
        self.assertEqual(len(raw), 4 * frame_size)
        before = hashlib.sha256(self.movie.read_bytes()).hexdigest()
        for count, args in ((4, []), (2, ['--frames', '2'])):
            result = self.run_movie('--decode-only', *args)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(f'Decoded {count} frames, 32x24', result.stdout)
            crc = int(re.search(r'stream CRC32 ([0-9a-f]+)', result.stdout)[1], 16)
            self.assertEqual(crc, zlib.crc32(raw[:count * frame_size]))
        self.assertEqual(hashlib.sha256(self.movie.read_bytes()).hexdigest(), before)

    def test_malformed_movie_fails_without_claiming_frames(self):
        self.movie.write_bytes(b'not a movie\x00\xff')
        result = self.run_movie('--decode-only')
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn('Decoded ', result.stdout)

    def audio_fixture(self):
        self.movie = self.movie.with_name('audio.mkv')
        subprocess.run([FFMPEG, '-v', 'error', '-f', 'lavfi', '-i', 'testsrc2=size=32x24:rate=2:duration=2',
                        '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=44100:duration=2',
                        '-f', 'lavfi', '-i', 'sine=frequency=880:sample_rate=48000:duration=2',
                        '-map', '0:v', '-map', '1:a', '-map', '2:a', '-c:v', 'ffv1', '-c:a', 'pcm_f32le',
                        '-ac:a:1', '2', str(self.movie)], check=True, capture_output=True, timeout=10)

    def test_audio_track_selection_and_pcm_match_ffmpeg(self):
        self.audio_fixture()
        before = hashlib.sha256(self.movie.read_bytes()).hexdigest()
        for track, options in ((1, []), (0, ['--audio-track', '0'])):
            expected = subprocess.run([FFMPEG, '-v', 'error', '-i', str(self.movie), '-map', f'0:a:{track}',
                                       '-ar', '48000', '-ac', '2', '-f', 'f32le', '-'],
                                      check=True, capture_output=True, timeout=10).stdout
            result = self.run_movie('--decode-only', *options)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(f'Audio: track {track}/2, {len(expected) // 8} stereo frames', result.stdout)
            crc = int(re.search(r'PCM CRC32 ([0-9a-f]+)', result.stdout)[1], 16)
            self.assertEqual(crc, zlib.crc32(expected))
        self.assertEqual(hashlib.sha256(self.movie.read_bytes()).hexdigest(), before)
        muted = self.run_movie('--decode-only', '--mute')
        self.assertEqual(muted.returncode, 0, muted.stderr)
        self.assertIn('Audio: muted', muted.stdout)
        self.assertNotEqual(self.run_movie('--decode-only', '--audio-track', '2').returncode, 0)

    def test_timestamp_offset_pads_audio_against_video_origin(self):
        self.movie = self.movie.with_name('offset.mkv')
        subprocess.run([FFMPEG, '-v', 'error', '-f', 'lavfi', '-i', 'testsrc2=size=32x24:rate=2:duration=2',
                        '-itsoffset', '0.25', '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000:duration=1',
                        '-map', '0:v', '-map', '1:a', '-c:v', 'ffv1', '-c:a', 'pcm_f32le', '-ac', '2', str(self.movie)],
                       check=True, capture_output=True, timeout=10)
        raw = subprocess.run([FFMPEG, '-v', 'error', '-i', str(self.movie), '-map', '0:a', '-f', 'f32le', '-'],
                             check=True, capture_output=True, timeout=10).stdout
        result = self.run_movie('--decode-only')
        self.assertEqual(result.returncode, 0, result.stderr)
        crc = int(re.search(r'PCM CRC32 ([0-9a-f]+)', result.stdout)[1], 16)
        self.assertEqual(crc, zlib.crc32(bytes(12000 * 8) + raw))

    @unittest.skipUnless(os.environ.get('BLUR_MOVIE_PLAYBACK_TEST') == '1', 'Opt-in desktop playback check requires a display')
    def test_dummy_audio_playback_drains_on_audio_clock(self):
        self.audio_fixture()
        result = subprocess.run([str(BINARY), '--assets', str(self.assets), '--movie', self.movie.name],
                                env={**os.environ, 'SDL_AUDIO_DRIVER': 'dummy'},
                                text=True, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('Presentation clock: audio consumed frames', result.stdout)
        lateness = float(re.search(r'max video lateness ([0-9.e+-]+)', result.stdout)[1])
        self.assertLess(lateness, 0.2)

    def test_reference_snapshot_write_is_refused(self):
        before = self.movie.read_bytes()
        result = self.run_movie('--frames', '1', '--snapshot', str(self.movie))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('outside the installation', result.stderr)
        self.assertEqual(self.movie.read_bytes(), before)


if __name__ == '__main__':
    unittest.main()

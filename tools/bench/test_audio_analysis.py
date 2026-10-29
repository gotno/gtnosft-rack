"""Run: .venv/bin/python -m unittest test_audio_analysis -v (from tools/bench)"""

import math
import struct
import unittest

import audio_analysis as aa


def pcm_payload(values):
    return struct.pack(f"<{len(values)}h", *values)


def packet(seq, epoch=0, redundancy=0, frame_samples=480, codec=aa.CODEC_OPUS, preset=1):
    payloads = [bytes([s & 0xFF]) for s in range(seq, max(-1, seq - 1 - redundancy), -1)]
    return aa.parse_packet(aa.build_packet(4242, epoch, codec, frame_samples, preset, seq, payloads))


class PacketTests(unittest.TestCase):
    def test_round_trip(self):
        data = aa.build_packet(7, 3, aa.CODEC_OPUS, 480, 1, 10, [b"new", b"old"])
        self.assertEqual(len(data), 20 + 2 + 3 + 2 + 3)
        p = aa.parse_packet(data)
        self.assertEqual((p.stream_id, p.epoch, p.codec, p.channels), (7, 3, 1, 2))
        self.assertEqual((p.frame_samples, p.preset_id, p.seq), (480, 1, 10))
        self.assertEqual(p.frames, [(10, b"new"), (9, b"old")])

    def test_matches_rack_layout(self):
        # same bytes as tests/test_packet.cpp packet_header_layout
        data = aa.build_packet(0x04030201, 0x0605, 1, 480, 1, 0x0A090807, [b"\xaa\xbb\xcc", b"\x11"])
        self.assertEqual(
            data[:20],
            bytes([82, 65, 1, 0, 1, 2, 3, 4, 5, 6, 1, 2, 0xE0, 1, 2, 1, 7, 8, 9, 10]),
        )

    def test_rejects_bad_packets(self):
        good = aa.build_packet(7, 0, 1, 480, 1, 0, [b"abc"])
        with self.assertRaises(ValueError):
            aa.parse_packet(b"XX" + good[2:])
        with self.assertRaises(ValueError):
            aa.parse_packet(good[:-1])
        with self.assertRaises(ValueError):
            aa.parse_packet(good[:10])


class TrackerTests(unittest.TestCase):
    def test_loss_recovery_duplicates_reorder(self):
        t = aa.StreamTracker()
        fs = 0.01
        # seq 3 missing entirely, seq 5 only arrives as a redundant copy in 6,
        # seq 7 duplicated, seq 9 arrives before 8
        order = [0, 1, 2, 4, 6, 7, 7, 9, 8, 10]
        for i, s in enumerate(order):
            t.feed(packet(s, redundancy=1), i * fs, 100)
        t.close()
        s = t.summary()
        self.assertEqual(s["duplicates"], 1)
        self.assertEqual(s["reordered"], 1)
        # 3 is covered by 4's redundant copy, 5 by 6's
        self.assertEqual(s["recovered"], 2)
        self.assertEqual(s["lost"], 0)
        self.assertEqual(s["frames_expected"], 11)

    def test_loss_without_redundancy(self):
        t = aa.StreamTracker()
        for s in (0, 1, 3, 4):
            t.feed(packet(s), s * 0.01, 50)
        t.close()
        self.assertEqual(t.summary()["lost"], 1)

    def test_epoch_change_closes_previous(self):
        t = aa.StreamTracker()
        for s in (0, 2):
            t.feed(packet(s, epoch=0), s * 0.01, 50)
        for s in (0, 1):
            t.feed(packet(s, epoch=1), 0.1 + s * 0.01, 50)
        t.close()
        s = t.summary()
        self.assertEqual(s["epoch_changes"], 1)
        self.assertEqual(s["lost"], 1)

    def test_jitter_and_drift(self):
        t = aa.StreamTracker()
        fs = 0.01
        # sender clock 100 ppm fast: media time runs ahead of arrival time
        for s in range(500):
            arrival = s * fs / 1.0001 + (0.004 if s % 50 == 0 else 0.0)
            t.feed(packet(s), arrival, 50)
        t.close()
        s = t.summary()
        self.assertAlmostEqual(s["drift_ppm"], 100, delta=10)
        self.assertGreater(s["jitter_ms"]["max"], 3.5)
        self.assertLess(s["jitter_ms"]["p50"], 1.0)


class JitterSimTests(unittest.TestCase):
    def test_steady_stream_no_underrun(self):
        sim = aa.JitterSim(40)
        for s in range(200):
            sim.feed(packet(s), s * 0.01 + (0.02 if s % 20 == 0 else 0))
        self.assertEqual((sim.underruns, sim.lost), (0, 0))

    def test_burst_longer_than_target_underruns(self):
        sim = aa.JitterSim(20)
        t = 0.0
        for s in range(100):
            t = s * 0.01 + (0.06 if 50 <= s < 53 else 0)
            sim.feed(packet(s), t)
        self.assertGreaterEqual(sim.underruns, 1)

    def test_redundant_copy_counts_as_recovered(self):
        sim = aa.JitterSim(40)
        for s in range(20):
            if s == 10:
                continue
            sim.feed(packet(s, redundancy=1), s * 0.01)
        self.assertEqual((sim.recovered, sim.lost, sim.underruns), (1, 0, 0))

    def test_missing_frame_with_later_frames_is_lost(self):
        sim = aa.JitterSim(40)
        for s in range(20):
            if s == 10:
                continue
            sim.feed(packet(s), s * 0.01)
        self.assertEqual((sim.lost, sim.underruns), (1, 0))


class SignalTests(unittest.TestCase):
    def sine(self, n, f=1000, amp=0.5, sr=48000):
        return [amp * math.sin(2 * math.pi * f * i / sr) for i in range(n)]

    def test_clean_sine(self):
        r = aa.analyze_sine(self.sine(48000), 48000)
        self.assertAlmostEqual(r["frequency_hz"], 1000, delta=1)
        self.assertEqual((r["glitches"], r["dropouts"]), (0, 0))

    def test_discontinuity(self):
        x = self.sine(48000)
        x = x[:20000] + x[20100:]  # phase jump
        self.assertEqual(aa.analyze_sine(x, 48000)["glitches"], 1)

    def test_dropout(self):
        x = self.sine(48000)
        x[10000:10480] = [0.0] * 480
        r = aa.analyze_sine(x, 48000)
        self.assertEqual(r["dropouts"], 1)

    def test_decode_pcm_fills_missing_with_silence(self):
        a = pcm_payload([32767, -32767, 0, 0])
        out = aa.decode_stream(aa.CODEC_PCM, 2, {0: a, 2: a}, 0, 2)
        self.assertEqual(len(out), 12)
        self.assertAlmostEqual(out[0], 1.0, places=3)
        self.assertEqual(out[4:8], [0.0] * 4)


if __name__ == "__main__":
    unittest.main()

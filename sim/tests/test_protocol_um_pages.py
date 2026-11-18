# sim/tests/test_protocol_um_pages.py - 52 UM PAGES in sim/protocol.py (0.5.0-pages, docs/um-pages.md): the same bytes as
# the C builders (teensy/libraries/RytmPlugins/extras/proto_test.cpp -DPROTO_DUMP_PAGES), round trips, refusals, and the
# 14-bit <-> entry mapping as rmp_api.h / tools/rmp_pack.py have it (every entry round-trips, detents land on entries).
# run: cd sim/tests && PYTHONPATH=.. ../../.venv/bin/python -m unittest test_protocol_um_pages
import os, subprocess, sys, tempfile, unittest
import protocol as P

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
LIB = os.path.join(ROOT, "teensy", "libraries", "RytmPlugins")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import rmp_pack  # noqa: E402

HI = [dict(p=17, look="dial", kind="number", min=1, max=20, default=3, long="Ratio", short="RAT"),
      dict(p=25, look="list", kind="enum", min=0, max=127, default=0, enum_count=3, long="Wave", short="WAV")]
PAIR = [f"B{i + 1}" for i in range(19)]


class CrossCheck(unittest.TestCase):
    def test_same_bytes_as_c(self):
        with tempfile.TemporaryDirectory() as d:
            exe = os.path.join(d, "dump")
            src = os.path.join(LIB, "src")
            subprocess.run(["c++", "-std=c++17", "-O1", "-DPROTO_DUMP_PAGES", "-I", src, os.path.join(LIB, "extras", "proto_test.cpp"),
                            os.path.join(src, "rmp_proto.cpp"), os.path.join(src, "rmp_manifest.cpp"),
                            os.path.join(src, "rmp_loader.cpp"), "-o", exe], check=True)
            got = subprocess.run([exe], check=True, capture_output=True, text=True).stdout.split()
        want = [P.pg_head(2, 9, HI, 2, "OPERATORS", "ENVELOPES"), P.pg_knobs(2, 9, HI),
                P.pg_steps(2, 9, [(1, P.STEP_PAIR, 361, 8128, 19), (2, P.STEP_NUMBER, 100, 1313, 0)]),
                P.pg_pair(2, 9, 1, PAIR, 0), P.pg_pair(2, 9, 1, PAIR, 12), P.pg_values(3, 2, 17, list(range(100, 116))),
                P.pg_head(4, 1)]
        self.assertEqual(got, [m.hex() for m in want])


class RoundTrip(unittest.TestCase):
    def test_messages(self):
        dec = lambda m: P.decode_pg(P.parse(m)[1])  # noqa: E731
        self.assertEqual(P.parse(P.pg_ask(3))[0], P.UM_PAGES)
        h = dec(P.pg_head(2, 9, HI, 2, "OPERATORS", "ENVELOPES"))
        self.assertEqual((h["nhi"], h["pages"], h["last"], h["nstep"], h["title3"]), (2, 3, 25, 2, "OPERATORS"))
        self.assertEqual(dec(P.pg_head(1, 1, [dict(p=20)]))["pages"], 1)
        k = dec(P.pg_knobs(2, 9, HI))["knobs"]
        self.assertEqual([(x["p"], x["look"], x["default"], x["enum_count"], x["short"]) for x in k],
                         [(17, "dial", 3, 0, "RAT"), (25, "list", 0, 3, "WAV")])
        s = dec(P.pg_steps(2, 9, [(4, P.STEP_PAIR, 361, 8128, 19)]))["steps"]
        self.assertEqual(s, [dict(p=4, kind=1, steps=361, def14=8128, npair=19)])
        pr = dec(P.pg_pair(2, 9, 4, PAIR, 12))
        self.assertEqual((pr["first"], pr["total"], pr["names"][-1]), (12, 19, "B19"))
        self.assertEqual(dec(P.pg_values(7, 5, 32, [127])), dict(kind="values", voice=7, slot=5, first=32, values=[127]))
        self.assertEqual(dec(P.pg_ask()), dict(kind="ask", slot=0))

    def test_refused(self):
        for bad in ([1, 4, 0, 1, 32, 2, 1, 2],      # past P32
                    [1, 4, 8, 1, 17, 1, 0],         # voice 8
                    [1, 4, 0, 6, 17, 1, 0],         # slot 6
                    [1, 4, 0, 1, 17, 2, 0],         # short
                    [2, 4, 0, 1, 17, 1, 0],         # another version
                    [1, 9, 0],                      # another kind
                    [1, 5, 6],                      # ASK slot 6
                    [1, 2, 1, 1, 1, 4, 1, 2, 104, 0, 0, 19]):   # a pair whose steps are not n * n
            self.assertIsNone(P.decode_pg(bytes(bad)), bad)
        for f in (lambda: P.pg_values(0, 1, 16, [1]), lambda: P.pg_values(0, 1, 32, [1, 2]), lambda: P.pg_values(8, 1, 17, [1]),
                  lambda: P.pg_knobs(1, 1, [dict(p=16)]), lambda: P.pg_steps(1, 1, [(17, 0, 5, 0, 0)]),
                  lambda: P.pg_pair(1, 1, 1, ["A"])):
            with self.assertRaises(ValueError):
                f()


class Steps(unittest.TestCase):
    def test_mapping(self):
        for n in (2, 3, 5, 100, 361, 16255, 16256):
            self.assertEqual([P.step_entry(n, P.step_v14(n, q)) for q in range(n)], list(range(n)))
            self.assertEqual((P.step_v14(n, 0), P.step_v14(n, n - 1), P.step_entry(n, 16383)), (0, 16256, n - 1))
            self.assertEqual([P.step_v14(n, q) for q in (0, n // 2, n - 1)],
                             [rmp_pack.step_v14(n, q) for q in (0, n // 2, n - 1)])
        self.assertEqual((P.step_v14(361, 180), P.step_entry(361, 8128), P.pair_text(PAIR, 180)), (8128, 180, "B10 / B10"))

    def test_detents(self):
        """A Rytm detent from any 14-bit value lands on the neighbouring entry's exact value: 360 detents walk 361's."""
        v, seen = 0, [0]
        for _ in range(400):
            v = P.step_detent(361, v, 1)
            seen.append(P.step_entry(361, v))
        self.assertEqual(seen[:361], list(range(361)))
        self.assertEqual(P.step_entry(361, v), 360)
        self.assertEqual(P.step_detent(361, 8000, 1), P.step_v14(361, P.step_entry(361, 8000) + 1))
        self.assertEqual(P.step_detent(361, 45, -5), 0)


if __name__ == "__main__":
    unittest.main()

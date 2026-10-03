#!/usr/bin/env python3
"""Unit tests for class_sweep.py: the creator list, log parsing, failure attribution and the link bisect (no app runs)."""
import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import class_sweep as cs  # noqa: E402

OK_LINE = "[class-sweep] RESULT {i} {n} status={s} actors=1 alive=1 model=1 drawn=1 hidden=0 clipped=0 validdraw=1 move=0 actor=X created=1 row=synth zone=0 mount_ms=1.0 create_ms=0.5 archives=A,B nonresident=B tpl_links=SW_A:3"


def log(*items, tail=()):
    lines = ["PETARI noise", "[class-sweep] start: first normal-play frame"]
    for i, (name, state, status) in enumerate(items):
        lines.append(f"[class-sweep] BEGIN {i} {name}")
        if state in ("created", "result", "killed"):
            lines.append(f"[class-sweep] CREATED {i} {name} created=1")
        if state in ("result", "killed"):
            lines.append(OK_LINE.format(i=i, n=name, s=status))
        if state == "killed":
            lines.append(f"[class-sweep] KILLED {i} {name}")
    return "\n".join(lines + list(tail)) + "\n"


JOBS = [{"class": n, "mode": "empty", "overrides": ""} for n in ("A", "B", "C")]


def records(text, stalled=False):
    parsed = cs.parse_log(text)
    return parsed, [cs.record_for(j, i, parsed["items"].get(i), parsed, stalled, 0, 3) for i, j in enumerate(JOBS)]


class FactoryList(unittest.TestCase):
    def test_every_creator_is_listed_once(self):
        classes = cs.list_classes()
        self.assertGreater(len(classes), 1100)
        self.assertEqual(len({c["class"].lower() for c in classes}), len(classes))
        by = {c["class"]: c for c in classes}
        self.assertTrue(by["SwitchCube"]["area_like"])
        self.assertFalse(by["Takobo"]["area_like"])
        self.assertEqual(by["Takobo"]["archive"], "Takobo")

    def test_area_jobs_get_a_small_scale(self):
        jobs = cs.make_jobs([{"class": "SwitchCube", "area_like": True}, {"class": "Coin", "area_like": False}])
        self.assertIn("scale_x=0.02", jobs[0]["overrides"])
        self.assertEqual(jobs[1]["overrides"], "")


class Attribution(unittest.TestCase):
    def test_clean_batch(self):
        text = log(("A", "killed", "ok"), ("B", "killed", "dead"), ("C", "killed", "invisible")) + "[class-sweep] BATCH-DONE 3\n"
        parsed, recs = records(text)
        self.assertTrue(parsed["done"])
        self.assertEqual([r["status"] for r in recs], ["ok", "dead", "invisible"])
        self.assertEqual(recs[0]["metrics"]["archives"], "A,B")

    def test_crash_blames_the_in_flight_class_and_skips_the_rest(self):
        text = log(("A", "killed", "ok"), ("B", "begin", None)) + "==== Petari native crash report ====\nsignal: SIGSEGV\n"
        parsed, recs = records(text)
        self.assertEqual(recs[0]["status"], "ok")
        self.assertEqual((recs[1]["status"], recs[1]["phase"]), ("crash", "create"))
        self.assertIsNone(recs[2])
        self.assertEqual(parsed["crash"]["signal"], "signal: SIGSEGV")

    def test_assert_and_phase(self):
        text = log(("A", "created", None)) + "==== Petari native crash report ====\nsignal: SIGABRT\nlast panic: bad rail\n"
        _, recs = records(text)
        self.assertEqual((recs[0]["status"], recs[0]["phase"]), ("assert", "observe"))

    def test_crash_in_kill_keeps_the_observation(self):
        text = log(("A", "result", "ok")) + "==== Petari native crash report ====\nsignal: SIGSEGV\n"
        _, recs = records(text)
        self.assertEqual((recs[0]["status"], recs[0]["phase"], recs[0]["observed"]), ("crash", "kill", "ok"))

    def test_stall_is_a_hang(self):
        _, recs = records(log(("A", "created", None)), stalled=True)
        self.assertEqual(recs[0]["status"], "hang")

    def test_heap_failure_is_oom(self):
        _, recs = records(log(("A", "begin", None)) + "Native heap allocation failed (12345 bytes)\n")
        self.assertEqual(recs[0]["status"], "oom")

    def test_abort_while_observing_blames_the_class(self):
        text = log(("A", "created", None)) + "[class-sweep] ABORT mario-died\n"
        _, recs = records(text)
        self.assertEqual(recs[0]["status"], "kills-player")

    def test_boot_failure_records_nothing(self):
        _, recs = records("PETARI boot failed\n")
        self.assertEqual(recs, [None, None, None])


class BisectSearch(unittest.TestCase):
    def run_search(self, search, passes):
        """passes(job) -> bool; returns the verdict."""
        for _ in range(40):
            job = search.jobs()
            if job is None:
                break
            search.feed({"status": "ok" if passes(job) else "crash"})
        return search.verdict

    def test_template_links_are_reduced_to_the_required_one(self):
        search = cs.Bisect({"class": "X"}, {}, "")
        job = search.jobs()
        self.assertEqual(job["mode"], "keep")
        search.feed({"status": "ok", "metrics": {"row": "cloned", "tpl_links": "SW_A:3,CommonPath_ID:7,GroupId:2"}})
        verdict = self.run_search(search, lambda j: "CommonPath_ID=keep" in j["overrides"])
        self.assertEqual(verdict["result"], "required")
        self.assertEqual(verdict["fields"], {"CommonPath_ID": 7})

    def test_keep_that_passes_on_a_synthesized_row_proves_nothing(self):
        search = cs.Bisect({"class": "X"}, {"SW_A": [9]}, "")
        search.jobs()
        search.feed({"status": "ok", "metrics": {"row": "synth", "tpl_links": "-"}})
        self.assertEqual(search.jobs()["mode"], "empty")  # went on to provision real ids

    def test_provisioned_ids_are_reduced(self):
        search = cs.Bisect({"class": "X"}, {"CommonPath_ID": [4], "SW_A": [9], "GroupId": [1]}, "scale_x=0.02")
        verdict = self.run_search(search, lambda j: "CommonPath_ID=4" in j["overrides"])
        self.assertEqual(verdict, {"result": "required", "fields": {"CommonPath_ID": 4}, "source": "drop-provision"})

    def test_class_that_fails_everywhere_is_unresolved(self):
        search = cs.Bisect({"class": "X"}, {"SW_A": [9]}, "")
        self.assertEqual(self.run_search(search, lambda j: False)["result"], "unresolved")

    def test_nothing_required_when_one_field_set_is_not_needed(self):
        search = cs.Bisect({"class": "X"}, {"SW_A": [9]}, "")
        verdict = self.run_search(search, lambda j: True)
        self.assertEqual(verdict["fields"], {})


class Verdict(unittest.TestCase):
    def test_live_recreate(self):
        v = cs.live_recreate_verdict
        self.assertEqual(v({}), "unmeasured")
        self.assertEqual(v({"a": {"status": "ok"}, "b": {"status": "nonactor"}}), "safe")
        self.assertEqual(v({"a": {"status": "ok"}, "b": {"status": "invisible"}}), "suspect")
        # empty-link crash, nothing known about the row's own links
        self.assertEqual(v({"a": {"status": "ok"}, "b": {"status": "crash"}}), "unverified")
        # crashes with empty links but is fine from its own row with its links: safe
        keep = {"status": "crash", "links": {"result": "required", "source": "drop-keep", "fields": {"CommonPath_ID": 7}}}
        self.assertEqual(v({"a": keep}), "safe")
        # still fails from its own row, or only works with provisioned ids: unsafe
        self.assertEqual(v({"a": {"status": "crash", "links": {"result": "unresolved"}}}), "unsafe")
        provisioned = {"status": "crash", "links": {"result": "required", "source": "drop-provision", "fields": {"SW_A": 1}}}
        self.assertEqual(v({"a": provisioned}), "unsafe")


class Scrub(unittest.TestCase):
    def test_absolute_paths_and_thread_ids_do_not_reach_the_report(self):
        home = "/Users/someone/.bb/plugins/env/host-data/workspaces/thr_abcdef1234/petari-worktrees/editor-class-sweep"
        data = {"crash": {"panic": f"File isn't exist. in \"{home}/src/Game/System/FileRipper.cpp\" on line 112.",
                          "top_frames": [f"{home}/native/app/x.cpp", "/var/folders/ab/cd/T/cache/x", "plain_symbol"]},
                "note": "see /home/runner/work/petari/petari/include/Foo.hpp and /Users/me/Library/Caches/x for thr_xi2xyxi3dz"}
        text = json.dumps(cs.scrub(data))
        for leak in ("/Users/", "/home/", "/var/folders", "thr_", "someone", "worktrees"):
            self.assertNotIn(leak, text)
        self.assertIn("src/Game/System/FileRipper.cpp", text)
        self.assertIn("include/Foo.hpp", text)
        self.assertIn("plain_symbol", text)

    def test_markdown_is_scrubbed_too(self):
        self.assertNotIn("/Users/", cs.scrub("| X | /Users/a/b/c.cpp |"))


class Harvest(unittest.TestCase):
    def test_decode_bcsv_reads_signed_ints(self):
        import struct
        data = struct.pack(">IIII", 1, 1, 32, 4) + struct.pack(">IIHBB", 0x1234, 0xFFFFFFFF, 0, 0, 0) + bytes(4) + b"\xff\xff\xff\xfe"
        self.assertEqual(list(cs.decode_bcsv(data, {0x1234: "SW_A"})), [{"SW_A": -2}])


if __name__ == "__main__":
    unittest.main()

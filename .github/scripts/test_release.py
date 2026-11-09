import contextlib
import io
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import release
import yaml


ROOT = Path(__file__).resolve().parents[2]
SHA = "a" * 40


class VersionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "sodium").mkdir()
        for name in ("CMakeLists.txt", "Info.plist.in"):
            shutil.copyfile(ROOT / "sodium" / name, self.root / "sodium" / name)
        self.write("2.3.4\n")

    def write(self, text):
        (self.root / "VERSION").write_text(text)

    def test_stable_version_and_bumps(self):
        self.assertEqual(release.version(self.root), "2.3.4")
        for mode, expected in (("patch", "2.3.5"), ("minor", "2.4.0"), ("major", "3.0.0")):
            with self.subTest(mode=mode):
                self.write("2.3.4\n")
                self.assertEqual(release.bump(mode, self.root), expected)
                self.assertEqual(release.version(self.root), expected)

    def test_invalid_versions(self):
        for text in ("", "1.2", "v1.2.3", "01.2.3", "1.02.3", "1.2.03",
                     "1.2.3-beta", "1.2.3+4", "1.2.3\n\n", " 1.2.3\n", "1.2.3\n4.5.6"):
            with self.subTest(text=text):
                self.write(text)
                with self.assertRaises(ValueError):
                    release.version(self.root)

    def test_cmake_drift(self):
        path = self.root / "sodium/CMakeLists.txt"
        path.write_text(path.read_text().replace(
            'MACOSX_BUNDLE_BUNDLE_VERSION "${PLUGIN_VERSION}"',
            'MACOSX_BUNDLE_BUNDLE_VERSION "9.9.9"'))
        with self.assertRaisesRegex(ValueError, "CMake"):
            release.version(self.root)

    def test_plist_drift(self):
        path = self.root / "sodium/Info.plist.in"
        path.write_text(path.read_text().replace("${MACOSX_BUNDLE_SHORT_VERSION_STRING}", "9.9.9"))
        with self.assertRaisesRegex(ValueError, "CFBundleShortVersionString"):
            release.version(self.root)

    def test_live_metadata(self):
        self.assertEqual(release.version(ROOT), (ROOT / "VERSION").read_text().strip())


class AvailabilityTests(unittest.TestCase):
    @staticmethod
    def response(status, code):
        return subprocess.CompletedProcess([], code, f"HTTP/2.0 {status}\n\n{{}}", "API error")

    def test_only_confirmed_missing_is_available(self):
        with patch.object(release.subprocess, "run", return_value=self.response(404, 1)) as api:
            release.available("owner/repo", "v2.3.4")
        self.assertEqual(api.call_count, 2)

    def test_existing_tag_or_release(self):
        for responses in ([self.response(200, 0)],
                          [self.response(404, 1), self.response(200, 0)]):
            with patch.object(release.subprocess, "run", side_effect=responses):
                with self.assertRaisesRegex(ValueError, "already exists"):
                    release.available("owner/repo", "v2.3.4")

    def test_api_errors_fail_closed(self):
        for result in (self.response(401, 1), self.response(403, 1),
                       self.response(429, 1), self.response(500, 1),
                       subprocess.CompletedProcess([], 1, "", "network failed"),
                       self.response(404, 0)):
            with self.subTest(result=result), patch.object(release.subprocess, "run", return_value=result):
                with self.assertRaises(RuntimeError):
                    release.available("owner/repo", "v2.3.4")


class PublicationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.assets = [str(Path(self.temp.name) / name) for name in ("sodium.zip", "sodium.dmg")]
        for path in self.assets:
            Path(path).write_bytes(b"fixture")
        self.version = patch.object(release, "version", return_value="2.3.4")
        self.version.start()
        self.addCleanup(self.version.stop)

    def publish(self):
        release.publish("owner/repo", "v2.3.4", SHA, self.assets)

    def test_recheck_then_nonforce_tag_exact_sha_then_create(self):
        events = []
        with patch.object(release, "available", side_effect=lambda *a: events.append(("check", *a))), \
             patch.object(release, "run", side_effect=lambda *a: events.append(a) or SHA + "\n"):
            self.publish()
        self.assertEqual(events, [
            ("git", "rev-parse", "HEAD"), ("check", "owner/repo", "v2.3.4"),
            ("git", "tag", "v2.3.4", SHA),
            ("git", "push", "origin", "refs/tags/v2.3.4:refs/tags/v2.3.4"),
            ("gh", "api", "--method", "POST", "repos/owner/repo/releases",
             "-f", "tag_name=v2.3.4", "-f", f"target_commitish={SHA}",
             "-f", "name=v2.3.4", "-f", f"body=Built from {SHA}.",
             "-F", "draft=false", "-F", "prerelease=false"),
            ("gh", "release", "upload", "v2.3.4", *self.assets, "--repo", "owner/repo"),
        ])

    def test_existing_publication_blocks_tag(self):
        with patch.object(release, "run", return_value=SHA), \
             patch.object(release, "available", side_effect=ValueError("already exists")):
            with self.assertRaises(ValueError):
                self.publish()

    def test_wrong_checkout_blocks_publication(self):
        with patch.object(release, "run", return_value="b" * 40), \
             patch.object(release, "available") as available:
            with self.assertRaises(ValueError):
                self.publish()
            available.assert_not_called()

    def test_version_mismatch_blocks_publication(self):
        with patch.object(release, "run", return_value=SHA), \
             patch.object(release, "version", return_value="3.0.0"), \
             patch.object(release, "available") as available:
            with self.assertRaisesRegex(ValueError, "source metadata"):
                self.publish()
            available.assert_not_called()

    def test_invalid_sha_blocks_commands(self):
        with patch.object(release, "run") as run:
            with self.assertRaisesRegex(ValueError, "immutable"):
                release.publish("owner/repo", "v2.3.4", "main", self.assets)
            run.assert_not_called()

    def test_missing_asset_blocks_publication(self):
        Path(self.assets[0]).unlink()
        with patch.object(release, "run", return_value=SHA), \
             patch.object(release, "available") as available:
            with self.assertRaises(ValueError):
                self.publish()
            available.assert_not_called()

    def test_failed_tag_push_never_creates_release(self):
        failure = subprocess.CalledProcessError(1, ["git", "push"], stderr="tag exists")
        with patch.object(release, "available"), \
             patch.object(release, "run", side_effect=[SHA, "", failure]) as run:
            with self.assertRaises(subprocess.CalledProcessError):
                self.publish()
            self.assertEqual(run.call_count, 3)

    def test_creation_and_upload_failures_preserve_partial_state(self):
        for failing_step in ("create", "upload"):
            with self.subTest(failing_step=failing_step):
                remote = {"tag": None, "release": False, "assets": []}
                commands = []

                def execute(*args):
                    commands.append(args)
                    if args == ("git", "rev-parse", "HEAD"):
                        return SHA
                    if args[:2] == ("git", "tag"):
                        return ""
                    if args[:2] == ("git", "push"):
                        remote["tag"] = SHA
                        return ""
                    if args[:4] == ("gh", "api", "--method", "POST"):
                        if failing_step == "create":
                            raise subprocess.CalledProcessError(1, args, stderr="creation failed")
                        remote["release"] = True
                        return "{}"
                    if args[:3] == ("gh", "release", "upload"):
                        remote["assets"].append(self.assets[0])
                        raise subprocess.CalledProcessError(1, args, stderr="upload failed")
                    self.fail(f"Unexpected publication command: {args}")

                summary = Path(self.temp.name) / "summary"
                with patch.object(release, "available"), \
                     patch.object(release, "run", side_effect=execute), \
                     patch.dict(release.os.environ, {"GITHUB_STEP_SUMMARY": str(summary)}), \
                     contextlib.redirect_stderr(io.StringIO()) as errors:
                    with self.assertRaises(subprocess.CalledProcessError):
                        self.publish()
                self.assertEqual(remote["tag"], SHA)
                self.assertEqual(remote["release"], failing_step == "upload")
                self.assertEqual(remote["assets"], self.assets[:1] if failing_step == "upload" else [])
                self.assertIn("recover manually", errors.getvalue())
                self.assertIn("never deletes or overwrites", summary.read_text())
                self.assertEqual(len(commands), 4 if failing_step == "create" else 5)
                for args in commands:
                    self.assertFalse({"DELETE", "PATCH", "--clobber", "--force", "--delete",
                                      "delete", "edit", "create"}.intersection(args))

    def test_notary_must_accept(self):
        path = Path(self.temp.name) / "notary.json"
        for status in ("Invalid", "In Progress", None):
            path.write_text('{"status": ' + (f'"{status}"' if status else "null") + "}")
            with self.assertRaises(ValueError):
                release.notarized(path)
        path.write_text('{"status":"Accepted"}')
        release.notarized(path)


class WorkflowTests(unittest.TestCase):
    @staticmethod
    def workflow(name):
        return yaml.load((ROOT / ".github/workflows" / name).read_text(), Loader=yaml.BaseLoader)

    def test_manual_no_input_immutable_serialized_build(self):
        workflow = self.workflow("release.yml")
        self.assertEqual(workflow["name"], "Build")
        self.assertEqual(workflow["on"], {"workflow_dispatch": ""})
        self.assertEqual(workflow["concurrency"]["group"], "release-publication")
        self.assertEqual(workflow["concurrency"]["cancel-in-progress"], "false")
        for job in workflow["jobs"].values():
            checkout = next(s for s in job["steps"] if s.get("uses", "").startswith("actions/checkout@"))
            self.assertEqual(checkout["with"]["ref"], "${{ github.sha }}")
        self.assertEqual(workflow["jobs"]["build-windows"]["needs"], "version")
        self.assertEqual(workflow["jobs"]["build-macos"]["needs"], ["version", "build-windows"])

    def test_publication_is_last_after_verification_and_notarization(self):
        steps = self.workflow("release.yml")["jobs"]["build-macos"]["steps"]
        script = "\n".join(step.get("run", "") for step in steps)
        self.assertIn("lipo -info", script)
        self.assertEqual(script.count("release.py notarized"), 2)
        self.assertLess(script.index("stapler validate"), script.index("release.py publish"))
        self.assertIn("release.py publish", steps[-1]["run"])
        self.assertNotIn("action-gh-release", str(steps))

    def test_bump_only_pushes_selected_branch(self):
        workflow = self.workflow("bump-version.yml")
        mode = workflow["on"]["workflow_dispatch"]["inputs"]["mode"]
        self.assertEqual(mode["options"], ["patch", "minor", "major"])
        steps = workflow["jobs"]["bump"]["steps"]
        self.assertEqual(steps[0]["with"]["ref"], "${{ github.sha }}")
        script = "\n".join(s.get("run", "") for s in steps)
        self.assertIn('if [ "$REMOTE_SHA" != "$SELECTED_SHA" ]', script)
        self.assertIn('git push origin "HEAD:refs/heads/$SELECTED_BRANCH"', script)
        self.assertNotIn("git tag", script)
        self.assertNotIn("--force", script)


if __name__ == "__main__":
    unittest.main()

from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from tools import release


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "repo"
        self.remote = Path(self.temp.name) / "remote"
        self.root.mkdir()
        self.git("init", "--quiet", "--initial-branch=main")
        self.git("config", "user.name", "Release Test")
        self.git("config", "user.email", "release@example.invalid")
        (self.root / "VERSION").write_text("0.1.0\n")
        (self.root / "CHANGELOG.md").write_text("# Changelog\n")
        self.git("add", "VERSION", "CHANGELOG.md")
        self.git("commit", "--quiet", "-m", "Test fixture")
        self.git("init", "--quiet", "--bare", "--initial-branch=main", str(self.remote))
        self.git("remote", "add", "origin", str(self.remote))
        self.git("push", "--quiet", "-u", "origin", "main")
        self.before = self.git("rev-parse", "HEAD")
        self.addCleanup(patch.stopall)
        patch("tools.release.shutil.which", return_value="/test/git-cliff").start()

    def git(self, *arguments):
        return subprocess.check_output(["git", *arguments], cwd=self.root, text=True,
                                       stderr=subprocess.PIPE).strip()

    def fake_cliff(self, arguments, root=release.ROOT):
        if arguments[0] == "git-cliff":
            Path(arguments[-1]).write_text("# Changelog\n\n## v0.2.0\n\n- Improved release tooling.\n")
            return ""
        return self.original_run(arguments, root)

    def test_version_and_ci_tag_validation(self):
        for invalid in ("v1.2.3", "01.2.3", "1.2", "1.2.3;echo nope", "1.2.3-beta", "../1.2.3", "1\u0662.0.0"):
            with self.assertRaises(ValueError):
                release.version_tuple(invalid)
        release.check_tag("v0.1.0", self.root)
        with self.assertRaises(ValueError):
            release.check_tag("v0.2.0", self.root)

    def test_dry_run_keeps_worktree_tags_and_remote_unchanged(self):
        release.release("0.2.0", self.root, dry_run=True)
        self.assertEqual(self.git("rev-parse", "HEAD"), self.before)
        self.assertEqual(self.git("status", "--porcelain"), "")
        self.assertEqual(self.git("tag", "--list"), "")
        self.assertEqual(self.git("rev-parse", "origin/main"), self.before)

    def test_clean_release_pushes_matching_annotated_tag_atomically(self):
        self.original_run = release.run
        with patch("tools.release.run", side_effect=self.fake_cliff) as commands:
            release.release("0.2.0", self.root)
        self.assertEqual((self.root / "VERSION").read_text(), "0.2.0\n")
        self.assertEqual(self.git("cat-file", "-t", "v0.2.0"), "tag")
        self.assertEqual(self.git("rev-parse", "v0.2.0^{}"), self.git("rev-parse", "HEAD"))
        self.assertEqual(self.git("rev-parse", "origin/main"), self.git("rev-parse", "HEAD"))
        self.assertEqual(set(self.git("diff-tree", "--no-commit-id", "--name-only", "-r", "HEAD").splitlines()),
                         {"VERSION", "CHANGELOG.md"})
        commands.assert_any_call(["git", "push", "--atomic", "origin", "HEAD:refs/heads/main",
                                  "refs/tags/v0.2.0"], self.root)

    def test_dirty_worktree_is_not_staged_or_changed(self):
        (self.root / "unrelated.txt").write_text("user work")
        with self.assertRaisesRegex(ValueError, "worktree must be clean"):
            release.release("0.2.0", self.root)
        self.assertEqual(self.git("rev-parse", "HEAD"), self.before)
        self.assertEqual(self.git("diff", "--cached", "--name-only"), "")

    def test_wrong_branch_old_version_and_existing_tag_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "older"):
            release.release("0.0.9", self.root)
        self.git("tag", "v0.1.0")
        with self.assertRaisesRegex(ValueError, "already exists"):
            release.release("0.1.0", self.root)
        self.git("checkout", "--quiet", "-b", "feature")
        with self.assertRaisesRegex(ValueError, "main branch"):
            release.release("0.2.0", self.root)

    def test_unpushed_commit_is_not_released(self):
        (self.root / "VERSION").write_text("0.1.1\n")
        self.git("add", "VERSION")
        self.git("commit", "--quiet", "-m", "Unpushed change")
        with self.assertRaisesRegex(ValueError, "Synchronize"):
            release.release("0.2.0", self.root)

    def test_changelog_failure_leaves_files_unchanged(self):
        self.original_run = release.run
        def fail(arguments, root=release.ROOT):
            if arguments[0] == "git-cliff":
                raise RuntimeError("Changelog failed")
            return self.original_run(arguments, root)
        with patch("tools.release.run", side_effect=fail):
            with self.assertRaisesRegex(RuntimeError, "Changelog failed"):
                release.release("0.2.0", self.root)
        self.assertEqual(self.git("status", "--porcelain"), "")

    def test_push_failure_retains_local_release_without_rewriting_remote(self):
        self.original_run = release.run
        def fail(arguments, root=release.ROOT):
            if arguments[:3] == ["git", "push", "--atomic"]:
                raise RuntimeError("Push failed")
            return self.fake_cliff(arguments, root)
        with patch("tools.release.run", side_effect=fail):
            with self.assertRaisesRegex(RuntimeError, "Push failed"):
                release.release("0.2.0", self.root)
        self.assertNotEqual(self.git("rev-parse", "HEAD"), self.before)
        self.assertEqual(self.git("rev-parse", "origin/main"), self.before)
        self.assertEqual(self.git("cat-file", "-t", "v0.2.0"), "tag")


if __name__ == "__main__":
    unittest.main()

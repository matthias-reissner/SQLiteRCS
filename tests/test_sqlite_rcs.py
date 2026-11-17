import json
import os
import re
import sqlite3
import unittest


class SQLiteRcsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        extension = os.environ.get("SQLITERCS_EXTENSION")
        if not extension:
            raise RuntimeError("Set SQLITERCS_EXTENSION to the built extension path")

        cls.connection = sqlite3.connect(":memory:")
        cls.connection.enable_load_extension(True)
        cls.connection.load_extension(extension)

    @classmethod
    def tearDownClass(cls):
        cls.connection.close()

    def checkin(self, archive, content, log):
        return self.connection.execute(
            "SELECT rcs_checkin(?, ?, ?)", (archive, content, log)
        ).fetchone()[0]

    def checkout(self, archive, revision):
        return self.connection.execute(
            "SELECT rcs_checkout(?, ?)", (archive, revision)
        ).fetchone()[0]

    def diff(self, archive, left, right):
        return self.connection.execute(
            "SELECT rcs_diff(?, ?, ?)", (archive, left, right)
        ).fetchone()[0]

    def create_branch(self, archive, name, revision):
        return self.connection.execute(
            "SELECT rcs_create_branch(?, ?, ?)", (archive, name, revision)
        ).fetchone()[0]

    def branch_checkin(self, archive, branch, content, log):
        return self.connection.execute(
            "SELECT rcs_branch_checkin(?, ?, ?, ?)",
            (archive, branch, content, log),
        ).fetchone()[0]

    def merge(self, archive, base, ours, theirs):
        result = self.connection.execute(
            "SELECT rcs_merge(?, ?, ?, ?)", (archive, base, ours, theirs)
        ).fetchone()[0]
        return json.loads(result)

    def test_symbolic_tags_resolve_and_round_trip(self):
        archive = self.checkin("", "first\n", "initial")
        archive = self.checkin(archive, "second\n", "update")
        archive = self.connection.execute(
            "SELECT rcs_tag(?, ?, ?)", (archive, "zLatest", "1.2")
        ).fetchone()[0]
        archive = self.connection.execute(
            "SELECT rcs_tag(?, ?, ?)", (archive, "release_1", "1.1")
        ).fetchone()[0]

        self.assertEqual(
            self.connection.execute(
                "SELECT rcs_resolve_revision(?, ?)", (archive, "release_1")
            ).fetchone()[0],
            "1.1",
        )
        self.assertEqual(self.checkout(archive, "release_1"), "first\n")
        self.assertEqual(
            [symbol["name"] for symbol in json.loads(
                self.connection.execute(
                    "SELECT rcs_symbols(?)", (archive,)
                ).fetchone()[0]
            )],
            ["release_1", "zLatest"],
        )

        archive = self.connection.execute(
            "SELECT rcs_tag(?, ?, ?)", (archive, "release_1", "1.2")
        ).fetchone()[0]
        self.assertEqual(
            self.connection.execute(
                "SELECT rcs_resolve_revision(?, ?)", (archive, "release_1")
            ).fetchone()[0],
            "1.2",
        )
        self.assertEqual(self.diff(archive, "release_1", "zLatest"), "")
        archive = self.checkin(archive, "third\n", "new head")
        self.assertEqual(
            self.connection.execute("SELECT rcs_head(?)", (archive,)).fetchone()[0],
            "1.3",
        )
        self.assertEqual(self.checkout(archive, "release_1"), "second\n")

        with self.assertRaises(sqlite3.OperationalError):
            self.connection.execute(
                "SELECT rcs_tag(?, ?, ?)", (archive, "not a tag", "1.1")
            )
        with self.assertRaises(sqlite3.OperationalError):
            self.connection.execute(
                "SELECT rcs_tag(?, ?, ?)", (archive, "unknown", "1.4")
            )

    def test_checkin_and_checkout_multiple_revisions(self):
        first = "first @ version\nwith two lines\n"
        second = "short version\n"
        third = ""

        archive = self.checkin("", first, "initial @ import")
        archive = self.checkin(archive, second, "shorten")
        archive = self.checkin(archive, third, "clear")

        self.assertTrue(archive.startswith("head 1.3;\n"))
        self.assertEqual(self.checkout(archive, "1.1"), first)
        self.assertEqual(self.checkout(archive, "1.2"), second)
        self.assertEqual(self.checkout(archive, "1.3"), third)

    def test_archive_can_be_stored_and_updated_in_sqlite(self):
        self.connection.execute(
            "CREATE TABLE documents (name TEXT PRIMARY KEY, archive TEXT NOT NULL)"
        )
        self.connection.execute(
            "INSERT INTO documents VALUES (?, rcs_checkin('', ?, ?))",
            ("notes", "one\n", "initial"),
        )
        self.connection.execute(
            "UPDATE documents SET archive = rcs_checkin(archive, ?, ?) WHERE name = ?",
            ("two\n", "update", "notes"),
        )
        result = self.connection.execute(
            "SELECT rcs_checkout(archive, '1.1'), "
            "rcs_checkout(archive, '1.2') FROM documents WHERE name = 'notes'"
        ).fetchone()
        self.assertEqual(result, ("one\n", "two\n"))

    def test_invalid_inputs_are_sql_errors(self):
        with self.assertRaises(sqlite3.OperationalError):
            self.checkin("", "missing newline", "log")
        with self.assertRaises(sqlite3.OperationalError):
            self.checkin("", "embedded\x00null\n", "log")
        with self.assertRaises(sqlite3.OperationalError):
            self.checkin("", b"\xff\n", "log")
        with self.assertRaises(sqlite3.OperationalError):
            self.checkin("", "valid\n", b"\xff")
        with self.assertRaises(sqlite3.OperationalError):
            self.checkout("", "1.1")

        archive = self.checkin("", "valid\n", "log")
        with self.assertRaises(sqlite3.OperationalError):
            self.checkout(archive, "1.2")
        malformed = archive.replace("author sqlite", "author invalid; state Exp; author", 1)
        with self.assertRaises(sqlite3.OperationalError):
            self.checkout(malformed, "1.1")

    def test_is_valid_checks_supported_archives(self):
        is_valid = lambda archive: self.connection.execute(
            "SELECT rcs_is_valid(?)", (archive,)
        ).fetchone()[0]

        self.assertEqual(is_valid(""), 1)
        archive = self.checkin("", "valid\n", "log")
        self.assertEqual(is_valid(archive), 1)
        self.assertEqual(is_valid("not an RCS archive"), 0)
        self.assertIsNone(is_valid(None))

    def test_import_export_support_general_deltas_and_deltatext_order(self):
        older = "one\nextra\ntwo\nthree\nfour\n"
        newer = "one\ninsert\ntwo\nfour\n"
        archive = self.checkin("", older, "initial")
        archive = self.checkin(archive, newer, "head")

        head_record = (
            "1.2\nlog\n@head@\ntext\n@" + newer + "@\n"
        )
        old_record = (
            "1.1\nlog\n@initial@\ntext\n@d1 4\n"
            "a4 5\none\nextra\ntwo\nthree\nfour\n@\n"
        )
        self.assertIn(head_record + "\n" + old_record, archive)
        old_record = (
            "1.1\nlog\n@initial@\ntext\n@d2 1\n"
            "a1 1\nextra\na3 1\nthree\n@\n"
        )
        external_archive = archive.replace(
            head_record + "\n" +
            "1.1\nlog\n@initial@\ntext\n@d1 4\n"
            "a4 5\none\nextra\ntwo\nthree\nfour\n@\n",
            old_record + "\n" + head_record,
            1,
        )

        imported = self.connection.execute(
            "SELECT rcs_import(?)", (external_archive,)
        ).fetchone()[0]
        self.assertEqual(self.checkout(imported, "1.1"), older)
        self.assertEqual(self.checkout(imported, "1.2"), newer)
        self.assertEqual(
            self.connection.execute(
                "SELECT rcs_export(?)", (imported,)
            ).fetchone()[0],
            imported,
        )
        self.assertEqual(
            self.connection.execute(
                "SELECT rcs_is_valid(?)", (external_archive,)
            ).fetchone()[0],
            1,
        )
        self.assertEqual(
            self.connection.execute("SELECT rcs_import('')").fetchone()[0], ""
        )
        self.assertEqual(
            self.connection.execute("SELECT rcs_export('')").fetchone()[0], ""
        )
        self.assertIsNone(
            self.connection.execute("SELECT rcs_import(NULL)").fetchone()[0]
        )

    def test_import_real_gnu_rcs_archive(self):
        fixture_path = os.path.join(
            os.path.dirname(__file__), "fixtures", "gnu-rcs-5.10.1-trunk,v"
        )
        with open(fixture_path, encoding="utf-8") as fixture:
            archive = fixture.read()

        imported = self.connection.execute(
            "SELECT rcs_import(?)", (archive,)
        ).fetchone()[0]
        self.assertEqual(self.checkout(imported, "1.1"), "alpha\nbeta\ngamma\n")
        self.assertEqual(
            self.checkout(imported, "1.2"),
            "alpha\ninserted\nbeta\ngamma\n",
        )
        self.assertEqual(self.checkout(imported, "release"), "alpha\nbeta\ngamma\n")
        self.assertEqual(self.checkout(imported, "feature"), "branch second\n")
        self.assertEqual(
            self.checkout(imported, "1.1.1.1"), "branch line\n"
        )
        self.assertEqual(
            self.connection.execute(
                "SELECT rcs_resolve_revision(?, 'feature')", (imported,)
            ).fetchone()[0],
            "1.1.1",
        )
        self.assertEqual(
            json.loads(
                self.connection.execute(
                    "SELECT rcs_symbols(?)", (imported,)
                ).fetchone()[0]
            ),
            [
                {"name": "feature", "revision": "1.1.1", "is_branch": True},
                {"name": "release", "revision": "1.1", "is_branch": False},
            ],
        )
        self.assertEqual(
            self.connection.execute(
                "SELECT rcs_is_valid(?)", (archive,)
            ).fetchone()[0],
            1,
        )
        exported = self.connection.execute(
            "SELECT rcs_export(?)", (imported,)
        ).fetchone()[0]
        self.assertEqual(
            self.connection.execute(
                "SELECT rcs_import(?)", (exported,)
            ).fetchone()[0],
            exported,
        )

    def test_import_real_gnu_nested_branch_archive(self):
        fixture_path = os.path.join(
            os.path.dirname(__file__),
            "fixtures",
            "gnu-rcs-5.10.1-nested-branches,v",
        )
        with open(fixture_path, encoding="utf-8") as fixture:
            archive = fixture.read()

        imported = self.connection.execute(
            "SELECT rcs_import(?)", (archive,)
        ).fetchone()[0]
        self.assertEqual(self.checkout(imported, "feature"), "branch latest\n")
        self.assertEqual(
            self.checkout(imported, "1.1.1.1.1.1"), "nested first\n"
        )
        self.assertEqual(
            self.checkout(imported, "nested"), "nested latest\n"
        )
        self.assertEqual(
            self.connection.execute(
                "SELECT rcs_resolve_revision(?, 'nested')", (imported,)
            ).fetchone()[0],
            "1.1.1.1.1",
        )
        self.assertEqual(
            self.connection.execute(
                "SELECT rcs_is_valid(?)", (archive,)
            ).fetchone()[0],
            1,
        )
        exported = self.connection.execute(
            "SELECT rcs_export(?)", (imported,)
        ).fetchone()[0]
        self.assertEqual(
            self.connection.execute(
                "SELECT rcs_import(?)", (exported,)
            ).fetchone()[0],
            exported,
        )

    def test_import_real_gnu_archive_with_admin_metadata_variants(self):
        fixture_path = os.path.join(
            os.path.dirname(__file__), "fixtures", "gnu-rcs-5.10.1-metadata,v"
        )
        with open(fixture_path, encoding="utf-8") as fixture:
            archive = fixture.read()

        self.assertIn("access\n\tReviewer;", archive)
        self.assertIn("locks\n\tMatthias:1.2;", archive)
        self.assertIn("comment\t@//@;", archive)
        self.assertNotIn("strict;", archive)
        imported = self.connection.execute(
            "SELECT rcs_import(?)", (archive,)
        ).fetchone()[0]
        self.assertIn("access;\nsymbols;\nlocks; strict;\n", imported)
        self.assertIn("comment @# @;\n", imported)
        self.assertEqual(self.checkout(imported, "1.1"), "old line\n")
        self.assertEqual(self.checkout(imported, "1.2"), "new line\n")
        self.assertEqual(
            self.connection.execute(
                "SELECT rcs_is_valid(?)", (archive,)
            ).fetchone()[0],
            1,
        )
        exported = self.connection.execute(
            "SELECT rcs_export(?)", (imported,)
        ).fetchone()[0]
        self.assertEqual(
            self.connection.execute(
                "SELECT rcs_import(?)", (exported,)
            ).fetchone()[0],
            exported,
        )

    def test_import_preserves_branch_tags_and_rejects_invalid_deltas(self):
        archive = self.checkin("", "base\n", "initial")
        archive = self.checkin(archive, "trunk\n", "trunk update")
        archive = self.connection.execute(
            "SELECT rcs_tag(?, 'release', '1.1')", (archive,)
        ).fetchone()[0]
        archive = self.create_branch(archive, "feature", "release")
        archive = self.branch_checkin(
            archive, "feature", "branch\n", "branch initial"
        )
        marker = "desc\n@SQLiteRCS archive@\n\n"
        prefix, deltatext = archive.split(marker, 1)
        records = re.findall(
            r"(?m)^\d+(?:\.\d+)+\nlog\n@.*?@\ntext\n@.*?@\n",
            deltatext,
            re.S,
        )
        self.assertEqual(len(records), 3)
        archive = prefix + marker + "\n".join(
            record.rstrip("\n") for record in reversed(records)
        ) + "\n"

        imported = self.connection.execute(
            "SELECT rcs_import(?)", (archive,)
        ).fetchone()[0]
        self.assertEqual(self.checkout(imported, "release"), "base\n")
        self.assertEqual(self.checkout(imported, "feature"), "branch\n")
        self.assertEqual(
            json.loads(
                self.connection.execute(
                    "SELECT rcs_symbols(?)", (imported,)
                ).fetchone()[0]
            ),
            json.loads(
                self.connection.execute(
                    "SELECT rcs_symbols(?)", (archive,)
                ).fetchone()[0]
            ),
        )

        malformed = imported.replace("d1 1\n", "d99 1\n", 1)
        self.assertEqual(
            self.connection.execute(
                "SELECT rcs_is_valid(?)", (malformed,)
            ).fetchone()[0],
            0,
        )
        with self.assertRaises(sqlite3.OperationalError):
            self.connection.execute("SELECT rcs_import(?)", (malformed,))

    def test_branch_checkins_checkout_and_trunk_independence(self):
        archive = self.checkin("", "base\n", "initial")
        archive = self.checkin(archive, "trunk update\n", "trunk")
        archive = self.connection.execute(
            "SELECT rcs_tag(?, 'release', '1.1')", (archive,)
        ).fetchone()[0]
        archive = self.create_branch(archive, "feature", "release")

        symbols = json.loads(
            self.connection.execute(
                "SELECT rcs_symbols(?)", (archive,)
            ).fetchone()[0]
        )
        self.assertEqual(
            symbols,
            [
                {"name": "feature", "revision": "1.1.1", "is_branch": True},
                {"name": "release", "revision": "1.1", "is_branch": False},
            ],
        )
        with self.assertRaises(sqlite3.OperationalError):
            self.checkout(archive, "feature")

        archive = self.branch_checkin(
            archive, "feature", "branch first\n", "branch initial"
        )
        archive = self.branch_checkin(
            archive, "feature", "branch latest\n", "branch update"
        )
        archive = self.branch_checkin(
            archive, "feature", "branch final\n", "branch final"
        )
        self.assertEqual(self.checkout(archive, "feature"), "branch final\n")
        self.assertEqual(self.checkout(archive, "1.1.1.1"), "branch first\n")
        self.assertEqual(self.checkout(archive, "1.1.1.2"), "branch latest\n")
        self.assertEqual(self.checkout(archive, "1.1.1.3"), "branch final\n")
        self.assertEqual(self.checkout(archive, "release"), "base\n")
        self.assertEqual(self.checkout(archive, "1.2"), "trunk update\n")
        self.assertEqual(
            self.connection.execute(
                "SELECT rcs_resolve_revision(?, '1.1.1.2')", (archive,)
            ).fetchone()[0],
            "1.1.1.2",
        )
        self.assertEqual(
            self.connection.execute(
                "SELECT rcs_resolve_revision(?, 'feature')", (archive,)
            ).fetchone()[0],
            "1.1.1",
        )
        self.assertTrue(
            self.diff(archive, "1.1.1.1", "feature").startswith(
                "--- 1.1.1.1\n+++ 1.1.1.3\n"
            )
        )
        revisions = json.loads(
            self.connection.execute(
                "SELECT rcs_revisions(?)", (archive,)
            ).fetchone()[0]
        )
        self.assertEqual(
            [revision["revision"] for revision in revisions],
            ["1.2", "1.1", "1.1.1.3", "1.1.1.2", "1.1.1.1"],
        )

        archive = self.checkin(archive, "trunk latest\n", "trunk next")
        self.assertEqual(
            self.connection.execute(
                "SELECT rcs_head(?)", (archive,)
            ).fetchone()[0],
            "1.3",
        )
        self.assertEqual(self.checkout(archive, "feature"), "branch final\n")
        self.assertEqual(self.checkout(archive, "release"), "base\n")
        self.assertEqual(
            self.connection.execute(
                "SELECT rcs_is_valid(?)", (archive,)
            ).fetchone()[0],
            1,
        )
        broken_parent = archive.replace("branches 1.1.1.1;", "branches;", 1)
        self.assertEqual(
            self.connection.execute(
                "SELECT rcs_is_valid(?)", (broken_parent,)
            ).fetchone()[0],
            0,
        )

    def test_branch_names_are_unique_and_require_creation(self):
        archive = self.checkin("", "base\n", "initial")
        archive = self.create_branch(archive, "first", "1.1")
        archive = self.create_branch(archive, "second", "1.1")
        symbols = json.loads(
            self.connection.execute(
                "SELECT rcs_symbols(?)", (archive,)
            ).fetchone()[0]
        )
        self.assertEqual(
            [symbol["revision"] for symbol in symbols],
            ["1.1.1", "1.1.2"],
        )
        archive = self.branch_checkin(
            archive, "second", "second branch\n", "second branch initial"
        )
        self.assertEqual(
            self.checkout(archive, "1.1.2.1"), "second branch\n"
        )
        self.assertEqual(
            self.connection.execute(
                "SELECT rcs_is_valid(?)", (archive,)
            ).fetchone()[0],
            1,
        )
        with self.assertRaises(sqlite3.OperationalError):
            self.create_branch(archive, "first", "1.1")
        with self.assertRaises(sqlite3.OperationalError):
            self.branch_checkin(archive, "missing", "content\n", "log")
        with self.assertRaises(sqlite3.OperationalError):
            self.connection.execute(
                "SELECT rcs_tag(?, 'first', '1.1')", (archive,)
            )

    def test_nested_branches_round_trip_and_keep_parent_revisions(self):
        archive = self.checkin("", "base\n", "initial")
        archive = self.create_branch(archive, "feature", "1.1")
        archive = self.branch_checkin(
            archive, "feature", "branch first\n", "branch initial"
        )
        archive = self.branch_checkin(
            archive, "feature", "branch latest\n", "branch update"
        )
        archive = self.create_branch(archive, "nested", "1.1.1.1")
        archive = self.branch_checkin(
            archive, "nested", "nested first\n", "nested initial"
        )
        archive = self.branch_checkin(
            archive, "nested", "nested latest\n", "nested update"
        )

        self.assertEqual(
            self.connection.execute(
                "SELECT rcs_is_valid(?)", (archive,)
            ).fetchone()[0],
            1,
        )
        self.assertEqual(
            self.checkout(archive, "1.1.1.1.1.1"), "nested first\n"
        )
        self.assertEqual(
            self.checkout(archive, "nested"), "nested latest\n"
        )
        self.assertEqual(
            self.checkout(archive, "feature"), "branch latest\n"
        )
        self.assertEqual(
            self.connection.execute(
                "SELECT rcs_resolve_revision(?, 'nested')", (archive,)
            ).fetchone()[0],
            "1.1.1.1.1",
        )
        self.assertTrue(
            self.diff(archive, "1.1.1.1", "nested").startswith(
                "--- 1.1.1.1\n+++ 1.1.1.1.1.2\n"
            )
        )

        exported = self.connection.execute(
            "SELECT rcs_export(?)", (archive,)
        ).fetchone()[0]
        imported = self.connection.execute(
            "SELECT rcs_import(?)", (exported,)
        ).fetchone()[0]
        self.assertEqual(
            self.connection.execute(
                "SELECT rcs_is_valid(?)", (imported,)
            ).fetchone()[0],
            1,
        )
        self.assertEqual(
            self.checkout(imported, "nested"), "nested latest\n"
        )
        broken_parent = archive.replace(
            "branches 1.1.1.1.1.1;", "branches;", 1
        )
        self.assertEqual(
            self.connection.execute(
                "SELECT rcs_is_valid(?)", (broken_parent,)
            ).fetchone()[0],
            0,
        )

    def test_merge_combines_disjoint_edits(self):
        base = "alpha\nbeta\ngamma\n"
        ours = "ALPHA\nbeta\ngamma\n"
        theirs = "alpha\nbeta\nGAMMA\n"
        archive = self.checkin("", base, "base")
        archive = self.checkin(archive, ours, "ours")
        archive = self.create_branch(archive, "theirs", "1.1")
        archive = self.branch_checkin(archive, "theirs", theirs, "theirs")

        result = self.merge(archive, "1.1", "1.2", "theirs")
        self.assertEqual(result, {
            "content": "ALPHA\nbeta\nGAMMA\n",
            "has_conflicts": False,
        })

    def test_merge_keeps_insertions_at_change_boundaries_separate(self):
        base = "before\nremove\nend\n"
        ours = "before\nend\n"
        theirs = "before\ninserted\nremove\nend\n"
        archive = self.checkin("", base, "base")
        archive = self.checkin(archive, ours, "delete")
        archive = self.create_branch(archive, "insert", "1.1")
        archive = self.branch_checkin(archive, "insert", theirs, "insert")

        result = self.merge(archive, "1.1", "1.2", "insert")
        self.assertEqual(result, {
            "content": "before\ninserted\nend\n",
            "has_conflicts": False,
        })

    def test_merge_marks_conflicts_and_deduplicates_identical_edits(self):
        base = "alpha\nbeta\ngamma\n"
        conflicting_ours = "alpha\nours\ngamma\n"
        conflicting_theirs = "alpha\ntheirs\ngamma\n"
        archive = self.checkin("", base, "base")
        archive = self.checkin(archive, conflicting_ours, "ours")
        archive = self.create_branch(archive, "conflict", "1.1")
        archive = self.branch_checkin(
            archive, "conflict", conflicting_theirs, "theirs"
        )

        result = self.merge(archive, "1.1", "1.2", "conflict")
        self.assertTrue(result["has_conflicts"])
        self.assertEqual(
            result["content"],
            "alpha\n<<<<<<< ours\nours\n=======\ntheirs\n"
            ">>>>>>> theirs\ngamma\n",
        )

        identical = self.checkin("", base, "base")
        identical = self.checkin(identical, conflicting_ours, "ours")
        identical = self.create_branch(identical, "same", "1.1")
        identical = self.branch_checkin(
            identical, "same", conflicting_ours, "same change"
        )
        result = self.merge(identical, "1.1", "1.2", "same")
        self.assertEqual(result["content"], conflicting_ours)
        self.assertFalse(result["has_conflicts"])

        empty_base = self.checkin("", "", "empty base")
        empty_base = self.checkin(empty_base, "ours\n", "ours")
        empty_base = self.create_branch(empty_base, "theirs", "1.1")
        empty_base = self.branch_checkin(
            empty_base, "theirs", "theirs\n", "theirs"
        )
        result = self.merge(empty_base, "1.1", "1.2", "theirs")
        self.assertTrue(result["has_conflicts"])
        self.assertEqual(
            result["content"],
            "<<<<<<< ours\nours\n=======\ntheirs\n>>>>>>> theirs\n",
        )

    def test_merge_comparison_limit(self):
        below_limit = "".join("line\n" for _ in range(1999))
        ours = "ours\n" + below_limit[len("line\n"):]
        theirs = below_limit[:-len("line\n")] + "theirs\n"
        archive = self.checkin("", below_limit, "base")
        archive = self.checkin(archive, ours, "ours")
        archive = self.create_branch(archive, "theirs", "1.1")
        archive = self.branch_checkin(archive, "theirs", theirs, "theirs")
        result = self.merge(archive, "1.1", "1.2", "theirs")
        self.assertFalse(result["has_conflicts"])
        self.assertTrue(result["content"].startswith("ours\n"))
        self.assertTrue(result["content"].endswith("theirs\n"))

        at_limit = "".join("line\n" for _ in range(2000))
        ours = "ours\n" + at_limit[len("line\n"):]
        theirs = at_limit[:-len("line\n")] + "theirs\n"
        archive = self.checkin("", at_limit, "base")
        archive = self.checkin(archive, ours, "ours")
        archive = self.create_branch(archive, "theirs", "1.1")
        archive = self.branch_checkin(archive, "theirs", theirs, "theirs")
        with self.assertRaises(sqlite3.OperationalError):
            self.merge(archive, "1.1", "1.2", "theirs")

    def test_merge_result_size_limit(self):
        ours = "o" * (4 * 1024 * 1024) + "\n"
        theirs = "t" * (4 * 1024 * 1024) + "\n"
        archive = self.checkin("", "", "base")
        archive = self.checkin(archive, ours, "ours")
        archive = self.create_branch(archive, "theirs", "1.1")
        archive = self.branch_checkin(archive, "theirs", theirs, "theirs")
        with self.assertRaises(sqlite3.OperationalError):
            self.merge(archive, "1.1", "1.2", "theirs")

    def test_empty_revision_round_trip(self):
        archive = self.checkin("", "", "empty file")
        self.assertEqual(self.checkout(archive, "1.1"), "")

    def test_head_and_revision_metadata(self):
        first_log = 'quote " and @; snowman \u2603\nsecond line'
        archive = self.checkin("", "one\n", first_log)
        archive = self.checkin(archive, "two\n", "second")

        head = self.connection.execute(
            "SELECT rcs_head(?)", (archive,)
        ).fetchone()[0]
        revisions_json = self.connection.execute(
            "SELECT rcs_revisions(?)", (archive,)
        ).fetchone()[0]

        self.assertEqual(head, "1.2")
        revisions = json.loads(revisions_json)
        self.assertEqual(
            [revision["revision"] for revision in revisions], ["1.2", "1.1"]
        )
        self.assertEqual(revisions[1]["author"], "sqlite")
        self.assertEqual(revisions[1]["state"], "Exp")
        self.assertEqual(revisions[1]["log"], first_log)

    def test_unified_diff_between_revisions(self):
        archive = self.checkin("", "alpha\nbeta\nsame\n", "initial")
        archive = self.checkin(archive, "alpha\ngamma\nsame\n", "change")

        self.assertEqual(
            self.diff(archive, "1.1", "1.2"),
            "--- 1.1\n+++ 1.2\n@@ -1,3 +1,3 @@\n"
            " alpha\n-beta\n+gamma\n same\n",
        )
        self.assertEqual(self.diff(archive, "1.2", "1.2"), "")

        empty_archive = self.checkin("", "", "empty")
        empty_archive = self.checkin(empty_archive, "new\n", "add")
        self.assertEqual(
            self.diff(empty_archive, "1.1", "1.2"),
            "--- 1.1\n+++ 1.2\n@@ -0,0 +1,1 @@\n+new\n",
        )

    def test_unified_diff_splits_distant_changes_into_hunks(self):
        old_lines = ["line {}".format(i) for i in range(20)]
        new_lines = old_lines[:]
        new_lines[0] = "changed first"
        new_lines[-1] = "changed last"
        archive = self.checkin("", "\n".join(old_lines) + "\n", "initial")
        archive = self.checkin(
            archive, "\n".join(new_lines) + "\n", "two distant changes"
        )

        self.assertEqual(
            self.diff(archive, "1.1", "1.2"),
            "--- 1.1\n+++ 1.2\n"
            "@@ -1,4 +1,4 @@\n"
            "-line 0\n+changed first\n"
            " line 1\n line 2\n line 3\n"
            "@@ -17,4 +17,4 @@\n"
            " line 16\n line 17\n line 18\n"
            "-line 19\n+changed last\n",
        )

    def test_diff_comparison_limit(self):
        below_limit = "".join("line\n" for _ in range(1999))
        below_limit_changed = "changed\n" + below_limit[len("line\n"):]
        archive = self.checkin("", below_limit, "initial")
        archive = self.checkin(archive, below_limit_changed, "change")
        self.assertTrue(self.diff(archive, "1.1", "1.2").startswith("--- 1.1\n"))

        at_limit = "".join("line\n" for _ in range(2000))
        at_limit_changed = "changed\n" + at_limit[len("line\n"):]
        archive = self.checkin("", at_limit, "initial")
        archive = self.checkin(archive, at_limit_changed, "change")
        with self.assertRaises(sqlite3.OperationalError):
            self.diff(archive, "1.1", "1.2")


if __name__ == "__main__":
    unittest.main()

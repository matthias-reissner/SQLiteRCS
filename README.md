# SQLiteRCS

SQLiteRCS is a revision control system for SQLite, similar to
[PostRCS](https://github.com/matthias-reissner/postrcs).

## Design

SQLite has no extension-defined scalar data types, so an archive is represented
as `TEXT`. SQLiteRCS provides SQL functions through a loadable SQLite
extension. The functions return new archive values rather than modifying a
database implicitly; applications can store the returned archive in any table
or column.

SQLiteRCS writes textual RCS archives. `rcs_import` validates and normalizes
supported archives, and `rcs_export` serializes them in SQLiteRCS's canonical
layout. The parser accepts GNU RCS metadata with tab-separated or wrapped
symbols, line-oriented `a`/`d` delta scripts, and deltatext records in any
order. It also accepts access lists, lock entries, optional strict locking,
and custom comment leaders. Import normalizes these administrative fields to
SQLiteRCS defaults; it does not preserve or enforce access lists or locks. It
does not yet support every variation in third-party RCS archive metadata or
binary content.

## SQL API

```sql
SELECT rcs_checkin(archive, content, log);
SELECT rcs_create_branch(archive, name, revision);
SELECT rcs_branch_checkin(archive, branch, content, log);
SELECT rcs_checkout(archive, revision);
SELECT rcs_head(archive);
SELECT rcs_revisions(archive);
SELECT rcs_diff(archive, revision_a, revision_b);
SELECT rcs_merge(archive, base, ours, theirs);
SELECT rcs_tag(archive, name, revision);
SELECT rcs_symbols(archive);
SELECT rcs_resolve_revision(archive, name);
SELECT rcs_is_valid(archive);
SELECT rcs_import(text);
SELECT rcs_export(archive);
```

- `rcs_checkin` returns an archive containing a new trunk revision. It accepts
  an empty archive to create the first revision.
- `rcs_create_branch` creates a named branch from a trunk revision, branch
  revision, or tag.
- `rcs_branch_checkin` adds a revision to a named branch, including nested
  branches.
- `rcs_checkout` returns the content of a trunk or branch revision, branch tip,
  or symbolic tag (for example, `1.1`, `1.1.1.2`, `1.1.1.1.1.2`, or
  `feature`).
- `rcs_head` returns the current trunk head, such as `1.3`.
- `rcs_revisions` returns a JSON array containing trunk revisions newest-first,
  followed by each branch's revisions newest-first. Each object has `revision`,
  `author`, `date`, `state`, and `log` fields.
- `rcs_diff` returns a unified diff between trunk or branch revisions, branch
  tips, or symbolic tags, or an empty string when their contents are
  identical. Distant changes are split into hunks with three context lines.
- `rcs_merge` performs a three-way merge of trunk or branch revisions and
  returns JSON with `content` and `has_conflicts`. Conflicting regions are
  marked with `<<<<<<< ours`, `=======`, and `>>>>>>> theirs`; the merge
  result is limited to 8 MiB.
- `rcs_tag` creates or updates a symbolic name for a trunk revision.
- `rcs_symbols` returns a JSON array of symbolic names, their revision targets,
  and an `is_branch` boolean.
- `rcs_resolve_revision` returns the revision or branch number identified by
  a number or symbolic name.
- `rcs_is_valid` returns `1` for an empty archive or a valid archive in the
  SQLiteRCS-supported format, including its revision delta chains, `0` for
  invalid or unsupported text, and `NULL` when its argument is `NULL`.
- `rcs_import` validates an RCS archive and returns it in SQLiteRCS's canonical
  layout. `rcs_export` validates and serializes an archive in that same layout.
  Both return `NULL` for `NULL` input and an empty string for an empty archive.
- Symbol names are ASCII identifiers of at most 128 characters.
- Other functions report invalid archives, revisions, or arguments as SQL
  errors.
- Contents and logs are UTF-8 text, not binary data. Non-empty content must
  end with a newline.
- Archives are limited to 16 MiB, revision contents to 8 MiB, and each
  delta script to 100,000 commands.
- Diff and merge comparisons are limited to 4,000,000
  dynamic-programming cells per compared revision.

The current implementation supports trunk and nested branch check-in, checkout,
metadata, diff, three-way merge, symbolic tags, and branch names. Import support
is limited to the RCS metadata structure understood by SQLiteRCS; arbitrary
third-party archive variants remain unsupported.

## Build and test

The extension requires SQLite development headers (`sqlite3ext.h` and
`sqlite3.h`) and a C compiler.

On Windows, run `build.bat` from an x64 Visual Studio developer prompt after
setting `SQLITE_INCLUDE_DIR` to the directory containing the SQLite headers.
On Unix-like systems, run `make`.

After building, run the tests with Python 3:

```text
set SQLITERCS_EXTENSION=sqlitercs.dll
python -m unittest discover -s tests -v
```

On Unix-like systems, use `SQLITERCS_EXTENSION=./sqlitercs.so` before the
Python command. The suite includes an archive created by GNU RCS 5.10.1 with
trunk, tag, branch, and nested-branch revisions.

## Example

```sql
CREATE TABLE documents (
    name TEXT PRIMARY KEY,
    archive TEXT NOT NULL
);

INSERT INTO documents(name, archive)
VALUES ('notes.txt', rcs_checkin('', 'First version' || char(10), 'Initial import'));

UPDATE documents
SET archive = rcs_checkin(archive, 'Updated version' || char(10), 'Edit notes')
WHERE name = 'notes.txt';

SELECT rcs_checkout(archive, '1.1')
FROM documents
WHERE name = 'notes.txt';
```

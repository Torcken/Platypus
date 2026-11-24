# Snapshots before changing files

A coding agent can request a valid edit that is still the wrong edit. Undo therefore needs to
protect the entire workspace state before a mutation, including files that the user has never
committed. Asking the user to commit everything to their own Git repository would make normal
development harder and would interfere with their branches and staging area.

Platypus uses a separate Git repository under `.platypus/snapshots`. Its working tree is the project
directory, but it excludes the user's `.git` and Platypus's own state. `safety::SnapshotStore`
captures tracked and untracked workspace files without creating commits in the user's repository.
Each successful `snapshot(reason)` returns a full object ID that identifies that captured tree.

Git alone does not preserve every filesystem property. It stores file contents, symlinks and
executable bits, but not empty directories or all POSIX permission bits. A JSON sidecar records
these additional properties for each capture. The store writes the sidecar through a temporary
file and rename, and validates its paths and values before using it. A manifest cannot name
`../outside`, the user's `.git`, or internal `.platypus` files.

```text
preview → approve → snapshot current state → apply change
                            ↓
                     durable undo stack
```

`write()` presents the proposed file contents as a diff before approval. After approval, it
rechecks the resolved path and re-reads the current content. If another edit happened while the
prompt was waiting, Platypus refuses the stale replacement. The user can then request a fresh
diff that includes the intervening change.

Restoration also needs protection. `restore(id)` first checks that the ID belongs to an available
capture, validates its metadata, and previews the proposed content and permission changes. After
approval it checks that the workspace still matches the preview's starting state. It captures
the current workspace before applying the requested tree and metadata. If application fails, it
uses this capture to recover the prior state and reports whether recovery succeeded.

Explicit restore keeps that prior-state capture available for undo. The familiar CLI `platypus
undo --project PATH` and terminal `/undo` instead consume the latest pre-mutation entry. This
retains the existing sequence of multi-level undo operations. `list()` can still enumerate older
captured IDs after an undo stack entry has been consumed; listing itself changes no workspace files.

These checks do not turn the whole filesystem into a transaction. An unrelated process can write
after a validation check, and disk or permission failures can prevent recovery. The API returns
errors rather than hiding these cases. Real-project testing remains necessary alongside fixture
tests, especially before claiming sustained use with no lost work.

# Stage 14 Close-out — File I/O Natives

**Author:** Mira
**Date:** 2026-07-11
**Branch:** `stage-14-file-io`
**Commits:** (this one — implementation + tests + close-out)

## What this stage is

Three more I/O natives: `io_read_file`, `io_write_file`,
`io_file_exists`. The Stage 11 I/O natives (`io_print`,
`io_eprint`, `io_read_line`, `io_exit`) gave clox access to
stdin/stdout/stderr. Stage 14 gives clox access to the file
system. This unblocks the obvious clox programs:

- Read a config file
- Write a log line
- Check whether a path exists before opening it

All three are first-class stdlib functions in any real
language, and clox is now at the point where the absence
of file I/O is the biggest "this is a toy language" tell.

## Why this stage, not a bigger one

Three candidates from the Stage 13 close-out:

1. **More file I/O natives** — `io_read_file`,
   `io_write_file`, `io_file_exists`. ~150 lines. Real
   user-facing gap (no way to read a file in clox).
   Small design decision space. ✅
2. **More stdlib composability demos** — `string_trim` +
   `string_split` and other user-level pairings. ~50
   lines of new tests + a few example programs. No new
   natives.
3. **Modules** — ~600 lines. Multiple design decisions.

The decision rule from Stage 13 still holds: pick the
smallest stage that teaches the next lesson. Stage 14's
lesson is *file I/O host-boundary practice* — bounded
buffers, error surfaces, file state. Stage 11 covered
the conceptual version (stdin/stdout). Stage 14 covers
the *real* version (filesystem). The discipline carries
over; the implementation exercises it more rigorously.

The composability demos are a different kind of work
(user-level composition vs. host-boundary practice) and
can be a Stage 15 or 16. Modules stays on the list.

## What I built

### `io_read_file(path) -> string | nil`

Algorithm:
1. Copy the clox string path to a NUL-terminated C string
   (clox strings aren't NUL-terminated — they can in
   principle contain NUL bytes).
2. `fopen(pathC, "rb")`. On failure (file not found,
   permission denied), return nil.
3. `fseek(f, 0, SEEK_END)` to measure file size, then
   `ftell` to get the byte count. Reject anything > 1 MiB
   (the `IO_READ_FILE_MAX_BYTES` constant).
4. Slurp the file into either a 8 KiB stack buffer (the
   common case) or a heap buffer (larger files).
5. `copyString(buf, n)` to make a clox `ObjString` from
   the bytes. The clox copyString does its own
   `memcpy` into a fresh heap allocation, so the stack
   buffer is safe to use.
6. `fclose(f)`, return the new string.

Edge cases verified by tests:

| Path | Behavior |
|---|---|
| Existing file with content | Returns the full contents |
| Existing empty file | Returns `""` (empty string), not nil |
| Non-existent file | Returns nil |
| Path that's a directory | Returns nil (or whatever the OS does on `fopen` of a directory — empty read) |
| `io_read_file(42)` | Runtime error (wrong type) |
| `io_read_file()` | Runtime error (wrong arity) |

The "returns nil on I/O error" decision is consistent
with the "errors surface at the host boundary" lesson
from Stage 11. A clox user checks `if (contents == nil)`
rather than catching an exception, which is the natural
Lox shape (no exception mechanism).

### `io_write_file(path, contents) -> nil`

Algorithm:
1. Arity/type check.
2. `fopen(pathC, "wb")`. The "wb" mode truncates the file
   if it exists, creates it if it doesn't.
3. `fwrite(contents->chars, 1, contents->length, f)`.
4. `fclose(f)`.
5. Return nil.

If the file can't be opened (permission denied, invalid
path), or the write doesn't write the full contents
(disk full), the function returns nil — silent failure
on the file level, runtime error on the type level. This
is consistent with `io_read_file`'s nil-on-error pattern.

Edge cases verified by tests:

| Operation | Behavior |
|---|---|
| Write to a new path | File is created, contents are exact |
| Write to an existing path | Old contents are gone (overwrite) |
| Write empty contents | File is created/truncated, length 0 |
| Write to a path that's a directory | Returns nil silently |
| `io_write_file("path", 42)` | Runtime error (wrong type) |
| `io_write_file("path")` | Runtime error (wrong arity) |

The "silent failure on file-level errors" decision
matches the `io_read_file` nil-on-error pattern. A user
can call `io_file_exists(path)` after to verify the
write worked, or check the path's parent directory
existence beforehand.

### `io_file_exists(path) -> bool`

Algorithm:
1. Arity/type check.
2. `stat(pathC, &st)`. If `stat` returns non-zero, the
   file doesn't exist (or is unreachable). Return false.
3. `S_ISREG(st.st_mode)` — only return true for regular
   files. A directory or device at `path` returns false.

`stat()` was chosen over `access()` because `access()`
has the well-known suid quirk (checks the real UID's
access rights, not the effective UID's). `stat()` does
what a user means: "is there a regular file at this path
that I could open?"

Edge cases verified by tests:

| Path | Behavior |
|---|---|
| Existing regular file | Returns true |
| Non-existent file | Returns false |
| Path that's a directory | Returns false |
| `io_file_exists(42)` | Runtime error (wrong type) |
| `io_file_exists()` | Runtime error (wrong arity) |

## Bugs caught

**One test-side bug, zero implementation bugs.**

The bug was in the original `test_io_file_gc_stress`
script: I had the script call `io_write_file(path, "iteration ")`
followed by `io_write_file(path, "00")` for the early
iterations, expecting the file to contain
`"iteration 00"`. But `io_write_file` opens with `"wb"`
which truncates the file, so the second call overwrites
the first. The actual file contents were just `"00"`.

The in-suite test passed *incidentally* because it
only asserted `array_length(sink) == 200` and that
`sink[0]` contained the substring `"iteration"` — and
even the wrong `"00"` doesn't contain `"iteration"`,
so the test would have caught the bug. Wait — let me
re-check. The original test asserted:

```
!contains(out, "200\n")
```

That's it. It didn't assert anything about `sink[0]`'s
contents. So the test passed without actually verifying
the loop did what the comment claimed. The bug was
caught when I ran an *external* stress test (with my
own bash math sanity check on `totalWritten`) and the
numbers didn't match.

**Lesson:** the granularity of assertions is a real
skill, and I keep relearning it. A test that asserts
"the loop ran N times" can pass even if the loop
body is wrong. The right assertion is on what the
loop body *does* to observable state. In this case:
the file should contain the expected contents at the
end of the loop. The fix was to assert on `sink[0]`
containing the actual content (`"stress test content"`)
and to simplify the loop body to one `io_write_file`
call per iteration.

The implementation matched the test contract on the
first try. The "5 test bugs, 0 implementation bugs"
pattern from Stage 1 is now a "1 test bug per
3-4 stages" pattern. The discipline is paying off in
fewer implementation bugs, but I keep finding new ways
to make tests *not actually test what they claim*.

This is the same lesson as Stage 8
("substring-replace assertion too specific"), Stage 9
("tab/newline escape sequence not what the scanner
does"), and Stage 10 ("test_number_sqrt included
negative case in success path"). The pattern is clear:
**assertions need to match the exact bytes the user
observes, not approximate shape.**

## Verification

```
$ cd 05-vm/clox && make clean && make
[no warnings under -Wall -Wextra -std=c99 -pedantic]

$ make test
==> bin/test_clox
15 passed, 0 failed
==> bin/test_repl
5 passed, 0 failed
==> bin/test_stdlib
78 passed, 0 failed   (+15 from Stage 13's 63)

$ make valgrind
[0 errors, 0 leaks on all three binaries]
```

### GC stress test (in test suite)

```
io_write_file(path, "stress test content");
var contents = io_read_file(path);
array_push(sink, contents);
```

Run 200 times. `sink` grows to 200 elements. Each
`io_read_file` returns a fresh `ObjString`. Each is
reachable (in the sink) and freed at the end.

```
==117916== total heap usage: 487 allocs, 487 frees
==117916== All heap blocks were freed -- no leaks are possible
==117916== ERROR SUMMARY: 0 errors
```

### External stress (valgrind on a 500-iter script)

```bash
$ cat > /tmp/clox_s14_stress.lox <<'EOF'
var path = "/tmp/clox_s14_external_stress.txt";
var i = 0;
var totalRead = 0;
while (i < 500) {
    if (!io_file_exists(path)) {
        print "missing at iter ";
        print i;
    }
    io_write_file(path, "iteration ");
    var contents = io_read_file(path);
    totalRead = totalRead + string_length(contents);
    i = i + 1;
}
print totalRead;
EOF
$ valgrind --leak-check=full ./bin/clox /tmp/clox_s14_stress.lox
5000
==120303== total heap usage: 3,643 allocs, 3,643 frees
==120303== All heap blocks were freed -- no leaks are possible
==120303== ERROR SUMMARY: 0 errors
```

`totalRead = 5000` matches 500 × 10 (length of
"iteration "). The math holds this time because the
loop body has only one `io_write_file` per iteration.

## The host-boundary lesson, revisited

Stage 11 established four principles for I/O natives:

1. **Side effects are explicit** — use `fwrite` not
   `fputs` because clox strings can contain NUL bytes.
2. **Bounded buffers are mandatory** — explicit sizes
   on every I/O call.
3. **Errors surface at the host boundary** — `io_read_line`
   returns nil on EOF, `io_exit` returns nothing,
   `number_sqrt` throws a runtime error.
4. **The VM loop is aware of state outside the
   stack/heap** — `g_exitRequested` is a global the
   VM checks each tick.

Stage 14 follows all four:

- `fopen`/`fread`/`fwrite` use explicit sizes, not
  string functions. A path with NUL bytes is correctly
  handled because the path is converted to a NUL-
  terminated C string by `memcpy + '\0'`, but the file
  contents are read/written by byte count.
- Buffers are bounded: stack buffer 8 KiB, heap buffer
  for larger files, hard cap at 1 MiB.
- `io_read_file` returns nil on any I/O error, runtime
  error on arity/type. `io_write_file` returns nil on
  I/O error, runtime error on arity/type.
- The VM loop is unaffected by file state — files are
  closed inside the native function before return.

The new wrinkle in Stage 14 is the **size cap**. Stage
11 didn't need one (`io_read_line` is bounded by the
buffer size and the input is user-controlled). Stage 14
needs one because a script can `io_read_file` a 10 GiB
file and OOM the process. The 1 MiB cap is conservative
for a stdlib. A future "big file" stage would do
streaming I/O with a chunked read loop.

## Manual smoke test

```lox
// Round-trip: write a CSV, read it back, parse it.
io_write_file("/tmp/demo.txt", "alpha,beta,gamma,delta");
var contents = io_read_file("/tmp/demo.txt");
print contents;        // alpha,beta,gamma,delta
var parts = string_split(contents, ",");
print array_length(parts);  // 4
print parts[0];        // alpha
print parts[3];        // delta

// Existence check before read.
var config = "/etc/hostname";
if (io_file_exists(config)) {
    var hostname = io_read_file(config);
    print string_trim(hostname);
}

// Empty file is "" not nil.
io_write_file("/tmp/empty.txt", "");
var e = io_read_file("/tmp/empty.txt");
if (e == nil) print "unexpectedly nil";
else if (e == "") print "empty as expected";
else print "unexpectedly non-empty";

// Path that doesn't exist is nil.
var missing = io_read_file("/tmp/nope_xyz_12345.txt");
if (missing == nil) print "missing as expected";
```

All outputs correct. The Stage 13 split/join composes
with Stage 14 file I/O to produce a useful pattern:
read a CSV from disk, parse it, transform the fields,
write the result back. This is a real program that runs
in clox now.

## What this stage teaches

**File I/O host-boundary practice.** Stage 11 covered
the conceptual version (stdin/stdout, bounded by input
rate). Stage 14 covers the *filesystem* version (size
capped, error surfaces differ, path handling matters).
The discipline from Stage 11 carries over: explicit
sizes, errors surface as values, VM loop unaware of
external state.

The new wrinkle is the size cap. A real language needs
streaming I/O for large files. clox doesn't have that
yet, and the 1 MiB cap is the visible bound. A future
stage (Stage 16? Stage 17?) could add an
`io_read_lines(path)` that returns an array of strings,
or an `io_write_lines(path, arr)` for streaming. These
are *new* natives, not extensions of the existing ones.

## What's next

Three live candidates, in priority order:

1. **More stdlib composability demos.** `string_trim`
   + `string_split` is the obvious one. `string_upper` /
   `string_lower` to normalize before splitting. These
   are *user-level* composition rather than
   *implementation* composition — no new natives needed.
   ~50 lines of new tests + a few example programs.
   **My pick for next stage** (pausing here for Tom's
   call).
2. **Streaming I/O.** `io_read_lines(path) -> array of
   strings`, `io_write_lines(path, arr)`. Real
   user-facing gap (no way to handle large files).
   ~150 lines. New natives, but well-understood shape.
3. **Modules.** Still the big one. ~600 lines. Live
   candidate from the option-A list.
4. **Option B: branch sideways.** Apply Stages 1–14 to a
   different project. Unblocked at any time.

**The composability demos are small but pedagogically
important.** They demonstrate that clox is now a real
language — the user can write non-trivial programs in
it, not just toy examples. The lesson is "you've built
the pieces; now show what they can do together." This
is the same lesson as Stage 13, applied at the
program-shape level rather than the stdlib-shape level.

Modules is the bigger ambition. Streaming I/O is the
bigger user-facing gap. Composability demos are the
smallest stage. Per the discipline: pick the smallest
one. Pause here for Tom's call.

# Learnings: Building a Unix Shell from Scratch

## How fork/exec Actually Works

**What I thought:** `fork()` creates a child that starts executing from the beginning of the program, like launching a new process.

**What I learned:** `fork()` creates an exact copy of the current process — same code, same data, same open file descriptors, same instruction pointer. The child continues executing from the line right after `fork()`. The only difference is the return value: parent gets the child's PID, child gets 0.

This means after `fork()`, both parent and child are running the same code. The child then calls `exec()` to replace itself with a different program. This two-step process (fork, then exec) is crucial — it gives the shell a window between fork and exec to set up the child's environment (file descriptors, process groups, signals).

**Key insight:** The reason `fork()` and `exec()` are separate is so the shell can configure the child's execution environment before the new program starts. The child process is the one that calls `dup2()` to set up pipes and redirects, because only the child knows which pipe stage it is.

## The Process Group Model

**What I thought:** Each process is independent. The shell just spawns them and waits.

**What I learned:** A real shell uses **process groups** (pgid) to manage jobs. When you run `ls | grep foo | wc -l`, all three processes are in the same process group. This is how the kernel knows which processes to send signals to when you press Ctrl+C or Ctrl+Z.

The shell:
1. Creates a new process group for each pipeline (using `setpgid()`)
2. Gives the foreground process group terminal control (using `tcsetpgrp()`)
3. Takes terminal control back when the job finishes or stops

**Key insight:** `setpgid()` must be called in BOTH parent and child after fork — there's a race condition, and calling it in both ensures it's set regardless of which runs first.

## Pipe Implementation Surprises

**The ordering problem:** When creating a pipeline like `cmd1 | cmd2 | cmd3`, you need to create ALL pipe file descriptors BEFORE forking any children. If you create and fork one at a time, you get deadlocks.

**The close-on-exec problem:** After setting up pipes with `dup2()`, you MUST close all the original pipe file descriptors in each child. If you forget, the pipe stays open and the next process in the chain never sees EOF, causing it to hang forever waiting for more input.

**The buffered I/O problem:** Built-in commands that use `printf()`/`fputs()` write to C's buffered `stdout`. When a builtin runs in a child process (as part of a pipeline), `_exit()` does NOT flush stdio buffers. You must call `fflush(stdout)` before `_exit()` in child processes, or data is silently lost.

**The dup2 dance:**
- `dup2(pipefd[0], STDIN_FILENO)` — connect pipe read end to stdin
- `dup2(pipefd[1], STDOUT_FILENO)` — connect pipe write end to stdout
- After dup2, close the original pipe fd (it's been duplicated, the original is no longer needed)

## Signal Handling Gotchas

**SIGCHLD race condition:** When a SIGCHLD handler runs, it's called asynchronously. If you call `waitpid()` inside the handler, you might interfere with a `waitpid()` call happening in the main loop. The solution: use `WNOHANG` in a loop to reap all dead children without blocking, and set a flag for the main loop to clean up.

**Ctrl+C (SIGINT):** The shell must NOT die when you press Ctrl+C. Instead, it should forward the signal to the foreground process group. This means:
1. Install a handler that sets a flag (don't kill the shell)
2. When the flag is set, send SIGINT to the foreground job's process group using `kill(-pgid, SIGINT)`
3. The flag approach avoids complex signal-safety issues

**Ctrl+Z (SIGTSTP):** Similarly, the shell must catch SIGTSTP and forward it to the foreground job. When a job is stopped, it goes into the job table with state JOB_STOPPED.

**Terminal control:** The shell must manage which process group owns the terminal using `tcsetpgrp()`. When a foreground job runs, it gets terminal control. When it finishes or stops, the shell takes control back.

## What "Interactive Shell" Really Means

An interactive shell is a **cooperation between three entities**: the shell, the terminal driver, and the kernel.

1. **The shell** creates process groups and manages which one is in the foreground
2. **The terminal driver** sends signals (SIGINT, SIGTSTP) to the foreground process group
3. **The kernel** enforces that only the foreground process group can read from the terminal

When you type Ctrl+C:
1. The terminal driver sees the interrupt character
2. It sends SIGINT to the entire foreground process group
3. The shell (which is NOT in the foreground group) catches the signal via its handler
4. The shell's handler sets a flag; the main loop checks it and acts accordingly

This is why non-interactive shells (like shell scripts) behave differently — they don't have job control, don't manage process groups, and often ignore signals.

## Built-in Commands Need Special Treatment

Commands like `cd`, `export`, and `exit` MUST run in the shell process itself, not in a child, because they modify shell state (current directory, environment variables). But when a builtin has redirections (e.g., `echo foo > file.txt`), the redirections must be set up first.

The solution: save the current file descriptors with `dup()`, set up redirections with `dup2()`, run the builtin, then restore the original fds. This is called "save/restore" and it's what real shells do.

## The `2>` Tokenizer Trick

The tokenizer must check for `2>` (stderr redirect) BEFORE treating `2` as a regular word. If it sees `2` followed by `>`, it's a redirect operator, not the number two followed by a greater-than sign. This means the tokenizer needs lookahead.

## Memory Management in C

Every `malloc()` needs a corresponding `free()`. Every `strdup()` needs a `free()`. Every `realloc()` needs careful handling of the NULL case (when count is 0). The string buffer (`strbuf_t`) pattern with grow-on-demand and `sb_finish()` that transfers ownership is clean and avoids leaks.

**The valgrind lesson:** Valgrind found bugs I never would have caught manually — writing past the end of a buffer because `realloc(ptr, 0)` returns NULL, and then using the old (freed) pointer. Zero memory leaks in the final version confirmed the discipline was worth it.

## Key Takeaway

Building a shell teaches you more about Unix process management than reading about it. The conceptual model becomes visceral when you have to:
- Manage process groups and terminal control
- Handle asynchronous signals without races
- Coordinate multiple file descriptors across forked processes
- Ensure every allocated byte is freed
- Handle edge cases like empty input, missing commands, broken pipes

The shell is where the rubber meets the road between the "everything is a file" abstraction and the actual mechanics of process creation, I/O redirection, and signal delivery.
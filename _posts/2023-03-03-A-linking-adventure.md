---
layout:     post
title:      "A Linking Adventure"
subtitle:   "The long journey to main()"
date:       2023-03-03 12:00:00
catalog: true
tags: [Linkers]
---

## Background
It occurred to me recently that I never hear of anyone linking their programs manually - meaning, running `ld` directly rather than through a wrapper utility like the `gcc` or `clang` drivers.

Given two simple translation units, I thought, one should be able to:
* invoke `gcc` with the `-c` flag, which skips the linking step and outputs the object files
* invoke `ld` with the object files, asking it politely to output a statically-linked binary
* run the binary and move on with my life


## The Program
We use a very simple program, defined across two files, `square.c` and `main.c`, for this exploration.

```cpp
// square.c
int square(int n) { return n * n; }
```

The program simply exits with status code 9 by calling `square(int)`, defined in a different translation unit, to square 4.

```cpp
//main.c
int square(int);
int main() { return square(4); }
```

By running `gcc -c main.c square.c`, we get our two object files - `main.o` and `square.o`. The `file` utility describes both of these files as `ELF 64-bit LSB relocatable, x86-64, version 1 (SYSV), not stripped`. To parse this a bit:
* [ELF](https://en.wikipedia.org/wiki/Executable_and_Linkable_Format) is the predominant file format for executables, libraries, and object files in Unix systems.
* `relocatable` means that `ld` can use it in the [relocation](https://en.wikipedia.org/wiki/Relocation_(computing)) process to produce an executable or shared object.
* the ELF file is `not stripped` of debug symbols
* `x86-64, version 1 (SYSV)` is the execution environment or 'target' we compiled for - `LSB` here stands for 'least significant byte`, indicting the target is little-endian.

## Our troubles _start

Running `ld main.o library.o` doesn't *fail*, but prints `ld: warning: cannot find entry symbol _start; defaulting to 0000000000401000`. The resulting binary simply seg-faults when run. Uh oh.

The message is quite straightforward: it looks like the linker used the `_start` function as the entry point for this target, and cannot find it. We can confirm that this symbol is not found in either of our object files using the `nm` - a neat utility that lists the symbols in an object file:

```bash
$ nm main.o square.o

main.o:
                 U _GLOBAL_OFFSET_TABLE_
0000000000000000 T main
                 U square

square.o:
0000000000000000 T square
```

Note that `main.o` contains an undefined (`U` in the output) reference to `_GLOBAL_OFFSET_TABLE_`. In short, the Global Offset Table (GOT) is a structure, primarily operated on by the *runtime linker* or loader, which allows for global symbols to be located during execution. This is why that only `main.o`, which references a global symbol in `square`, has a reference to the GOT.

Having ruled out `gcc -c` introducing the `_start` symbol in either of our translation units, we may surmise that it must come from another relocatable.


## Peeling the onion

If the entrypoint symbol is not provided by the programmer, and not introduced by the compiler, where does it come from? We can use GCC's verbose mode to observe how it produces a statically-linked executable from object files; `gcc --verbose --static main.o square.o` uncovers no direct invocation of `ld`, but rather a wrapper utility called [collect2](http://gcc.gnu.org/onlinedocs/gccint/Collect2.html). Although `collect2` did not have a manual entry, it turned out to have a flag to toggle verbose mode just like the GCC driver, which revealed the `ld` invocation we've been looking for:

```
/usr/bin/ld -v -plugin /usr/lib/gcc/x86_64-linux-gnu/9/liblto_plugin.so
 -plugin-opt=/usr/lib/gcc/x86_64-linux-gnu/9/lto-wrapper -plugin-opt=-fresolution=/tmp/ccXoZG5w.res
-plugin-opt=-pass-through=-lgcc -plugin-opt=-pass-through=-lgcc_eh -plugin-opt=-pass-through=-lc --build-id
-m elf_x86_64 --hash-style=gnu --as-needed -static -z relro
/usr/lib/gcc/x86_64-linux-gnu/9/../../../x86_64-linux-gnu/crt1.o /usr/lib/gcc/x86_64-linux-gnu/9/../../../x86_64-linux-gnu/crti.o
/usr/lib/gcc/x86_64-linux-gnu/9/crtbeginT.o -L/usr/lib/gcc/x86_64-linux-gnu/9 -L/usr/lib/gcc/x86_64-linux-gnu/9/../../../x86_64-linux-gnu
-L/usr/lib/gcc/x86_64-linux-gnu/9/../../../../lib -L/lib/x86_64-linux-gnu -L/lib/../lib -L/usr/lib/x86_64-linux-gnu
-L/usr/lib/../lib -L/usr/lib/gcc/x86_64-linux-gnu/9/../../.. main.o square.o --start-group -lgcc -lgcc_eh -lc
--end-group /usr/lib/gcc/x86_64-linux-gnu/9/crtend.o /usr/lib/gcc/x86_64-linux-gnu/9/../../../x86_64-linux-gnu/crtn.o
```

Wow - that is a *lot of flags*.

Surely, not all of them are needed for our toy program? We don't even utilize the standard library. With a little bit of trial and error, I removed a handful of libraries that deal with link time optimization, exception handling (gcc_eh), etc and was still able to link a working binary with this command:

```
/usr/bin/ld -static /usr/lib/x86_64-linux-gnu/crt1.o /usr/lib/x86_64-linux-gnu/crti.o -L/usr/lib/gcc/x86_64-linux-gnu/9 main.o square.o --start-group -lgcc -lgcc_eh -lc --end-group /usr/lib/gcc/x86_64-linux-gnu/9/crtend.o /usr/lib/x86_64-linux-gnu/crtn.o
```

What does each remaining input supply?
* To answer our original question, `nm /usr/lib/x86_64-linux-gnu/crt1.o` reveals that `_start` is defined within the text segment:
```
$ nm /usr/lib/x86_64-linux-gnu/crt1.o
0000000000000000 D __data_start
0000000000000000 W data_start
0000000000000030 T _dl_relocate_static_pie
                 U _GLOBAL_OFFSET_TABLE_
0000000000000000 R _IO_stdin_used
                 U __libc_csu_fini
                 U __libc_csu_init
                 U __libc_start_main
                 U main
0000000000000000 T _start
```
* the undefined `__libc_csu_init` is found in `/lib/x86_64-linux-gnu/libc.a`...
* ...which depends on the `_init` symbol defined in `/usr/lib/x86_64-linux-gnu/crti.o` and `printf` implementations in `libgcc`...

... and so on an so forth. The statically-linked binary for our program that does [nearly] nothing is a whopping 851 KiB. `nm` reveals 1704 symbols, of which 726 are defined in the text section, including `malloc`, `qsort` and `fprintf`. It works, but surely we can do better.

## Becoming a self-starter

Let's recall how we got on the path down this rabbit-hole - `ld` was looking for an appropriate entrypoint named `_start`, which happened to be defined in `crt1.o`. While `main` is the program entrypoint from most programmers' perspective, `_start` is *usually* the name of the code that serves as the *operating system's entrypoint*. There is [quite a bit of work](http://dbp-consulting.com/tutorials/debugging/linuxProgramStartup.html) involved in program startup on Linux (for example, `__libc_csu_init` which we saw in `crt1` ends up calling the constructors of global objects in C++). 

Luckily for us, our program is *not* most programs. It is much simpler - which should make a lot of the heavy lifting before `main` optional.

### What do we need from `_start`?

In short, `_start` needs to:
* Do anything that "needs to be done" before passing control to `main`. This is intentionally vague and depends on the programming language and execution environment (like constructing globals, aligning the start of the stack at a nice boundary, etc).
* If `main` returns control, do any cleanup before returning control to the OS via a system call like `[exit](https://en.wikipedia.org/wiki/Exit_(system_call))`. 

For our toy program, there is almost no additional accounting required. We can implement our setup and teardown logic in about 5 lines:

```cpp
// custom_start.c

#include <sys/syscall.h>

int main();

// In the System V AMD-64 ABI, the first integer arg in
// user-level applications is passed in register %rdi,
// so %rdi holds `main`'s return value. The second arg
// is passed in %rsi, which here holds the sycall number
// for the `exit` system call.
void call_exit(int code, int exit_syscall_num) {
	asm("mov %rsi, %rax;"  // Copy syscall number into %rax
	    "syscall;");       // exit's arg is already in %rsi
}

void _start() { call_exit(main(), SYS_exit); }
```

Recall the target machine described in our ELF file was `x86-64, version 1 (SYSV)`. The System V AMD-64 (or x64-64) [ABI](https://uclibc.org/docs/psABI-x86_64.pdf) specifies how such an execution environment should act - right down to which registers or sections of memory must be used to pass values to and from called functions, known as the *calling convention*.

We use these guidelines to implement our custom startup and teardown code. The section we're most interested in in A.2.1:

* "The number of the syscall has to be passed in register %rax" - we'll want to put the code for `exit` in `%rax` before executing the `syscall` instruction.

* "User-level applications use integer registers for passing the sequence
`%rdi`, `%rsi`, `%rdx`, `%rcx`, `%r8` and `%r9`. The kernel interface uses `%rdi`,
`%rsi`, `%rdx`, `%r10`, `%r8` and `%r9` ... " - we never pass more than two arguments, so for our purposes, `%rdi` is argument 0, and `%rsi` is argument 1 for both user-level functions and system calls.

Let's give it a try:

Running `gcc -c -static custom_start.c main.c square.c` and linking with `ld -static main.o square.o custom_start.o` gives us an executable which exits with the value `16` (if you're following along, you can run `./a.out ; echo "$?"`).

Our new statically-linked executable is 9.1 KiB large, versus the 851 KiB from our last attempt. There are 7 total symbols , down from 1704:

```
0000000000404000 R __bss_start
0000000000401027 T call_exit
0000000000404000 R _edata
0000000000404000 R _end
0000000000401000 T main
0000000000401014 T square
000000000040103d T _start
```

(9.1 KiB is still a little larger than I would have expected for such a barebones executable - worth a deeper look.)
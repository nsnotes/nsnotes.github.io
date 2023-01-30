
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
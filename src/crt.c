/*
 * Minimal runtime for the Vita launcher.
 * Replaces the VitaSDK crt0/newlib glue: entry point, heap (_sbrk)
 * on a Sce memblock and stubbed syscalls for newlib.
 */
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <sys/stat.h>
#include <errno.h>
#include <stddef.h>

#undef errno
extern int errno;

#define HEAP_SIZE (96 * 1024 * 1024)   /* keep in sync with platform_vita.c */

static char *heap_base = NULL;
static char *heap_cur = NULL;
static char *heap_end = NULL;

int main(void);

static void heap_init(void)
{
	SceUID id = sceKernelAllocMemBlock("vl_heap", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,
	                                   HEAP_SIZE, NULL);
	if (id < 0)
		return;
	void *base = NULL;
	if (sceKernelGetMemBlockBase(id, &base) < 0)
		return;
	heap_base = heap_cur = (char *)base;
	heap_end = heap_base + HEAP_SIZE;
}

void *_sbrk(ptrdiff_t incr)
{
	if (!heap_base)
		heap_init();
	if (!heap_base || heap_cur + incr > heap_end || heap_cur + incr < heap_base) {
		errno = ENOMEM;
		return (void *)-1;
	}
	char *prev = heap_cur;
	heap_cur += incr;
	return prev;
}

int _write(int fd, const void *buf, size_t n) { (void)fd; (void)buf; return (int)n; }
int _read(int fd, void *buf, size_t n) { (void)fd; (void)buf; (void)n; return 0; }
int _close(int fd) { (void)fd; return -1; }
int _lseek(int fd, int off, int whence) { (void)fd; (void)off; (void)whence; return 0; }
int _fstat(int fd, struct stat *st) { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd) { (void)fd; return 1; }
int _getpid(void) { return 1; }
int _kill(int pid, int sig) { (void)pid; (void)sig; errno = EINVAL; return -1; }
void _exit(int status) { sceKernelExitProcess(status); for (;;) {} }

/* Entry point called by the Vita module loader. */
__attribute__((section(".text.start"), used))
int _start(unsigned int args, void *argp)
{
	(void)args; (void)argp;
	heap_init();
	int r = main();
	sceKernelExitProcess(r);
	return r;
}

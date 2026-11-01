/* Minimal newlib stubs so the link never depends on which --specs the toolchain wrapper adds. */
#include <errno.h>
#include <sys/stat.h>

void *_sbrk(int incr)                         { (void)incr; errno = ENOMEM; return (void *)-1; }
int   _write(int fd, const char *b, int n)    { (void)fd; (void)b; return n; }
int   _read(int fd, char *b, int n)           { (void)fd; (void)b; (void)n; return 0; }
int   _close(int fd)                          { (void)fd; return -1; }
int   _lseek(int fd, int p, int d)            { (void)fd; (void)p; (void)d; return 0; }
int   _fstat(int fd, struct stat *st)         { (void)fd; st->st_mode = S_IFCHR; return 0; }
int   _isatty(int fd)                         { (void)fd; return 1; }
int   _kill(int pid, int sig)                 { (void)pid; (void)sig; errno = EINVAL; return -1; }
int   _getpid(void)                           { return 1; }
void  _exit(int status)                       { (void)status; for (;;) {} }

#include <cstdint>
#include <errno.h>

extern "C" {

// Newlib heap for std::vector (CommandDispatcher outbound frames).
static std::uint8_t g_heap[32 * 1024];
static std::uint8_t* g_heap_end = g_heap;

void* _sbrk(int incr)
{
    std::uint8_t* prev = g_heap_end;
    if (g_heap_end + incr > g_heap + sizeof(g_heap))
    {
        errno = ENOMEM;
        return reinterpret_cast<void*>(static_cast<std::ptrdiff_t>(-1));
    }
    g_heap_end += incr;
    return prev;
}

int _close(int)
{
    return -1;
}

int _lseek(int, int, int)
{
    return 0;
}

int _read(int, char*, int)
{
    return 0;
}

int _write(int, char*, int len)
{
    return len;
}

void _exit(int)
{
    for (;;)
    {
    }
}

int _kill(int, int)
{
    return -1;
}

int _getpid(void)
{
    return 1;
}

int _isatty(int)
{
    return 1;
}

int _fstat(int, void*)
{
    return 0;
}
}

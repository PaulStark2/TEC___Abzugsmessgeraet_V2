// Newlib stubs for the electronics test. Same set as firmware/src/syscalls.cpp.
// This file is not shared with the production firmware.

extern "C" {

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

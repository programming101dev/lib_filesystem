#include "p101_filesystem/filesystem.h"
#include <p101_env/wrapper.h>
#ifdef __linux__
    #include <crypt.h>
#endif
#include <unistd.h>

void p101_sync(const struct p101_env *env)
{
    P101_TRACE(env);
    errno = 0;
    sync();
    P101_TRACE_EXIT(env);
}

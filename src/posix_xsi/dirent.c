#include "p101_filesystem/filesystem.h"
#include <p101_env/wrapper.h>

void p101_seekdir(const struct p101_env *env, DIR *dirp, long loc)
{
    P101_TRACE(env);
    errno = 0;
    seekdir(dirp, loc);
    P101_TRACE_EXIT(env);
}

long p101_telldir(const struct p101_env *env, struct p101_error *err, DIR *dirp)
{
    long ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1L);
    errno   = 0;
    ret_val = telldir(dirp);

    if(ret_val == -1L && errno != 0)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

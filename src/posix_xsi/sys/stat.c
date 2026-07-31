#include "p101_filesystem/filesystem.h"
#include <p101_env/wrapper.h>
#include <sys/stat.h>

int p101_mknod(const struct p101_env *env, struct p101_error *err, const char *path, mode_t mode, dev_t dev)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, -1);
    errno   = 0;
    ret_val = mknod(path, mode, dev);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_TRACE_EXIT(env);
    return ret_val;
}

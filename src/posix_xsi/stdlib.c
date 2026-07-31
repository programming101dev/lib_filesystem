#include "p101_filesystem/filesystem.h"
#include <p101_c/p101_string.h>
#include <p101_env/wrapper.h>
#include <stdlib.h>
#include <string.h>

char *p101_realpath(const struct p101_env *env, struct p101_error *err, const char *restrict file_name, char *restrict resolved_name)
{
    char *ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, NULL);
    errno   = 0;
    ret_val = realpath(file_name, resolved_name);

    if(ret_val == NULL)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }
    else if(resolved_name == NULL)
    {
        P101_TRACK_ALLOC(env, ret_val, p101_strlen(env, ret_val) + 1U);
    }

    P101_TRACE_EXIT(env);
    return ret_val;
}

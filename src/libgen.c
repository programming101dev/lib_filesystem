/*
 * Copyright 2026 D'Arcy Smith.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "p101_filesystem/p101_libgen.h"
#include <libgen.h>
#include <p101_env/wrapper.h>

/*
 * These wrappers want the POSIX basename from <libgen.h>, which takes char *
 * and may modify the path in place. glibc's <string.h> declares a different
 * GNU basename under _GNU_SOURCE that takes const char * and never modifies
 * its argument; the fact analyzers parse this file with _GNU_SOURCE on Linux.
 * <libgen.h> resolves this by defining basename as __xpg_basename, so its
 * absence means the GNU declaration won and p101_basename would silently
 * stop modifying the caller's buffer. Fail loudly instead.
 */
#if defined(__GLIBC__) && !defined(basename)
    #error "glibc: POSIX basename from <libgen.h> is not in effect; check include order"
#endif

char *p101_basename(const struct p101_env *env, struct p101_error *err, char *path)
{
    char *ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, NULL);
    errno   = 0;
    ret_val = basename(path);

    if(ret_val == NULL && errno != 0)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

char *p101_dirname(const struct p101_env *env, struct p101_error *err, char *path)
{
    char *ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, NULL);
    errno   = 0;
    ret_val = dirname(path);

    if(ret_val == NULL && errno != 0)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

/*
 *Copyright 2021-2024 D'Arcy Smith.
 *
 *Licensed under the Apache License, Version 2.0 (the "License");
 *you may not use this file except in compliance with the License.
 *You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *Unless required by applicable law or agreed to in writing, software
 *distributed under the License is distributed on an "AS IS" BASIS,
 *WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *See the License for the specific language governing permissions and
 *limitations under the License.
 */

#include "p101_filesystem/filesystem.h"
#include <p101_env/wrapper.h>
#include <stdint.h>

static int stdio_error_code(int error_code);

static int stdio_error_code(int error_code)
{
    if(error_code == 0)
    {
        error_code = EIO;
    }

    return error_code;
}

int p101_renameat(const struct p101_env *env, struct p101_error *err, int oldfd, const char *old_name, int newfd, const char *new_name)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = renameat(oldfd, old_name, newfd, new_name);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, stdio_error_code(errno));
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

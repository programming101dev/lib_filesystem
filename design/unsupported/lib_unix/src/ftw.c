/*
 * Copyright 2022-2024 D'Arcy Smith.
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

#include "p101_unix/p101_ftw.h"

// cppcheck-suppress funcArgNamesDifferentUnnamed
int p101_ftw(const struct p101_env *env, struct p101_error *err, const char *path, p101_ftw_fn fn, int ndirs)
{
    int ret_val;

    P101_TRACE(env);
    errno   = 0;
    ret_val = ftw(path, fn, ndirs);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_TRACE_EXIT(env);
    return ret_val;
}

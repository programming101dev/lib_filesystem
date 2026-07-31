#ifndef LIBP101_UNIX_P101_FTW_H
#define LIBP101_UNIX_P101_FTW_H

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

#include <ftw.h>
#include <p101_env/env.h>

/*
 * Source-controlled migration placeholder; this header is not installed.
 * The supported wrapper is <p101_posix_xsi/p101_ftw.h>.
 */
typedef int (*p101_ftw_fn)(const char *fpath, const struct stat *sb, int typeflag);

#ifdef __cplusplus
extern "C"
{
#endif

    int p101_ftw(const struct p101_env *env, struct p101_error *err, const char *path, p101_ftw_fn fn, int ndirs);

#ifdef __cplusplus
}
#endif

#endif    // LIBP101_UNIX_P101_FTW_H

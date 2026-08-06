#ifndef LIBP101_FILESYSTEM_P101_STDLIB_H
#define LIBP101_FILESYSTEM_P101_STDLIB_H

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

#ifndef LIBP101_FILESYSTEM_SHARED_DECLARATIONS
    #define LIBP101_FILESYSTEM_SHARED_DECLARATIONS
    #include <dirent.h>
    #include <ftw.h>
    #include <glob.h>
    #include <p101_env/env.h>
    #include <p101_error/attributes.h>
    #include <stdio.h>
    #include <sys/stat.h>
    #include <sys/statvfs.h>
    #include <sys/types.h>
    #include <unistd.h>

typedef int (*p101_ftw_fn)(const char *fpath, const struct stat *sb, int typeflag);
typedef int (*p101_nftw_fn)(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf);
#endif    // LIBP101_FILESYSTEM_SHARED_DECLARATIONS

#ifdef __cplusplus
extern "C"
{
#endif

    char *p101_mkdtemp(const struct p101_env *env, struct p101_error *err, char *name_template);
    int   p101_mkstemp(const struct p101_env *env, struct p101_error *err, char *name_template);
    char *p101_realpath(const struct p101_env *env, struct p101_error *err, const char *restrict file_name, char *restrict resolved_name) P101_ATTR_WARN_UNUSED_RESULT;

#ifdef __cplusplus
}
#endif

#endif    // LIBP101_FILESYSTEM_P101_STDLIB_H

#ifndef LIBP101_FILESYSTEM_SYS_P101_STAT_H
#define LIBP101_FILESYSTEM_SYS_P101_STAT_H

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

    int    p101_chmod(const struct p101_env *env, struct p101_error *err, const char *path, mode_t mode);
    int    p101_fchmod(const struct p101_env *env, struct p101_error *err, int fildes, mode_t mode);
    int    p101_fchmodat(const struct p101_env *env, struct p101_error *err, int fd, const char *path, mode_t mode, int flag);
    int    p101_fstat(const struct p101_env *env, struct p101_error *err, int fildes, struct stat *buf);
    int    p101_fstatat(const struct p101_env *env, struct p101_error *err, int fd, const char *restrict path, struct stat *restrict buf, int flag);
    int    p101_futimens(const struct p101_env *env, struct p101_error *err, int fd, const struct timespec times[2]);
    int    p101_lstat(const struct p101_env *env, struct p101_error *err, const char *restrict path, struct stat *restrict buf);
    int    p101_mkdir(const struct p101_env *env, struct p101_error *err, const char *path, mode_t mode);
    int    p101_mkdirat(const struct p101_env *env, struct p101_error *err, int fd, const char *path, mode_t mode);
    int    p101_mknod(const struct p101_env *env, struct p101_error *err, const char *path, mode_t mode, dev_t dev);
    int    p101_stat(const struct p101_env *env, struct p101_error *err, const char *restrict path, struct stat *restrict buf);
    mode_t p101_umask(const struct p101_env *env, mode_t cmask);
    int    p101_utimensat(const struct p101_env *env, struct p101_error *err, int fd, const char *path, const struct timespec times[2], int flag);

#ifdef __cplusplus
}
#endif

#endif    // LIBP101_FILESYSTEM_SYS_P101_STAT_H

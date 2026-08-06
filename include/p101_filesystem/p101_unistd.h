#ifndef LIBP101_FILESYSTEM_P101_UNISTD_H
#define LIBP101_FILESYSTEM_P101_UNISTD_H

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

    int     p101_access(const struct p101_env *env, struct p101_error *err, const char *path, int amode);
    int     p101_chdir(const struct p101_env *env, struct p101_error *err, const char *path);
    int     p101_chown(const struct p101_env *env, struct p101_error *err, const char *path, uid_t owner, gid_t group);
    int     p101_faccessat(const struct p101_env *env, struct p101_error *err, int fd, const char *path, int amode, int flag);
    int     p101_fchdir(const struct p101_env *env, struct p101_error *err, int fildes);
    int     p101_fchown(const struct p101_env *env, struct p101_error *err, int fildes, uid_t owner, gid_t group);
    int     p101_fchownat(const struct p101_env *env, struct p101_error *err, int fd, const char *path, uid_t owner, gid_t group, int flag);
    long    p101_fpathconf(const struct p101_env *env, struct p101_error *err, int fildes, int name);
    int     p101_ftruncate(const struct p101_env *env, struct p101_error *err, int fildes, off_t length);
    char   *p101_getcwd(const struct p101_env *env, struct p101_error *err, char *buf, size_t size);
    int     p101_lchown(const struct p101_env *env, struct p101_error *err, const char *path, uid_t owner, gid_t group);
    int     p101_link(const struct p101_env *env, struct p101_error *err, const char *path1, const char *path2);
    int     p101_linkat(const struct p101_env *env, struct p101_error *err, int fd1, const char *path1, int fd2, const char *path2, int flag);
    long    p101_pathconf(const struct p101_env *env, struct p101_error *err, const char *path, int name);
    ssize_t p101_readlink(const struct p101_env *env, struct p101_error *err, const char *restrict path, char *restrict buf, size_t bufsize);
    ssize_t p101_readlinkat(const struct p101_env *env, struct p101_error *err, int fd, const char *restrict path, char *restrict buf, size_t bufsize);
    int     p101_rmdir(const struct p101_env *env, struct p101_error *err, const char *path);
    int     p101_symlink(const struct p101_env *env, struct p101_error *err, const char *path1, const char *path2);
    int     p101_symlinkat(const struct p101_env *env, struct p101_error *err, const char *path1, int fd, const char *path2);
    void    p101_sync(const struct p101_env *env);
    int     p101_truncate(const struct p101_env *env, struct p101_error *err, const char *path, off_t length);
    int     p101_unlink(const struct p101_env *env, struct p101_error *err, const char *path);
    int     p101_unlinkat(const struct p101_env *env, struct p101_error *err, int fd, const char *path, int flag);

#ifdef __cplusplus
}
#endif

#endif    // LIBP101_FILESYSTEM_P101_UNISTD_H

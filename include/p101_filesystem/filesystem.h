#ifndef LIBP101_FILESYSTEM_FILESYSTEM_H
#define LIBP101_FILESYSTEM_FILESYSTEM_H

/*
 * Copyright 2026 D'Arcy Smith.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 */

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

#ifdef __cplusplus
extern "C"
{
#endif

    int            p101_access(const struct p101_env *env, struct p101_error *err, const char *path, int amode);
    int            p101_alphasort(const struct p101_env *env, const struct dirent **d1, const struct dirent **d2);
    char          *p101_basename(const struct p101_env *env, struct p101_error *err, char *path);
    int            p101_chdir(const struct p101_env *env, struct p101_error *err, const char *path);
    int            p101_chmod(const struct p101_env *env, struct p101_error *err, const char *path, mode_t mode);
    int            p101_chown(const struct p101_env *env, struct p101_error *err, const char *path, uid_t owner, gid_t group);
    int            p101_closedir(const struct p101_env *env, struct p101_error *err, DIR *dirp);
    int            p101_dirfd(const struct p101_env *env, struct p101_error *err, DIR *dirp);
    char          *p101_dirname(const struct p101_env *env, struct p101_error *err, char *path);
    int            p101_faccessat(const struct p101_env *env, struct p101_error *err, int fd, const char *path, int amode, int flag);
    int            p101_fchdir(const struct p101_env *env, struct p101_error *err, int fildes);
    int            p101_fchmod(const struct p101_env *env, struct p101_error *err, int fildes, mode_t mode);
    int            p101_fchmodat(const struct p101_env *env, struct p101_error *err, int fd, const char *path, mode_t mode, int flag);
    int            p101_fchown(const struct p101_env *env, struct p101_error *err, int fildes, uid_t owner, gid_t group);
    int            p101_fchownat(const struct p101_env *env, struct p101_error *err, int fd, const char *path, uid_t owner, gid_t group, int flag);
    DIR           *p101_fdopendir(const struct p101_env *env, struct p101_error *err, int fd) P101_ATTR_WARN_UNUSED_RESULT;
    int            p101_fnmatch(const struct p101_env *env, struct p101_error *err, const char *pattern, const char *string, int flags);
    long           p101_fpathconf(const struct p101_env *env, struct p101_error *err, int fildes, int name);
    int            p101_fstat(const struct p101_env *env, struct p101_error *err, int fildes, struct stat *buf);
    int            p101_fstatat(const struct p101_env *env, struct p101_error *err, int fd, const char *restrict path, struct stat *restrict buf, int flag);
    int            p101_fstatvfs(const struct p101_env *env, struct p101_error *err, int fildes, struct statvfs *buf);
    int            p101_ftruncate(const struct p101_env *env, struct p101_error *err, int fildes, off_t length);
    int            p101_ftw(const struct p101_env *env, struct p101_error *err, const char *path, p101_ftw_fn fn, int ndirs);
    int            p101_futimens(const struct p101_env *env, struct p101_error *err, int fd, const struct timespec times[2]);
    char          *p101_getcwd(const struct p101_env *env, struct p101_error *err, char *buf, size_t size);
    int            p101_glob(const struct p101_env *env, struct p101_error *err, const char *restrict pattern, int flags, int (*errfunc)(const char *epath, int eerrno), glob_t *restrict pglob);
    void           p101_globfree(const struct p101_env *env, glob_t *pglob);
    int            p101_lchown(const struct p101_env *env, struct p101_error *err, const char *path, uid_t owner, gid_t group);
    int            p101_link(const struct p101_env *env, struct p101_error *err, const char *path1, const char *path2);
    int            p101_linkat(const struct p101_env *env, struct p101_error *err, int fd1, const char *path1, int fd2, const char *path2, int flag);
    int            p101_lstat(const struct p101_env *env, struct p101_error *err, const char *restrict path, struct stat *restrict buf);
    int            p101_mkdir(const struct p101_env *env, struct p101_error *err, const char *path, mode_t mode);
    int            p101_mkdirat(const struct p101_env *env, struct p101_error *err, int fd, const char *path, mode_t mode);
    char          *p101_mkdtemp(const struct p101_env *env, struct p101_error *err, char *name_template);
    int            p101_mknod(const struct p101_env *env, struct p101_error *err, const char *path, mode_t mode, dev_t dev);
    int            p101_mkstemp(const struct p101_env *env, struct p101_error *err, char *name_template);
    int            p101_nftw(const struct p101_env *env, struct p101_error *err, const char *path, int (*fn)(const char *, const struct stat *, int, struct FTW *), int fd_limit, int flags);
    DIR           *p101_opendir(const struct p101_env *env, struct p101_error *err, const char *dirname) P101_ATTR_WARN_UNUSED_RESULT;
    long           p101_pathconf(const struct p101_env *env, struct p101_error *err, const char *path, int name);
    struct dirent *p101_readdir(const struct p101_env *env, struct p101_error *err, DIR *dirp);
    ssize_t        p101_readlink(const struct p101_env *env, struct p101_error *err, const char *restrict path, char *restrict buf, size_t bufsize);
    ssize_t        p101_readlinkat(const struct p101_env *env, struct p101_error *err, int fd, const char *restrict path, char *restrict buf, size_t bufsize);
    char          *p101_realpath(const struct p101_env *env, struct p101_error *err, const char *restrict file_name, char *restrict resolved_name) P101_ATTR_WARN_UNUSED_RESULT;
    int            p101_renameat(const struct p101_env *env, struct p101_error *err, int oldfd, const char *old_name, int newfd, const char *new_name);
    void           p101_rewinddir(const struct p101_env *env, DIR *dirp);
    int            p101_rmdir(const struct p101_env *env, struct p101_error *err, const char *path);
    int            p101_scandir(const struct p101_env *env, struct p101_error *err, const char *dir, struct dirent ***namelist, int (*sel)(const struct dirent *), int (*compar)(const struct dirent **, const struct dirent **));
    void           p101_seekdir(const struct p101_env *env, DIR *dirp, long loc);
    int            p101_stat(const struct p101_env *env, struct p101_error *err, const char *restrict path, struct stat *restrict buf);
    int            p101_statvfs(const struct p101_env *env, struct p101_error *err, const char *restrict path, struct statvfs *restrict buf);
    int            p101_symlink(const struct p101_env *env, struct p101_error *err, const char *path1, const char *path2);
    int            p101_symlinkat(const struct p101_env *env, struct p101_error *err, const char *path1, int fd, const char *path2);
    void           p101_sync(const struct p101_env *env);
    long           p101_telldir(const struct p101_env *env, struct p101_error *err, DIR *dirp);
    int            p101_truncate(const struct p101_env *env, struct p101_error *err, const char *path, off_t length);
    mode_t         p101_umask(const struct p101_env *env, mode_t cmask);
    int            p101_unlink(const struct p101_env *env, struct p101_error *err, const char *path);
    int            p101_unlinkat(const struct p101_env *env, struct p101_error *err, int fd, const char *path, int flag);
    int            p101_utimensat(const struct p101_env *env, struct p101_error *err, int fd, const char *path, const struct timespec times[2], int flag);

#ifdef __cplusplus
}
#endif

#endif    // LIBP101_FILESYSTEM_FILESYSTEM_H

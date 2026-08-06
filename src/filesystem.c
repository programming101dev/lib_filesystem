/*
 * Copyright 2021-2024 D'Arcy Smith.
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

#include "p101_filesystem/filesystem.h"
#include <p101_env/wrapper.h>

int p101_alphasort(const struct p101_env *env, const struct dirent **d1, const struct dirent **d2)
{
    int ret_val;

    P101_TRACE(env);
    errno   = 0;
    ret_val = alphasort(d1, d2);

    P101_TRACE_EXIT(env);
    return ret_val;
}

int p101_closedir(const struct p101_env *env, struct p101_error *err, DIR *dirp)
{
    char resource_id[P101_ENV_POINTER_RESOURCE_ID_SIZE];
    int  fd;
    int  ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    p101_env_pointer_resource_id(resource_id, sizeof(resource_id), dirp);
    fd      = dirfd(dirp);
    errno   = 0;
    ret_val = closedir(dirp);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }
    else if(fd >= 0)
    {
        P101_TRACK_CLOSE(env, fd);
        P101_TRACK_RESOURCE_RELEASE(env, "directory-stream", resource_id, NULL);
    }
    else
    {
        P101_TRACK_RESOURCE_RELEASE(env, "directory-stream", resource_id, NULL);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_dirfd(const struct p101_env *env, struct p101_error *err, DIR *dirp)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = dirfd(dirp);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

DIR *p101_fdopendir(const struct p101_env *env, struct p101_error *err, int fd)
{
    DIR *ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, NULL);
    errno   = 0;
    ret_val = fdopendir(fd);

    if(ret_val == NULL && errno != 0)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }
    else if(ret_val != NULL)
    {
        P101_TRACK_POINTER_RESOURCE_ACQUIRE(env, "directory-stream", ret_val, 0U, "fdopendir");
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

DIR *p101_opendir(const struct p101_env *env, struct p101_error *err, const char *dirname)
{
    DIR *ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, NULL);
    errno   = 0;
    ret_val = opendir(dirname);

    if(ret_val == NULL)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }
    else
    {
        int fd;

        fd = dirfd(ret_val);
        if(fd >= 0)
        {
            P101_TRACK_OPEN(env, fd);
        }
        P101_TRACK_POINTER_RESOURCE_ACQUIRE(env, "directory-stream", ret_val, 0U, dirname);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

struct dirent *p101_readdir(const struct p101_env *env, struct p101_error *err, DIR *dirp)
{
    struct dirent *ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, NULL);
    errno   = 0;
    ret_val = readdir(dirp);    // cppcheck-suppress readdirCalled

    if(ret_val == NULL && errno != 0)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

void p101_rewinddir(const struct p101_env *env, DIR *dirp)
{
    P101_TRACE(env);
    errno = 0;
    rewinddir(dirp);
    P101_TRACE_EXIT(env);
}

/* cppcheck-suppress funcArgNamesDifferentUnnamed */
int p101_scandir(const struct p101_env *env, struct p101_error *err, const char *dir, struct dirent ***namelist, int (*sel)(const struct dirent *), int (*compar)(const struct dirent **, const struct dirent **))
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = scandir(dir, namelist, sel, compar);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }
    else
    {
        if(*namelist != NULL)
        {
            P101_TRACK_ALLOC(env, (const void *)*namelist, (size_t)ret_val * sizeof(struct dirent *));
            for(int index = 0; index < ret_val; index++)
            {
                P101_TRACK_ALLOC(env, (*namelist)[index], sizeof(*(*namelist)[index]));
            }
        }
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

/*
 * Copyright 2021-2024 D'Arcy Smith.
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

#include <fnmatch.h>

int p101_fnmatch(const struct p101_env *env, struct p101_error *err, const char *pattern, const char *string, int flags)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN_SYSTEM_CODE(env, err, ret_val);
    errno   = 0;
    ret_val = fnmatch(pattern, string, flags);

    if(ret_val != 0 && ret_val != FNM_NOMATCH)
    {
        P101_ERROR_RAISE_SYSTEM(err, "Invalid filename match pattern", ret_val);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

/*
 * Copyright 2021-2024 D'Arcy Smith.
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

/* cppcheck-suppress funcArgNamesDifferentUnnamed */
int p101_glob(const struct p101_env *env, struct p101_error *err, const char *restrict pattern, int flags, int (*errfunc)(const char *epath, int eerrno), glob_t *restrict pglob)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN_SYSTEM_CODE(env, err, ret_val);
    errno   = 0;
    ret_val = glob(pattern, flags, errfunc, pglob);

    if(ret_val != 0 && ret_val != GLOB_NOMATCH)
    {
        if(ret_val == GLOB_ABORTED)
        {
            P101_ERROR_RAISE_SYSTEM(err, "", ret_val);
        }
        else if(ret_val == GLOB_NOSPACE)
        {
            P101_ERROR_RAISE_SYSTEM(err, "", ret_val);
        }
        else
        {
            P101_ERROR_RAISE_SYSTEM(err, "<unknown glob error>", ret_val);
        }
    }
    else if(ret_val == 0)
    {
        P101_TRACK_POINTER_RESOURCE_ACQUIRE(env, "glob-result", pglob, 0U, NULL);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

void p101_globfree(const struct p101_env *env, glob_t *pglob)
{
    char resource_id[P101_ENV_POINTER_RESOURCE_ID_SIZE];

    P101_TRACE(env);
    p101_env_pointer_resource_id(resource_id, sizeof(resource_id), pglob);
    errno = 0;
    globfree(pglob);
    P101_TRACK_RESOURCE_RELEASE(env, "glob-result", resource_id, NULL);
    P101_TRACE_EXIT(env);
}

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

/*
 * Copyright 2021-2024 D'Arcy Smith.
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

#include <stdlib.h>
#include <unistd.h>

char *p101_mkdtemp(const struct p101_env *env, struct p101_error *err, char *name_template)
{
    char *ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, NULL);
    errno   = 0;
    ret_val = mkdtemp(name_template);

    if(ret_val == NULL)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_mkstemp(const struct p101_env *env, struct p101_error *err, char *name_template)
{
    int p101_single_result_;
    int ret_val;
    int fault;

    P101_TRACE(env);
    fault = p101_env_check_fault(env, __func__);

    if(fault != 0)
    {
        P101_ERROR_RAISE_ERRNO(err, fault);
        P101_TRACE_EXIT(env);

        p101_single_result_ = -1;
        goto p101_single_exit_;
    }

    errno   = 0;
    ret_val = mkstemp(name_template);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }
    else
    {
        P101_TRACK_OPEN(env, ret_val);
    }

    P101_TRACE_EXIT(env);

    p101_single_result_ = ret_val;
    goto p101_single_exit_;

p101_single_exit_:
    return p101_single_result_;
}

/*
 * Copyright 2021-2024 D'Arcy Smith.
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

int p101_chmod(const struct p101_env *env, struct p101_error *err, const char *path, mode_t mode)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = chmod(path, mode);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_fchmod(const struct p101_env *env, struct p101_error *err, int fildes, mode_t mode)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = fchmod(fildes, mode);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_fchmodat(const struct p101_env *env, struct p101_error *err, int fd, const char *path, mode_t mode, int flag)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = fchmodat(fd, path, mode, flag);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_fstat(const struct p101_env *env, struct p101_error *err, int fildes, struct stat *buf)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = fstat(fildes, buf);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_fstatat(const struct p101_env *env, struct p101_error *err, int fd, const char *restrict path, struct stat *restrict buf, int flag)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = fstatat(fd, path, buf, flag);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_futimens(const struct p101_env *env, struct p101_error *err, int fd, const struct timespec times[2])
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = futimens(fd, times);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_lstat(const struct p101_env *env, struct p101_error *err, const char *restrict path, struct stat *restrict buf)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = lstat(path, buf);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_mkdir(const struct p101_env *env, struct p101_error *err, const char *path, mode_t mode)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = mkdir(path, mode);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_mkdirat(const struct p101_env *env, struct p101_error *err, int fd, const char *path, mode_t mode)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = mkdirat(fd, path, mode);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_stat(const struct p101_env *env, struct p101_error *err, const char *restrict path, struct stat *restrict buf)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = stat(path, buf);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

mode_t p101_umask(const struct p101_env *env, mode_t cmask)
{
    mode_t ret_val;

    P101_TRACE(env);
    errno   = 0;
    ret_val = umask(cmask);

    P101_TRACE_EXIT(env);
    return ret_val;
}

int p101_utimensat(const struct p101_env *env, struct p101_error *err, int fd, const char *path, const struct timespec times[2], int flag)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = utimensat(fd, path, times, flag);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

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

int p101_fstatvfs(const struct p101_env *env, struct p101_error *err, int fildes, struct statvfs *buf)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = fstatvfs(fildes, buf);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_statvfs(const struct p101_env *env, struct p101_error *err, const char *restrict path, struct statvfs *restrict buf)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = statvfs(path, buf);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

/*
 * Copyright 2021-2024 D'Arcy Smith.
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

int p101_access(const struct p101_env *env, struct p101_error *err, const char *path, int amode)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = access(path, amode);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_chdir(const struct p101_env *env, struct p101_error *err, const char *path)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = chdir(path);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_chown(const struct p101_env *env, struct p101_error *err, const char *path, uid_t owner, gid_t group)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = chown(path, owner, group);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_faccessat(const struct p101_env *env, struct p101_error *err, int fd, const char *path, int amode, int flag)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = faccessat(fd, path, amode, flag);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_fchdir(const struct p101_env *env, struct p101_error *err, int fildes)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = fchdir(fildes);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_fchown(const struct p101_env *env, struct p101_error *err, int fildes, uid_t owner, gid_t group)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = fchown(fildes, owner, group);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_fchownat(const struct p101_env *env, struct p101_error *err, int fd, const char *path, uid_t owner, gid_t group, int flag)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = fchownat(fd, path, owner, group, flag);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

long p101_fpathconf(const struct p101_env *env, struct p101_error *err, int fildes, int name)
{
    long ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = fpathconf(fildes, name);

    if(ret_val == -1 && errno != 0)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_ftruncate(const struct p101_env *env, struct p101_error *err, int fildes, off_t length)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = ftruncate(fildes, length);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

char *p101_getcwd(const struct p101_env *env, struct p101_error *err, char *buf, size_t size)
{
    char *ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, NULL);
    errno   = 0;
    ret_val = getcwd(buf, size);

    if(ret_val == NULL)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_lchown(const struct p101_env *env, struct p101_error *err, const char *path, uid_t owner, gid_t group)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = lchown(path, owner, group);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_link(const struct p101_env *env, struct p101_error *err, const char *path1, const char *path2)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = link(path1, path2);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_linkat(const struct p101_env *env, struct p101_error *err, int fd1, const char *path1, int fd2, const char *path2, int flag)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = linkat(fd1, path1, fd2, path2, flag);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

long p101_pathconf(const struct p101_env *env, struct p101_error *err, const char *path, int name)
{
    long ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = pathconf(path, name);

    if(ret_val == -1 && errno != 0)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

ssize_t p101_readlink(const struct p101_env *env, struct p101_error *err, const char *restrict path, char *restrict buf, size_t bufsize)
{
    ssize_t ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, (ssize_t)-1);
    errno   = 0;
    ret_val = readlink(path, buf, bufsize);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

ssize_t p101_readlinkat(const struct p101_env *env, struct p101_error *err, int fd, const char *restrict path, char *restrict buf, size_t bufsize)
{
    ssize_t ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, (ssize_t)-1);
    errno   = 0;
    ret_val = readlinkat(fd, path, buf, bufsize);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_rmdir(const struct p101_env *env, struct p101_error *err, const char *path)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = rmdir(path);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_symlink(const struct p101_env *env, struct p101_error *err, const char *path1, const char *path2)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = symlink(path1, path2);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_symlinkat(const struct p101_env *env, struct p101_error *err, const char *path1, int fd, const char *path2)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = symlinkat(path1, fd, path2);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_truncate(const struct p101_env *env, struct p101_error *err, const char *path, off_t length)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = truncate(path, length);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_unlink(const struct p101_env *env, struct p101_error *err, const char *path)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = unlink(path);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_unlinkat(const struct p101_env *env, struct p101_error *err, int fd, const char *path, int flag)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = unlinkat(fd, path, flag);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

void p101_seekdir(const struct p101_env *env, DIR *dirp, long loc)
{
    P101_TRACE(env);
    errno = 0;
    seekdir(dirp, loc);
    P101_TRACE_EXIT(env);
}

long p101_telldir(const struct p101_env *env, struct p101_error *err, DIR *dirp)
{
    long ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1L);
    errno   = 0;
    ret_val = telldir(dirp);

    if(ret_val == -1L && errno != 0)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

struct ftw_callback_context
{
    p101_ftw_fn                  callback;
    int                          stopped;
    struct ftw_callback_context *previous;
};

struct nftw_callback_context
{
    p101_nftw_fn                  callback;
    int                           stopped;
    struct nftw_callback_context *previous;
};

static _Thread_local struct ftw_callback_context  *active_ftw_context;     // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
static _Thread_local struct nftw_callback_context *active_nftw_context;    // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

static int call_ftw_callback(const char *path, const struct stat *status, int type)
{
    struct ftw_callback_context *context;
    int                          result;

    context = active_ftw_context;
    result  = context->callback(path, status, type);
    if(result != 0)
    {
        context->stopped = 1;
    }

    return result;
}

static int call_nftw_callback(const char *path, const struct stat *status, int type, struct FTW *walk)
{
    struct nftw_callback_context *context;
    int                           result;

    context = active_nftw_context;
    result  = context->callback(path, status, type, walk);
    if(result != 0)
    {
        context->stopped = 1;
    }

    return result;
}

/* cppcheck-suppress funcArgNamesDifferentUnnamed */
int p101_ftw(const struct p101_env *env, struct p101_error *err, const char *path, p101_ftw_fn fn, int ndirs)
{
    struct ftw_callback_context context;
    int                         saved_errno;
    int                         ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    context.callback = fn;
    context.stopped  = 0;
    context.previous = active_ftw_context;

    active_ftw_context = &context;
    errno              = 0;
    ret_val            = ftw(path, call_ftw_callback, ndirs);
    saved_errno        = errno;
    active_ftw_context = context.previous;

    if(ret_val == -1 && context.stopped == 0)
    {
        P101_ERROR_RAISE_ERRNO(err, (saved_errno == 0) ? EIO : saved_errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

/* cppcheck-suppress funcArgNamesDifferentUnnamed */
int p101_nftw(const struct p101_env *env, struct p101_error *err, const char *path, p101_nftw_fn fn, int fd_limit, int flags)
{
    struct nftw_callback_context context;
    int                          saved_errno;
    int                          ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    context.callback = fn;
    context.stopped  = 0;
    context.previous = active_nftw_context;

    active_nftw_context = &context;
    errno               = 0;
    ret_val             = nftw(path, call_nftw_callback, fd_limit, flags);
    saved_errno         = errno;
    active_nftw_context = context.previous;

    if(ret_val == -1 && context.stopped == 0)
    {
        P101_ERROR_RAISE_ERRNO(err, (saved_errno == 0) ? EIO : saved_errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

#include <libgen.h>

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

#include <p101_c/p101_string.h>
#include <string.h>

char *p101_realpath(const struct p101_env *env, struct p101_error *err, const char *restrict file_name, char *restrict resolved_name)
{
    char *ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, NULL);
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

    P101_WRAPPER_DONE(env);
    return ret_val;
}

#include <sys/stat.h>

int p101_mknod(const struct p101_env *env, struct p101_error *err, const char *path, mode_t mode, dev_t dev)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = mknod(path, mode, dev);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

#ifdef __linux__
    #include <crypt.h>
#endif

void p101_sync(const struct p101_env *env)
{
    P101_TRACE(env);
    errno = 0;
    sync();
    P101_TRACE_EXIT(env);
}

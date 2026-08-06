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

#include "p101_filesystem/p101_ftw.h"
#include <p101_env/wrapper.h>

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

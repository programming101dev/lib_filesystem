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

#include "p101_filesystem/p101_dirent.h"
#include "p101_filesystem/p101_fnmatch.h"
#include "p101_filesystem/p101_ftw.h"
#include "p101_filesystem/p101_glob.h"
#include "p101_filesystem/p101_libgen.h"
#include "p101_filesystem/p101_stdio.h"
#include "p101_filesystem/p101_stdlib.h"
#include "p101_filesystem/p101_unistd.h"
#include "p101_filesystem/sys/p101_stat.h"
#include "p101_filesystem/sys/p101_statvfs.h"
#include <p101_env/wrapper.h>

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

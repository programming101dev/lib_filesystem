#include <errno.h>
#include <p101_env/env.h>
#include <p101_error/error.h>
#include <p101_filesystem/filesystem.h>
#include <stdio.h>
#include <stdlib.h>

static int failures;

#define EXPECT(condition)                                                                                                                                                                                                                                          \
    do                                                                                                                                                                                                                                                             \
    {                                                                                                                                                                                                                                                              \
        if(!(condition))                                                                                                                                                                                                                                           \
        {                                                                                                                                                                                                                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                                                                                                                                                                   \
            failures++;                                                                                                                                                                                                                                            \
        }                                                                                                                                                                                                                                                          \
    } while(0)

struct fault_state
{
    int checks;
};

static int fail_next_call(const struct p101_env *env, const char *call_name, void *user_data)
{
    struct fault_state *state;

    (void)env;
    (void)call_name;
    state = user_data;
    state->checks++;
    return EIO;
}

/* P101_TEST_CASE(p101_access) */
static void test_p101_access(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_access(env, err, NULL, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_chdir) */
static void test_p101_chdir(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_chdir(env, err, NULL);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_chmod) */
static void test_p101_chmod(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_chmod(env, err, NULL, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_chown) */
static void test_p101_chown(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_chown(env, err, NULL, 0, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_closedir) */
static void test_p101_closedir(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_closedir(env, err, NULL);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_dirfd) */
static void test_p101_dirfd(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_dirfd(env, err, NULL);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_faccessat) */
static void test_p101_faccessat(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_faccessat(env, err, 0, NULL, 0, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_fchdir) */
static void test_p101_fchdir(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_fchdir(env, err, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_fchmod) */
static void test_p101_fchmod(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_fchmod(env, err, 0, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_fchmodat) */
static void test_p101_fchmodat(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_fchmodat(env, err, 0, NULL, 0, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_fchown) */
static void test_p101_fchown(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_fchown(env, err, 0, 0, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_fchownat) */
static void test_p101_fchownat(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_fchownat(env, err, 0, NULL, 0, 0, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_fdopendir) */
static void test_p101_fdopendir(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    DIR *result = p101_fdopendir(env, err, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_fnmatch) */
static void test_p101_fnmatch(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_fnmatch(env, err, NULL, NULL, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_fpathconf) */
static void test_p101_fpathconf(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    long result = p101_fpathconf(env, err, 0, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_fstat) */
static void test_p101_fstat(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_fstat(env, err, 0, NULL);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_fstatat) */
static void test_p101_fstatat(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_fstatat(env, err, 0, NULL, NULL, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_fstatvfs) */
static void test_p101_fstatvfs(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_fstatvfs(env, err, 0, NULL);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_ftruncate) */
static void test_p101_ftruncate(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_ftruncate(env, err, 0, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_ftw) */
static void test_p101_ftw(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_ftw(env, err, NULL, 0, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_futimens) */
static void test_p101_futimens(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_futimens(env, err, 0, NULL);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_getcwd) */
static void test_p101_getcwd(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    char *result = p101_getcwd(env, err, NULL, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_glob) */
static void test_p101_glob(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_glob(env, err, NULL, 0, NULL, NULL);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_lchown) */
static void test_p101_lchown(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_lchown(env, err, NULL, 0, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_link) */
static void test_p101_link(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_link(env, err, NULL, NULL);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_linkat) */
static void test_p101_linkat(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_linkat(env, err, 0, NULL, 0, NULL, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_lstat) */
static void test_p101_lstat(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_lstat(env, err, NULL, NULL);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_mkdir) */
static void test_p101_mkdir(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_mkdir(env, err, NULL, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_mkdirat) */
static void test_p101_mkdirat(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_mkdirat(env, err, 0, NULL, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_mkdtemp) */
static void test_p101_mkdtemp(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    char *result = p101_mkdtemp(env, err, NULL);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_mknod) */
static void test_p101_mknod(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_mknod(env, err, NULL, 0, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_mkstemp) */
static void test_p101_mkstemp(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_mkstemp(env, err, NULL);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_nftw) */
static void test_p101_nftw(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_nftw(env, err, NULL, NULL, 0, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_opendir) */
static void test_p101_opendir(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    DIR *result = p101_opendir(env, err, NULL);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_pathconf) */
static void test_p101_pathconf(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    long result = p101_pathconf(env, err, NULL, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_readdir) */
static void test_p101_readdir(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    struct dirent *result = p101_readdir(env, err, NULL);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_readlink) */
static void test_p101_readlink(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    ssize_t result = p101_readlink(env, err, NULL, NULL, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_readlinkat) */
static void test_p101_readlinkat(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    ssize_t result = p101_readlinkat(env, err, 0, NULL, NULL, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_realpath) */
static void test_p101_realpath(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    char *result = p101_realpath(env, err, NULL, NULL);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_renameat) */
static void test_p101_renameat(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_renameat(env, err, 0, NULL, 0, NULL);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_rmdir) */
static void test_p101_rmdir(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_rmdir(env, err, NULL);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_scandir) */
static void test_p101_scandir(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_scandir(env, err, NULL, NULL, NULL, NULL);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_stat) */
static void test_p101_stat(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_stat(env, err, NULL, NULL);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_statvfs) */
static void test_p101_statvfs(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_statvfs(env, err, NULL, NULL);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_symlink) */
static void test_p101_symlink(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_symlink(env, err, NULL, NULL);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_symlinkat) */
static void test_p101_symlinkat(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_symlinkat(env, err, NULL, 0, NULL);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_telldir) */
static void test_p101_telldir(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    long result = p101_telldir(env, err, NULL);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_truncate) */
static void test_p101_truncate(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_truncate(env, err, NULL, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_unlink) */
static void test_p101_unlink(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_unlink(env, err, NULL);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_unlinkat) */
static void test_p101_unlinkat(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_unlinkat(env, err, 0, NULL, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_utimensat) */
static void test_p101_utimensat(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_utimensat(env, err, 0, NULL, NULL, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

int main(void)
{
    struct p101_error *err;
    struct p101_env   *env;

    err = p101_error_create(false);
    if(err == NULL)
    {
        return EXIT_FAILURE;
    }
    env = p101_env_create(err, NULL);
    if(env == NULL)
    {
        p101_error_destroy(err);
        return EXIT_FAILURE;
    }
    test_p101_access(env, err);
    test_p101_chdir(env, err);
    test_p101_chmod(env, err);
    test_p101_chown(env, err);
    test_p101_closedir(env, err);
    test_p101_dirfd(env, err);
    test_p101_faccessat(env, err);
    test_p101_fchdir(env, err);
    test_p101_fchmod(env, err);
    test_p101_fchmodat(env, err);
    test_p101_fchown(env, err);
    test_p101_fchownat(env, err);
    test_p101_fdopendir(env, err);
    test_p101_fnmatch(env, err);
    test_p101_fpathconf(env, err);
    test_p101_fstat(env, err);
    test_p101_fstatat(env, err);
    test_p101_fstatvfs(env, err);
    test_p101_ftruncate(env, err);
    test_p101_ftw(env, err);
    test_p101_futimens(env, err);
    test_p101_getcwd(env, err);
    test_p101_glob(env, err);
    test_p101_lchown(env, err);
    test_p101_link(env, err);
    test_p101_linkat(env, err);
    test_p101_lstat(env, err);
    test_p101_mkdir(env, err);
    test_p101_mkdirat(env, err);
    test_p101_mkdtemp(env, err);
    test_p101_mknod(env, err);
    test_p101_mkstemp(env, err);
    test_p101_nftw(env, err);
    test_p101_opendir(env, err);
    test_p101_pathconf(env, err);
    test_p101_readdir(env, err);
    test_p101_readlink(env, err);
    test_p101_readlinkat(env, err);
    test_p101_realpath(env, err);
    test_p101_renameat(env, err);
    test_p101_rmdir(env, err);
    test_p101_scandir(env, err);
    test_p101_stat(env, err);
    test_p101_statvfs(env, err);
    test_p101_symlink(env, err);
    test_p101_symlinkat(env, err);
    test_p101_telldir(env, err);
    test_p101_truncate(env, err);
    test_p101_unlink(env, err);
    test_p101_unlinkat(env, err);
    test_p101_utimensat(env, err);
    p101_env_destroy(env);
    p101_error_destroy(err);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

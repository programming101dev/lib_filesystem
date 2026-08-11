#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <p101_env/env.h>
#include <p101_error/error.h>
#include <p101_filesystem/p101_dirent.h>
#include <p101_filesystem/p101_fnmatch.h>
#include <p101_filesystem/p101_ftw.h>
#include <p101_filesystem/p101_glob.h>
#include <p101_filesystem/p101_libgen.h>
#include <p101_filesystem/p101_stdio.h>
#include <p101_filesystem/p101_stdlib.h>
#include <p101_filesystem/p101_unistd.h>
#include <p101_filesystem/sys/p101_stat.h>
#include <p101_filesystem/sys/p101_statvfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

static int count_ftw_entry(const char *path, const struct stat *status, int type)
{
    (void)path;
    (void)status;
    (void)type;
    return 0;
}

static int count_nftw_entry(const char *path, const struct stat *status, int type, struct FTW *information)
{
    (void)path;
    (void)status;
    (void)type;
    (void)information;
    return 0;
}

static int fail_only_nested_dirfd(const struct p101_env *env, const char *call_name, void *user_data)
{
    int comparison;
    int result;

    (void)env;
    (void)user_data;
    comparison = strcmp(call_name, "p101_dirfd");
    result     = comparison == 0 ? EIO : 0;
    return result;
}

static void expect_invalid_argument(struct p101_error *err, int result)
{
    bool is_invalid;

    is_invalid = p101_error_is_errno(err, EINVAL);
    EXPECT(result == -1);
    EXPECT(is_invalid);
    p101_error_reset(err);
}

static void test_walk_argument_validation(const struct p101_env *env, struct p101_error *err)
{
    int result;

    result = p101_ftw(env, err, NULL, count_ftw_entry, 1);
    expect_invalid_argument(err, result);
    result = p101_ftw(env, err, ".", NULL, 1);
    expect_invalid_argument(err, result);
    result = p101_ftw(env, err, ".", count_ftw_entry, 0);
    expect_invalid_argument(err, result);

    result = p101_nftw(env, err, NULL, count_nftw_entry, 1, 0);
    expect_invalid_argument(err, result);
    result = p101_nftw(env, err, ".", NULL, 1, 0);
    expect_invalid_argument(err, result);
    result = p101_nftw(env, err, ".", count_nftw_entry, 0, 0);
    expect_invalid_argument(err, result);
}

static void test_path_helpers(const struct p101_env *env, struct p101_error *err)
{
    struct dirent        first           = {0};
    struct dirent        second          = {0};
    const struct dirent *first_pointer   = &first;
    const struct dirent *second_pointer  = &second;
    char                 basename_path[] = "/tmp/p101-name";
    char                 dirname_path[]  = "/tmp/p101-name";

    (void)strcpy(first.d_name, "alpha");
    (void)strcpy(second.d_name, "beta");
    /* P101_TEST_CASE(p101_alphasort) */
    EXPECT(p101_alphasort(env, &first_pointer, &second_pointer) < 0);

    /* P101_TEST_CASE(p101_basename) */
    EXPECT(strcmp(p101_basename(env, err, basename_path), "p101-name") == 0);
    /* P101_TEST_CASE(p101_dirname) */
    EXPECT(strcmp(p101_dirname(env, err, dirname_path), "/tmp") == 0);
}

static void test_directory_helpers(const struct p101_env *env)
{
    DIR           *directory;
    struct dirent *entry;

    directory = opendir(".");
    EXPECT(directory != NULL);
    if(directory == NULL)
    {
        return;
    }
    entry = readdir(directory);
    EXPECT(entry != NULL);

    /* P101_TEST_CASE(p101_rewinddir) */
    p101_rewinddir(env, directory);
    EXPECT(readdir(directory) != NULL);

    /* P101_TEST_CASE(p101_seekdir) */
    p101_seekdir(env, directory, 0L);
    EXPECT(readdir(directory) != NULL);
    EXPECT(closedir(directory) == 0);
}

static void test_closedir_does_not_inject_bookkeeping(struct p101_env *env, struct p101_error *err)
{
    DIR *directory;
    int  close_status;
    bool has_error;

    p101_env_set_fault_injector(env, fail_only_nested_dirfd, NULL);
    directory = p101_opendir(env, err, ".");
    EXPECT(directory != NULL);
    if(directory != NULL)
    {
        close_status = p101_closedir(env, err, directory);
        has_error    = p101_error_has_error(err);
        EXPECT(close_status == 0);
        EXPECT(!has_error);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
}

static void test_glob_and_process_helpers(const struct p101_env *env)
{
    glob_t matches = {0};
    mode_t original;
    int    result;

    result = glob("/p101/no/such/path/*", GLOB_NOCHECK, NULL, &matches);
    EXPECT(result == 0);
    if(result == 0)
    {
        /* P101_TEST_CASE(p101_globfree) */
        p101_globfree(env, &matches);
    }

    original = umask(0);
    /* P101_TEST_CASE(p101_umask) */
    EXPECT(p101_umask(env, original) == 0);

    /* P101_TEST_CASE(p101_sync) */
    p101_sync(env);
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
    test_path_helpers(env, err);
    test_directory_helpers(env);
    test_closedir_does_not_inject_bookkeeping(env, err);
    test_glob_and_process_helpers(env);
    test_walk_argument_validation(env, err);
    p101_env_destroy(env);
    p101_error_destroy(err);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

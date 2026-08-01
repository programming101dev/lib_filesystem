#include <dirent.h>
#include <fcntl.h>
#include <p101_env/env.h>
#include <p101_error/error.h>
#include <p101_filesystem/filesystem.h>
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
    test_glob_and_process_helpers(env);
    p101_env_destroy(env);
    p101_error_destroy(err);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fmtmsg.h>
#include <fnmatch.h>
#include <ftw.h>
#include <limits.h>
#include <math.h>
#include <netinet/in.h>
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
#include <pthread.h>
#include <search.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utmpx.h>

static int    failures;
static size_t fault_resource_events;
static FILE  *outcome_stream;
static bool   native_child_process;
static int    native_child_status = EXIT_SUCCESS;

static int native_dirent_compare(const struct dirent **left, const struct dirent **right)
{
    (void)left;
    (void)right;
    return 0;
}

static int native_dirent_filter(const struct dirent *entry)
{
    (void)entry;
    return 1;
}

static int native_path_error_callback(const char *path, int error_code)
{
    (void)path;
    (void)error_code;
    return 0;
}

#define P101_TEST_ERRNO_SENTINEL 0x5A5A

#ifdef __linux__
    #define P101_TEST_PLATFORM "linux"
#elif defined(__APPLE__)
    #define P101_TEST_PLATFORM "macos"
#elif defined(__FreeBSD__)
    #define P101_TEST_PLATFORM "freebsd"
#else
    #define P101_TEST_PLATFORM "posix"
#endif

#define EXPECT(condition)                                                                                                                                                                                                                                          \
    do                                                                                                                                                                                                                                                             \
    {                                                                                                                                                                                                                                                              \
        if(!(condition))                                                                                                                                                                                                                                           \
        {                                                                                                                                                                                                                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                                                                                                                                                                   \
            failures++;                                                                                                                                                                                                                                            \
        }                                                                                                                                                                                                                                                          \
    } while(0)

#define P101_NATIVE_CLEANUP_ERRNO(expression)                                                                                                                                                                                                                      \
    do                                                                                                                                                                                                                                                             \
    {                                                                                                                                                                                                                                                              \
        if((expression) != 0)                                                                                                                                                                                                                                      \
        {                                                                                                                                                                                                                                                          \
            fprintf(stderr, "native cleanup failed: %s: %s\n", #expression, strerror(errno));                                                                                                                                                                      \
            native_passed = false;                                                                                                                                                                                                                                 \
        }                                                                                                                                                                                                                                                          \
    } while(0)

#define P101_NATIVE_CLEANUP_STATUS(expression)                                                                                                                                                                                                                     \
    do                                                                                                                                                                                                                                                             \
    {                                                                                                                                                                                                                                                              \
        int p101_cleanup_status_ = (expression);                                                                                                                                                                                                                   \
        if(p101_cleanup_status_ != 0)                                                                                                                                                                                                                              \
        {                                                                                                                                                                                                                                                          \
            fprintf(stderr, "native cleanup failed: %s: status %d\n", #expression, p101_cleanup_status_);                                                                                                                                                          \
            native_passed = false;                                                                                                                                                                                                                                 \
        }                                                                                                                                                                                                                                                          \
    } while(0)

#define P101_NATIVE_CLEANUP_UNLINK_IF_PRESENT(path)                                                                                                                                                                                                                \
    do                                                                                                                                                                                                                                                             \
    {                                                                                                                                                                                                                                                              \
        bool p101_cleanup_ok_;                                                                                                                                                                                                                                     \
                                                                                                                                                                                                                                                                   \
        p101_cleanup_ok_ = native_unlink_if_present(path);                                                                                                                                                                                                         \
        if(!p101_cleanup_ok_)                                                                                                                                                                                                                                      \
        {                                                                                                                                                                                                                                                          \
            native_passed = false;                                                                                                                                                                                                                                 \
        }                                                                                                                                                                                                                                                          \
    } while(0)

#define P101_NATIVE_FORMAT_PID_PATH_OR_SKIP(buffer, format)                                                                                                                                                                                                        \
    do                                                                                                                                                                                                                                                             \
    {                                                                                                                                                                                                                                                              \
        bool p101_format_ok_;                                                                                                                                                                                                                                      \
                                                                                                                                                                                                                                                                   \
        p101_format_ok_ = native_format_pid_path((buffer), sizeof(buffer), (format));                                                                                                                                                                              \
        if(!p101_format_ok_)                                                                                                                                                                                                                                       \
        {                                                                                                                                                                                                                                                          \
            fprintf(stderr, "native setup failed: path formatting\n");                                                                                                                                                                                             \
            native_child_status = 77;                                                                                                                                                                                                                              \
            goto native_child_done_;                                                                                                                                                                                                                               \
        }                                                                                                                                                                                                                                                          \
    } while(0)

struct fault_state
{
    int checks;
    int code;
};

static pid_t native_waitpid_nointr(pid_t pid, int *status) P101_ATTR_SEMANTIC_ROLE("p101:test:eintr-safe-wait-adapter")
{
    pid_t result;

    do
    {
        result = waitpid(pid, status, 0);
    } while(result < 0 && errno == EINTR);
    return result;
}

static void write_outcome(const char *wrapper, const char *domain, const char *symbol, int code, int passed)
{
    int written;

    if(outcome_stream != NULL)
    {
        written = fprintf(outcome_stream, "P101WRAPPER\t1\tFAULT\t%s\tlib_filesystem\t%s\t%s\t%s\t%d\t%s\n", P101_TEST_PLATFORM, wrapper, domain, symbol, code, passed ? "PASS" : "FAIL");
        if(written < 0 || fflush(outcome_stream) != 0)
        {
            fprintf(stderr, "FAIL: cannot write wrapper outcome receipt\n");
            failures++;
        }
    }
}

static int fail_next_call(const struct p101_env *env, const char *call_name, void *user_data)
{
    struct fault_state *state;

    (void)env;
    (void)call_name;
    state = user_data;
    state->checks++;
    return state->code;
}

static void count_fd_event(const struct p101_env *env, p101_env_fd_event event, int fd, const char *file_name, const char *function_name, int line_number, void *user_data)
{
    (void)env;
    (void)event;
    (void)fd;
    (void)file_name;
    (void)function_name;
    (void)line_number;
    (void)user_data;
    fault_resource_events++;
}

static void count_alloc_event(const struct p101_env *env, p101_env_alloc_event event, const void *ptr, const void *new_ptr, size_t size, const char *file_name, const char *function_name, int line_number, void *user_data)
{
    (void)env;
    (void)event;
    (void)ptr;
    (void)new_ptr;
    (void)size;
    (void)file_name;
    (void)function_name;
    (void)line_number;
    (void)user_data;
    fault_resource_events++;
}

static void count_resource_event(const struct p101_env *env, p101_env_resource_kind event, const char *resource_class, const char *resource_id, const char *related_id, size_t size, const char *metadata, const char *file_name, const char *function_name,
                                 int line_number, void *user_data)
{
    (void)env;
    (void)event;
    (void)resource_class;
    (void)resource_id;
    (void)related_id;
    (void)size;
    (void)metadata;
    (void)file_name;
    (void)function_name;
    (void)line_number;
    (void)user_data;
    fault_resource_events++;
}

/* P101_TEST_CASE(p101_access) */
static void test_p101_access(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EINVAL, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EROFS, ETXTBSY};
    static const char *const error_names[] = {"EACCES", "EINVAL", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EROFS", "ETXTBSY"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EFAULT, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EROFS, ETXTBSY};
    static const char *const error_names[] = {"EACCES", "EFAULT", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EROFS", "ETXTBSY"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EFAULT, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EROFS, ETXTBSY};
    static const char *const error_names[] = {"EACCES", "EFAULT", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EROFS", "ETXTBSY"};
#else
    static const int         errors[]      = {EACCES, EINVAL, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EROFS, ETXTBSY};
    static const char *const error_names[] = {"EACCES", "EINVAL", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EROFS", "ETXTBSY"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_access(env, err, NULL, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_access", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_access(native_env, native_err, "p101", 0);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_access: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_access: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_access: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_basename) */
static void test_p101_basename(struct p101_env *env, struct p101_error *err)
{
    char          argument_2[4];
    unsigned char argument_2_before[sizeof(argument_2)];
    memset(argument_2, 0xA5, sizeof(argument_2));
    memcpy(argument_2_before, argument_2, sizeof(argument_2));
#ifdef __linux__
    static const int         errors[]      = {EIO};
    static const char *const error_names[] = {"EIO"};
#elif defined(__APPLE__)
    static const int         errors[]      = {ENAMETOOLONG};
    static const char *const error_names[] = {"ENAMETOOLONG"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EIO};
    static const char *const error_names[] = {"EIO"};
#else
    static const int         errors[]      = {EIO};
    static const char *const error_names[] = {"EIO"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        char *result = p101_basename(env, err, argument_2);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (NULL));
        EXPECT(memcmp(argument_2, argument_2_before, sizeof(argument_2)) == 0);
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_basename", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            char  native_argument_2[PATH_MAX] = {0};
            char *native_result               = p101_basename(native_env, native_err, native_argument_2);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_basename: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_basename: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_basename: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_chdir) */
static void test_p101_chdir(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EFAULT, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOMEM, ENOTDIR};
    static const char *const error_names[] = {"EACCES", "EFAULT", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOMEM", "ENOTDIR"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EFAULT, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR};
    static const char *const error_names[] = {"EACCES", "EFAULT", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EFAULT, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR};
    static const char *const error_names[] = {"EACCES", "EFAULT", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR"};
#else
    static const int         errors[]      = {EACCES, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR};
    static const char *const error_names[] = {"EACCES", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_chdir(env, err, NULL);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_chdir", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_chdir(native_env, native_err, "p101");
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_chdir: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_chdir: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_chdir: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_chmod) */
static void test_p101_chmod(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EFAULT, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOMEM, ENOTDIR, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EFAULT", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOMEM", "ENOTDIR", "EPERM", "EROFS"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EBADF, EFAULT, EINTR, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EFAULT", "EINTR", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EPERM", "EROFS"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EBADF, EFAULT, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EFAULT", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EPERM", "EROFS"};
#else
    static const int         errors[]      = {EACCES, EINTR, EINVAL, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EINTR", "EINVAL", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EPERM", "EROFS"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_chmod(env, err, NULL, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_chmod", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_chmod(native_env, native_err, "p101", 0);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_chmod: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_chmod: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_chmod: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_chown) */
static void test_p101_chown(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EFAULT, ELOOP, ENAMETOOLONG, ENOENT, ENOMEM, ENOTDIR, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EFAULT", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOMEM", "ENOTDIR", "EPERM", "EROFS"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EBADF, EFAULT, EINTR, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EFAULT", "EINTR", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EPERM", "EROFS"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EBADF, EFAULT, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EFAULT", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EPERM", "EROFS"};
#else
    static const int         errors[]      = {EACCES, EINTR, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EINTR", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EPERM", "EROFS"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_chown(env, err, NULL, 0, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_chown", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_chown(native_env, native_err, "p101", 0, 0);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_chown: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_chown: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_chown: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_closedir) */
static void test_p101_closedir(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EBADF};
    static const char *const error_names[] = {"EBADF"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EBADF, EINTR, EIO};
    static const char *const error_names[] = {"EBADF", "EINTR", "EIO"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EBADF, EINTR, ENOSPC};
    static const char *const error_names[] = {"EBADF", "EINTR", "ENOSPC"};
#else
    static const int         errors[]      = {EBADF, EINTR};
    static const char *const error_names[] = {"EBADF", "EINTR"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_closedir(env, err, NULL);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_closedir", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            DIR *native_argument_2 = opendir(".");
            if(native_argument_2 == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_closedir(native_env, native_err, native_argument_2);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_closedir: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_closedir: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_closedir: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_dirfd) */
static void test_p101_dirfd(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EINVAL, ENOTSUP};
    static const char *const error_names[] = {"EINVAL", "ENOTSUP"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EINVAL, EMFILE, ENFILE};
    static const char *const error_names[] = {"EINVAL", "EMFILE", "ENFILE"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EINVAL};
    static const char *const error_names[] = {"EINVAL"};
#else
    static const int         errors[]      = {EINVAL, EMFILE, ENFILE};
    static const char *const error_names[] = {"EINVAL", "EMFILE", "ENFILE"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_dirfd(env, err, NULL);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_dirfd", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            DIR *native_argument_2 = opendir(".");
            if(native_argument_2 == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_dirfd(native_env, native_err, native_argument_2);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_dirfd: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            P101_NATIVE_CLEANUP_ERRNO(closedir(native_argument_2));
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_dirfd: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_dirfd: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_dirname) */
static void test_p101_dirname(struct p101_env *env, struct p101_error *err)
{
    char          argument_2[4];
    unsigned char argument_2_before[sizeof(argument_2)];
    memset(argument_2, 0xA5, sizeof(argument_2));
    memcpy(argument_2_before, argument_2, sizeof(argument_2));
#ifdef __linux__
    static const int         errors[]      = {EIO};
    static const char *const error_names[] = {"EIO"};
#elif defined(__APPLE__)
    static const int         errors[]      = {ENAMETOOLONG, ENOMEM};
    static const char *const error_names[] = {"ENAMETOOLONG", "ENOMEM"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EIO};
    static const char *const error_names[] = {"EIO"};
#else
    static const int         errors[]      = {EIO};
    static const char *const error_names[] = {"EIO"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        char *result = p101_dirname(env, err, argument_2);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (NULL));
        EXPECT(memcmp(argument_2, argument_2_before, sizeof(argument_2)) == 0);
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_dirname", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            char  native_argument_2[PATH_MAX] = {0};
            char *native_result               = p101_dirname(native_env, native_err, native_argument_2);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_dirname: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_dirname: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_dirname: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_faccessat) */
static void test_p101_faccessat(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EBADF, EFAULT, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOMEM, ENOTDIR, EPERM, EROFS, ETXTBSY};
    static const char *const error_names[] = {"EACCES", "EBADF", "EFAULT", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOMEM", "ENOTDIR", "EPERM", "EROFS", "ETXTBSY"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EBADF, EFAULT, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EROFS, ETXTBSY};
    static const char *const error_names[] = {"EACCES", "EBADF", "EFAULT", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EROFS", "ETXTBSY"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EBADF, EFAULT, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EROFS, ETXTBSY};
    static const char *const error_names[] = {"EACCES", "EBADF", "EFAULT", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EROFS", "ETXTBSY"};
#else
    static const int         errors[]      = {EACCES, EBADF, EINVAL, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EROFS, ETXTBSY};
    static const char *const error_names[] = {"EACCES", "EBADF", "EINVAL", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EROFS", "ETXTBSY"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_faccessat(env, err, 0, NULL, 0, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_faccessat", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_faccessat(native_env, native_err, 0, "p101", 0, 0);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_faccessat: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_faccessat: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_faccessat: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_fchdir) */
static void test_p101_fchdir(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EBADF, ENOTDIR};
    static const char *const error_names[] = {"EACCES", "EBADF", "ENOTDIR"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EBADF, EINTR, EIO, ENOTDIR};
    static const char *const error_names[] = {"EACCES", "EBADF", "EINTR", "EIO", "ENOTDIR"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EBADF, ENOTDIR};
    static const char *const error_names[] = {"EACCES", "EBADF", "ENOTDIR"};
#else
    static const int         errors[]      = {EACCES, EBADF, EINTR, EIO, ENOTDIR};
    static const char *const error_names[] = {"EACCES", "EBADF", "EINTR", "EIO", "ENOTDIR"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_fchdir(env, err, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_fchdir", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_fchdir(native_env, native_err, 0);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_fchdir: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_fchdir: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_fchdir: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_fchmod) */
static void test_p101_fchmod(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EBADF, EINTR, EINVAL, EPERM, EROFS};
    static const char *const error_names[] = {"EBADF", "EINTR", "EINVAL", "EPERM", "EROFS"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EBADF, EINTR, EINVAL, EIO, EPERM, EROFS};
    static const char *const error_names[] = {"EBADF", "EINTR", "EINVAL", "EIO", "EPERM", "EROFS"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EBADF, EINVAL, EIO, EROFS};
    static const char *const error_names[] = {"EBADF", "EINVAL", "EIO", "EROFS"};
#else
    static const int         errors[]      = {EBADF, EINTR, EINVAL, EPERM, EROFS};
    static const char *const error_names[] = {"EBADF", "EINTR", "EINVAL", "EPERM", "EROFS"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_fchmod(env, err, 0, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_fchmod", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_fchmod(native_env, native_err, 0, 0);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_fchmod: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_fchmod: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_fchmod: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_fchmodat) */
static void test_p101_fchmodat(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EBADF, EINTR, EINVAL, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EOPNOTSUPP, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EINTR", "EINVAL", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EOPNOTSUPP", "EPERM", "EROFS"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EBADF, EINVAL, ENOTDIR};
    static const char *const error_names[] = {"EBADF", "EINVAL", "ENOTDIR"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EBADF, EINVAL, ENOTDIR};
    static const char *const error_names[] = {"EBADF", "EINVAL", "ENOTDIR"};
#else
    static const int         errors[]      = {EACCES, EBADF, EINTR, EINVAL, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EOPNOTSUPP, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EINTR", "EINVAL", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EOPNOTSUPP", "EPERM", "EROFS"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_fchmodat(env, err, 0, NULL, 0, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_fchmodat", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_fchmodat(native_env, native_err, 0, "p101", 0, 0);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_fchmodat: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_fchmodat: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_fchmodat: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_fchown) */
static void test_p101_fchown(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EBADF, EINTR, EINVAL, EIO, EPERM, EROFS};
    static const char *const error_names[] = {"EBADF", "EINTR", "EINVAL", "EIO", "EPERM", "EROFS"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EBADF, EINTR, EINVAL, EIO, EPERM, EROFS};
    static const char *const error_names[] = {"EBADF", "EINTR", "EINVAL", "EIO", "EPERM", "EROFS"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EBADF, EINVAL, EIO, EPERM, EROFS};
    static const char *const error_names[] = {"EBADF", "EINVAL", "EIO", "EPERM", "EROFS"};
#else
    static const int         errors[]      = {EBADF, EINTR, EINVAL, EIO, EPERM, EROFS};
    static const char *const error_names[] = {"EBADF", "EINTR", "EINVAL", "EIO", "EPERM", "EROFS"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_fchown(env, err, 0, 0, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_fchown", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_fchown(native_env, native_err, 0, 0, 0);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_fchown: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_fchown: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_fchown: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_fchownat) */
static void test_p101_fchownat(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EBADF, EINTR, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EINTR", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EPERM", "EROFS"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EBADF, EINTR, EINVAL, EIO, ENOTDIR, EPERM, EROFS};
    static const char *const error_names[] = {"EBADF", "EINTR", "EINVAL", "EIO", "ENOTDIR", "EPERM", "EROFS"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EBADF, EINVAL, ENOTDIR};
    static const char *const error_names[] = {"EBADF", "EINVAL", "ENOTDIR"};
#else
    static const int         errors[]      = {EACCES, EBADF, EINTR, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EINTR", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EPERM", "EROFS"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_fchownat(env, err, 0, NULL, 0, 0, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_fchownat", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_fchownat(native_env, native_err, 0, "p101", 0, 0, 0);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_fchownat: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_fchownat: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_fchownat: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_fdopendir) */
static void test_p101_fdopendir(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EBADF, EMFILE, ENAMETOOLONG, ENFILE, ENOENT, ENOMEM, ENOTDIR};
    static const char *const error_names[] = {"EACCES", "EBADF", "EMFILE", "ENAMETOOLONG", "ENFILE", "ENOENT", "ENOMEM", "ENOTDIR"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EBADF, ENOTDIR};
    static const char *const error_names[] = {"EBADF", "ENOTDIR"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EBADF, ENOTDIR};
    static const char *const error_names[] = {"EBADF", "ENOTDIR"};
#else
    static const int         errors[]      = {EBADF, ENOTDIR};
    static const char *const error_names[] = {"EBADF", "ENOTDIR"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        DIR *result = p101_fdopendir(env, err, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (NULL));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_fdopendir", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            DIR *native_result = p101_fdopendir(native_env, native_err, 0);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_fdopendir: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_fdopendir: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_fdopendir: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_fnmatch) */
static void test_p101_fnmatch(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EINVAL};
    static const char *const error_names[] = {"EINVAL"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EINVAL};
    static const char *const error_names[] = {"EINVAL"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EINVAL};
    static const char *const error_names[] = {"EINVAL"};
#else
    static const int         errors[]      = {EINVAL};
    static const char *const error_names[] = {"EINVAL"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_fnmatch(env, err, NULL, NULL, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_error(err, P101_ERROR_SYSTEM, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == state.code);
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_fnmatch", "system", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_fnmatch(native_env, native_err, "p101", "p101", 0);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_fnmatch: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_fnmatch: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_fnmatch: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_fpathconf) */
static void test_p101_fpathconf(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EBADF, EINVAL};
    static const char *const error_names[] = {"EBADF", "EINVAL"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EBADF, EINVAL, EIO};
    static const char *const error_names[] = {"EBADF", "EINVAL", "EIO"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EBADF, EINVAL, EIO};
    static const char *const error_names[] = {"EBADF", "EINVAL", "EIO"};
#else
    static const int         errors[]      = {EBADF, EINVAL, EOVERFLOW};
    static const char *const error_names[] = {"EBADF", "EINVAL", "EOVERFLOW"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        long result = p101_fpathconf(env, err, 0, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_fpathconf", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            long native_result = p101_fpathconf(native_env, native_err, 0, 0);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_fpathconf: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_fpathconf: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_fpathconf: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_fstat) */
static void test_p101_fstat(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EBADF, EIO, EOVERFLOW};
    static const char *const error_names[] = {"EBADF", "EIO", "EOVERFLOW"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EBADF, EFAULT, EIO, EOVERFLOW};
    static const char *const error_names[] = {"EBADF", "EFAULT", "EIO", "EOVERFLOW"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EBADF, EFAULT, EIO, EOVERFLOW};
    static const char *const error_names[] = {"EBADF", "EFAULT", "EIO", "EOVERFLOW"};
#else
    static const int         errors[]      = {EBADF, EIO, EOVERFLOW};
    static const char *const error_names[] = {"EBADF", "EIO", "EOVERFLOW"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_fstat(env, err, 0, NULL);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_fstat", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            struct stat native_argument_3 = {0};
            int         native_result     = p101_fstat(native_env, native_err, 0, &native_argument_3);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_fstat: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_fstat: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_fstat: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_fstatat) */
static void test_p101_fstatat(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EBADF, EFAULT, EINVAL, ELOOP, ENAMETOOLONG, ENOENT, ENOMEM, ENOTDIR};
    static const char *const error_names[] = {"EACCES", "EBADF", "EFAULT", "EINVAL", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOMEM", "ENOTDIR"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EBADF, EINVAL, ENOTDIR};
    static const char *const error_names[] = {"EBADF", "EINVAL", "ENOTDIR"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EBADF, EINVAL, ENOTDIR};
    static const char *const error_names[] = {"EBADF", "EINVAL", "ENOTDIR"};
#else
    static const int         errors[]      = {EACCES, EBADF, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EOVERFLOW};
    static const char *const error_names[] = {"EACCES", "EBADF", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EOVERFLOW"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_fstatat(env, err, 0, NULL, NULL, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_fstatat", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            struct stat native_argument_4 = {0};
            int         native_result     = p101_fstatat(native_env, native_err, 0, "p101", &native_argument_4, 0);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_fstatat: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_fstatat: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_fstatat: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_fstatvfs) */
static void test_p101_fstatvfs(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EBADF, EFAULT, EINTR, EIO, ENOMEM, ENOSYS, EOVERFLOW};
    static const char *const error_names[] = {"EBADF", "EFAULT", "EINTR", "EIO", "ENOMEM", "ENOSYS", "EOVERFLOW"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EBADF, EFAULT, EIO};
    static const char *const error_names[] = {"EBADF", "EFAULT", "EIO"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EOVERFLOW};
    static const char *const error_names[] = {"EOVERFLOW"};
#else
    static const int         errors[]      = {EBADF, EINTR, EIO, EOVERFLOW};
    static const char *const error_names[] = {"EBADF", "EINTR", "EIO", "EOVERFLOW"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_fstatvfs(env, err, 0, NULL);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_fstatvfs", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            struct statvfs native_argument_3 = {0};
            int            native_result     = p101_fstatvfs(native_env, native_err, 0, &native_argument_3);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_fstatvfs: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_fstatvfs: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_fstatvfs: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_ftruncate) */
static void test_p101_ftruncate(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EBADF, EINVAL};
    static const char *const error_names[] = {"EBADF", "EINVAL"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EBADF, EDEADLK, EFBIG, EINTR, EINVAL, EIO, EPERM, EROFS};
    static const char *const error_names[] = {"EBADF", "EDEADLK", "EFBIG", "EINTR", "EINVAL", "EIO", "EPERM", "EROFS"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EBADF, EINVAL};
    static const char *const error_names[] = {"EBADF", "EINVAL"};
#else
    static const int         errors[]      = {EBADF, EFBIG, EINTR, EINVAL, EIO};
    static const char *const error_names[] = {"EBADF", "EFBIG", "EINTR", "EINVAL", "EIO"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_ftruncate(env, err, 0, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_ftruncate", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_ftruncate(native_env, native_err, 0, 0);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_ftruncate: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_ftruncate: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_ftruncate: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_ftw) */
static void test_p101_ftw(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EIO};
    static const char *const error_names[] = {"EIO"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EAGAIN, EBADF, EDEADLK, EDQUOT, EEXIST, EFAULT, EILSEQ, EINTR, EINVAL, EIO, EISDIR, ELOOP, EMFILE, ENAMETOOLONG, ENFILE, ENOENT, ENOSPC, ENOTDIR, ENXIO, EOPNOTSUPP, EOVERFLOW, EROFS, ETXTBSY, EWOULDBLOCK};
    static const char *const error_names[] = {"EACCES", "EAGAIN",       "EBADF",  "EDEADLK", "EDQUOT", "EEXIST",  "EFAULT", "EILSEQ",     "EINTR",     "EINVAL", "EIO",     "EISDIR",     "ELOOP",
                                              "EMFILE", "ENAMETOOLONG", "ENFILE", "ENOENT",  "ENOSPC", "ENOTDIR", "ENXIO",  "EOPNOTSUPP", "EOVERFLOW", "EROFS",  "ETXTBSY", "EWOULDBLOCK"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EBADF, EDQUOT, EEXIST, EFAULT, EINTR, EINVAL, EIO, EISDIR, ELOOP, EMFILE, EMLINK, ENAMETOOLONG, ENFILE, ENOENT, ENOSPC, ENOTDIR, ENXIO, EOPNOTSUPP, EOVERFLOW, EPERM, EROFS, ETXTBSY, EWOULDBLOCK};
    static const char *const error_names[] = {"EACCES",       "EBADF",  "EDQUOT", "EEXIST", "EFAULT",  "EINTR", "EINVAL",     "EIO",       "EISDIR", "ELOOP", "EMFILE",  "EMLINK",
                                              "ENAMETOOLONG", "ENFILE", "ENOENT", "ENOSPC", "ENOTDIR", "ENXIO", "EOPNOTSUPP", "EOVERFLOW", "EPERM",  "EROFS", "ETXTBSY", "EWOULDBLOCK"};
#else
    static const int         errors[]      = {EIO};
    static const char *const error_names[] = {"EIO"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_ftw(env, err, NULL, 0, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_ftw", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_ftw(native_env, native_err, "p101", 0, 0);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_ftw: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_ftw: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_ftw: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_futimens) */
static void test_p101_futimens(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EBADF, EFAULT, EINVAL, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EFAULT", "EINVAL", "EPERM", "EROFS"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EBADF, EINVAL, EIO, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EINVAL", "EIO", "EPERM", "EROFS"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EBADF, EFAULT, EINVAL, EIO, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EFAULT", "EINVAL", "EIO", "EPERM", "EROFS"};
#else
    static const int         errors[]      = {EACCES, EBADF, EINVAL, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EINVAL", "EPERM", "EROFS"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_futimens(env, err, 0, NULL);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_futimens", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            struct timespec native_argument_3 = {0};
            int             native_result     = p101_futimens(native_env, native_err, 0, &native_argument_3);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_futimens: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_futimens: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_futimens: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_getcwd) */
static void test_p101_getcwd(struct p101_env *env, struct p101_error *err)
{
    char          argument_2[4];
    unsigned char argument_2_before[sizeof(argument_2)];
    memset(argument_2, 0xA5, sizeof(argument_2));
    memcpy(argument_2_before, argument_2, sizeof(argument_2));
#ifdef __linux__
    static const int         errors[]      = {EACCES, EINVAL, ENOMEM, ERANGE};
    static const char *const error_names[] = {"EACCES", "EINVAL", "ENOMEM", "ERANGE"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EINVAL, ENOENT, ENOMEM, ERANGE};
    static const char *const error_names[] = {"EACCES", "EINVAL", "ENOENT", "ENOMEM", "ERANGE"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EINVAL, ENOENT, ENOMEM, ERANGE};
    static const char *const error_names[] = {"EACCES", "EINVAL", "ENOENT", "ENOMEM", "ERANGE"};
#else
    static const int         errors[]      = {EACCES, EINVAL, ENOMEM, ERANGE};
    static const char *const error_names[] = {"EACCES", "EINVAL", "ENOMEM", "ERANGE"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        char *result = p101_getcwd(env, err, argument_2, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (NULL));
        EXPECT(memcmp(argument_2, argument_2_before, sizeof(argument_2)) == 0);
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_getcwd", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            char  native_argument_2[PATH_MAX] = {0};
            char *native_result               = p101_getcwd(native_env, native_err, native_argument_2, 0);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_getcwd: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_getcwd: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_getcwd: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_glob) */
static void test_p101_glob(struct p101_env *env, struct p101_error *err)
{
    glob_t        argument_5[4];
    unsigned char argument_5_before[sizeof(argument_5)];
    memset(argument_5, 0xA5, sizeof(argument_5));
    memcpy(argument_5_before, argument_5, sizeof(argument_5));
#ifdef __linux__
    static const int         errors[]      = {GLOB_ABORTED, GLOB_NOSPACE};
    static const char *const error_names[] = {"GLOB_ABORTED", "GLOB_NOSPACE"};
#elif defined(__APPLE__)
    static const int         errors[]      = {GLOB_ABORTED, GLOB_NOSPACE};
    static const char *const error_names[] = {"GLOB_ABORTED", "GLOB_NOSPACE"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {GLOB_ABORTED, GLOB_NOSPACE};
    static const char *const error_names[] = {"GLOB_ABORTED", "GLOB_NOSPACE"};
#else
    static const int         errors[]      = {GLOB_ABORTED, GLOB_NOSPACE};
    static const char *const error_names[] = {"GLOB_ABORTED", "GLOB_NOSPACE"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_glob(env, err, NULL, 0, NULL, argument_5);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_error(err, P101_ERROR_SYSTEM, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == state.code);
        EXPECT(memcmp(argument_5, argument_5_before, sizeof(argument_5)) == 0);
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_glob", "system", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            glob_t native_argument_5 = {0};
            int    native_result     = p101_glob(native_env, native_err, "p101", 0, native_path_error_callback, &native_argument_5);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_glob: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_glob: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_glob: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_lchown) */
static void test_p101_lchown(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EINTR, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EINTR", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EPERM", "EROFS"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EBADF, EFAULT, EINTR, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EFAULT", "EINTR", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EPERM", "EROFS"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EBADF, EFAULT, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EFAULT", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EPERM", "EROFS"};
#else
    static const int         errors[]      = {EACCES, EINTR, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EINTR", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EPERM", "EROFS"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_lchown(env, err, NULL, 0, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_lchown", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_lchown(native_env, native_err, "p101", 0, 0);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_lchown: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_lchown: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_lchown: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_link) */
static void test_p101_link(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EDQUOT, EEXIST, EFAULT, EIO, ELOOP, EMLINK, ENAMETOOLONG, ENOENT, ENOMEM, ENOSPC, ENOTDIR, EPERM, EROFS, EXDEV};
    static const char *const error_names[] = {"EACCES", "EDQUOT", "EEXIST", "EFAULT", "EIO", "ELOOP", "EMLINK", "ENAMETOOLONG", "ENOENT", "ENOMEM", "ENOSPC", "ENOTDIR", "EPERM", "EROFS", "EXDEV"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EBADF, EDEADLK, EDQUOT, EEXIST, EFAULT, EINVAL, EIO, ELOOP, EMLINK, ENAMETOOLONG, ENOENT, ENOSPC, ENOTDIR, ENOTSUP, EPERM, EROFS, EXDEV};
    static const char *const error_names[] = {"EACCES", "EBADF", "EDEADLK", "EDQUOT", "EEXIST", "EFAULT", "EINVAL", "EIO", "ELOOP", "EMLINK", "ENAMETOOLONG", "ENOENT", "ENOSPC", "ENOTDIR", "ENOTSUP", "EPERM", "EROFS", "EXDEV"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EBADF, EDQUOT, EEXIST, EFAULT, EINVAL, EIO, ELOOP, EMLINK, ENAMETOOLONG, ENOENT, ENOSPC, ENOTDIR, EOPNOTSUPP, EPERM, EROFS, EXDEV};
    static const char *const error_names[] = {"EACCES", "EBADF", "EDQUOT", "EEXIST", "EFAULT", "EINVAL", "EIO", "ELOOP", "EMLINK", "ENAMETOOLONG", "ENOENT", "ENOSPC", "ENOTDIR", "EOPNOTSUPP", "EPERM", "EROFS", "EXDEV"};
#else
    static const int         errors[]      = {EACCES, EEXIST, EILSEQ, ELOOP, EMLINK, ENAMETOOLONG, ENOENT, ENOSPC, ENOTDIR, EPERM, EROFS, EXDEV};
    static const char *const error_names[] = {"EACCES", "EEXIST", "EILSEQ", "ELOOP", "EMLINK", "ENAMETOOLONG", "ENOENT", "ENOSPC", "ENOTDIR", "EPERM", "EROFS", "EXDEV"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_link(env, err, NULL, NULL);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_link", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_link(native_env, native_err, "p101", "p101");
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_link: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_link: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_link: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_linkat) */
static void test_p101_linkat(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EBADF, EDQUOT, EEXIST, EFAULT, EINVAL, EIO, ELOOP, EMLINK, ENAMETOOLONG, ENOENT, ENOMEM, ENOSPC, ENOTDIR, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EDQUOT", "EEXIST", "EFAULT", "EINVAL", "EIO", "ELOOP", "EMLINK", "ENAMETOOLONG", "ENOENT", "ENOMEM", "ENOSPC", "ENOTDIR", "EPERM", "EROFS"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EBADF, EINVAL, ELOOP, ENOTDIR, ENOTSUP};
    static const char *const error_names[] = {"EBADF", "EINVAL", "ELOOP", "ENOTDIR", "ENOTSUP"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EBADF, EINVAL, ENOTDIR};
    static const char *const error_names[] = {"EBADF", "EINVAL", "ENOTDIR"};
#else
    static const int         errors[]      = {EACCES, EBADF, EEXIST, EILSEQ, EINVAL, ELOOP, EMLINK, ENAMETOOLONG, ENOENT, ENOSPC, ENOTDIR, EPERM, EROFS, EXDEV};
    static const char *const error_names[] = {"EACCES", "EBADF", "EEXIST", "EILSEQ", "EINVAL", "ELOOP", "EMLINK", "ENAMETOOLONG", "ENOENT", "ENOSPC", "ENOTDIR", "EPERM", "EROFS", "EXDEV"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_linkat(env, err, 0, NULL, 0, NULL, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_linkat", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_linkat(native_env, native_err, 0, "p101", 0, "p101", 0);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_linkat: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_linkat: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_linkat: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_lstat) */
static void test_p101_lstat(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EOVERFLOW};
    static const char *const error_names[] = {"EACCES", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EOVERFLOW"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EBADF, EFAULT, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EOVERFLOW};
    static const char *const error_names[] = {"EACCES", "EBADF", "EFAULT", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EOVERFLOW"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EBADF, EFAULT, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EOVERFLOW};
    static const char *const error_names[] = {"EACCES", "EBADF", "EFAULT", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EOVERFLOW"};
#else
    static const int         errors[]      = {EACCES, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EOVERFLOW};
    static const char *const error_names[] = {"EACCES", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EOVERFLOW"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_lstat(env, err, NULL, NULL);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_lstat", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            struct stat native_argument_3 = {0};
            int         native_result     = p101_lstat(native_env, native_err, "p101", &native_argument_3);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_lstat: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_lstat: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_lstat: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_mkdir) */
static void test_p101_mkdir(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EEXIST, EILSEQ, ELOOP, EMLINK, ENAMETOOLONG, ENOENT, ENOSPC, ENOTDIR, EROFS};
    static const char *const error_names[] = {"EACCES", "EEXIST", "EILSEQ", "ELOOP", "EMLINK", "ENAMETOOLONG", "ENOENT", "ENOSPC", "ENOTDIR", "EROFS"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EBADF, EDQUOT, EEXIST, EFAULT, EILSEQ, EIO, EISDIR, ELOOP, EMLINK, ENAMETOOLONG, ENOENT, ENOSPC, ENOTDIR, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EDQUOT", "EEXIST", "EFAULT", "EILSEQ", "EIO", "EISDIR", "ELOOP", "EMLINK", "ENAMETOOLONG", "ENOENT", "ENOSPC", "ENOTDIR", "EROFS"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EBADF, EDQUOT, EEXIST, EFAULT, EIO, ELOOP, EMLINK, ENAMETOOLONG, ENOENT, ENOSPC, ENOTDIR, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EDQUOT", "EEXIST", "EFAULT", "EIO", "ELOOP", "EMLINK", "ENAMETOOLONG", "ENOENT", "ENOSPC", "ENOTDIR", "EPERM", "EROFS"};
#else
    static const int         errors[]      = {EACCES, EEXIST, EILSEQ, ELOOP, EMLINK, ENAMETOOLONG, ENOENT, ENOSPC, ENOTDIR, EROFS};
    static const char *const error_names[] = {"EACCES", "EEXIST", "EILSEQ", "ELOOP", "EMLINK", "ENAMETOOLONG", "ENOENT", "ENOSPC", "ENOTDIR", "EROFS"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_mkdir(env, err, NULL, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_mkdir", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_mkdir(native_env, native_err, "p101", 0);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_mkdir: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_mkdir: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_mkdir: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_mkdirat) */
static void test_p101_mkdirat(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EBADF, EDQUOT, EEXIST, EFAULT, EINVAL, ELOOP, EMLINK, ENAMETOOLONG, ENOENT, ENOMEM, ENOSPC, ENOTDIR, EOVERFLOW, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EDQUOT", "EEXIST", "EFAULT", "EINVAL", "ELOOP", "EMLINK", "ENAMETOOLONG", "ENOENT", "ENOMEM", "ENOSPC", "ENOTDIR", "EOVERFLOW", "EPERM", "EROFS"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EBADF, EILSEQ, ENOTDIR};
    static const char *const error_names[] = {"EBADF", "EILSEQ", "ENOTDIR"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EBADF, ENOTDIR};
    static const char *const error_names[] = {"EBADF", "ENOTDIR"};
#else
    static const int         errors[]      = {EACCES, EBADF, EEXIST, EILSEQ, ELOOP, EMLINK, ENAMETOOLONG, ENOENT, ENOSPC, ENOTDIR, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EEXIST", "EILSEQ", "ELOOP", "EMLINK", "ENAMETOOLONG", "ENOENT", "ENOSPC", "ENOTDIR", "EROFS"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_mkdirat(env, err, 0, NULL, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_mkdirat", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_mkdirat(native_env, native_err, 0, "p101", 0);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_mkdirat: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_mkdirat: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_mkdirat: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_mkdtemp) */
static void test_p101_mkdtemp(struct p101_env *env, struct p101_error *err)
{
    char          argument_2[4];
    unsigned char argument_2_before[sizeof(argument_2)];
    memset(argument_2, 0xA5, sizeof(argument_2));
    memcpy(argument_2_before, argument_2, sizeof(argument_2));
#ifdef __linux__
    static const int         errors[]      = {EINVAL};
    static const char *const error_names[] = {"EINVAL"};
#elif defined(__APPLE__)
    static const int         errors[]      = {ENOTDIR};
    static const char *const error_names[] = {"ENOTDIR"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EINVAL, ENOTDIR};
    static const char *const error_names[] = {"EINVAL", "ENOTDIR"};
#else
    static const int         errors[]      = {EINVAL};
    static const char *const error_names[] = {"EINVAL"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        char *result = p101_mkdtemp(env, err, argument_2);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (NULL));
        EXPECT(memcmp(argument_2, argument_2_before, sizeof(argument_2)) == 0);
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_mkdtemp", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            char  native_argument_2[PATH_MAX] = {0};
            char *native_result               = p101_mkdtemp(native_env, native_err, native_argument_2);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_mkdtemp: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_mkdtemp: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_mkdtemp: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_mknod) */
static void test_p101_mknod(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EDQUOT, EEXIST, EFAULT, EINVAL, ELOOP, ENAMETOOLONG, ENOENT, ENOMEM, ENOSPC, ENOTDIR, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EDQUOT", "EEXIST", "EFAULT", "EINVAL", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOMEM", "ENOSPC", "ENOTDIR", "EPERM", "EROFS"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EBADF, EDQUOT, EEXIST, EFAULT, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOSPC, ENOTDIR, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EDQUOT", "EEXIST", "EFAULT", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOSPC", "ENOTDIR", "EPERM", "EROFS"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EBADF, EDQUOT, EEXIST, EFAULT, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOSPC, ENOTDIR, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EDQUOT", "EEXIST", "EFAULT", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOSPC", "ENOTDIR", "EPERM", "EROFS"};
#else
    static const int         errors[]      = {EACCES, EEXIST, EILSEQ, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOSPC, ENOTDIR, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EEXIST", "EILSEQ", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOSPC", "ENOTDIR", "EPERM", "EROFS"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_mknod(env, err, NULL, 0, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_mknod", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_mknod(native_env, native_err, "p101", 0, 0);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_mknod: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_mknod: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_mknod: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_mkstemp) */
static void test_p101_mkstemp(struct p101_env *env, struct p101_error *err)
{
    char          argument_2[4];
    unsigned char argument_2_before[sizeof(argument_2)];
    memset(argument_2, 0xA5, sizeof(argument_2));
    memcpy(argument_2_before, argument_2, sizeof(argument_2));
#ifdef __linux__
    static const int         errors[]      = {EACCES, EBUSY, EDQUOT, EEXIST, EFAULT, EFBIG, EINTR, EINVAL, EISDIR, ELOOP, EMFILE, ENAMETOOLONG, ENFILE, ENODEV, ENOENT, ENOMEM, ENOSPC, ENOTDIR, ENXIO, EOPNOTSUPP, EOVERFLOW, EPERM, EROFS, ETXTBSY, EWOULDBLOCK};
    static const char *const error_names[] = {"EACCES", "EBUSY",  "EDQUOT", "EEXIST", "EFAULT",  "EFBIG", "EINTR",      "EINVAL",    "EISDIR", "ELOOP", "EMFILE",  "ENAMETOOLONG", "ENFILE",
                                              "ENODEV", "ENOENT", "ENOMEM", "ENOSPC", "ENOTDIR", "ENXIO", "EOPNOTSUPP", "EOVERFLOW", "EPERM",  "EROFS", "ETXTBSY", "EWOULDBLOCK"};
#elif defined(__APPLE__)
    static const int         errors[]      = {ENOTDIR};
    static const char *const error_names[] = {"ENOTDIR"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EINVAL, ENOTDIR};
    static const char *const error_names[] = {"EINVAL", "ENOTDIR"};
#else
    static const int         errors[]      = {EINVAL};
    static const char *const error_names[] = {"EINVAL"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_mkstemp(env, err, argument_2);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(memcmp(argument_2, argument_2_before, sizeof(argument_2)) == 0);
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_mkstemp", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            char native_argument_2[PATH_MAX] = {0};
            int  native_result               = p101_mkstemp(native_env, native_err, native_argument_2);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_mkstemp: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_mkstemp: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_mkstemp: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_nftw) */
static void test_p101_nftw(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, ELOOP, EMFILE, ENAMETOOLONG, ENFILE, ENOENT, ENOTDIR, EOVERFLOW};
    static const char *const error_names[] = {"EACCES", "ELOOP", "EMFILE", "ENAMETOOLONG", "ENFILE", "ENOENT", "ENOTDIR", "EOVERFLOW"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EAGAIN, EBADF, EDEADLK, EDQUOT, EEXIST, EFAULT, EILSEQ, EINTR, EINVAL, EIO, EISDIR, ELOOP, EMFILE, ENAMETOOLONG, ENFILE, ENOENT, ENOSPC, ENOTDIR, ENXIO, EOPNOTSUPP, EOVERFLOW, EROFS, ETXTBSY, EWOULDBLOCK};
    static const char *const error_names[] = {"EACCES", "EAGAIN",       "EBADF",  "EDEADLK", "EDQUOT", "EEXIST",  "EFAULT", "EILSEQ",     "EINTR",     "EINVAL", "EIO",     "EISDIR",     "ELOOP",
                                              "EMFILE", "ENAMETOOLONG", "ENFILE", "ENOENT",  "ENOSPC", "ENOTDIR", "ENXIO",  "EOPNOTSUPP", "EOVERFLOW", "EROFS",  "ETXTBSY", "EWOULDBLOCK"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EBADF, EDQUOT, EEXIST, EFAULT, EINTR, EINVAL, EIO, EISDIR, ELOOP, EMFILE, EMLINK, ENAMETOOLONG, ENFILE, ENOENT, ENOSPC, ENOTDIR, ENXIO, EOPNOTSUPP, EOVERFLOW, EPERM, EROFS, ETXTBSY, EWOULDBLOCK};
    static const char *const error_names[] = {"EACCES",       "EBADF",  "EDQUOT", "EEXIST", "EFAULT",  "EINTR", "EINVAL",     "EIO",       "EISDIR", "ELOOP", "EMFILE",  "EMLINK",
                                              "ENAMETOOLONG", "ENFILE", "ENOENT", "ENOSPC", "ENOTDIR", "ENXIO", "EOPNOTSUPP", "EOVERFLOW", "EPERM",  "EROFS", "ETXTBSY", "EWOULDBLOCK"};
#else
    static const int         errors[]      = {EACCES, ELOOP, EMFILE, ENAMETOOLONG, ENFILE, ENOENT, ENOTDIR, EOVERFLOW};
    static const char *const error_names[] = {"EACCES", "ELOOP", "EMFILE", "ENAMETOOLONG", "ENFILE", "ENOENT", "ENOTDIR", "EOVERFLOW"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_nftw(env, err, NULL, 0, 0, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_nftw", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_nftw(native_env, native_err, "p101", 0, 0, 0);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_nftw: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_nftw: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_nftw: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_opendir) */
static void test_p101_opendir(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EBADF, EMFILE, ENAMETOOLONG, ENFILE, ENOENT, ENOMEM, ENOTDIR};
    static const char *const error_names[] = {"EACCES", "EBADF", "EMFILE", "ENAMETOOLONG", "ENFILE", "ENOENT", "ENOMEM", "ENOTDIR"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR};
    static const char *const error_names[] = {"EACCES", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR};
    static const char *const error_names[] = {"EACCES", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR"};
#else
    static const int         errors[]      = {EACCES, ELOOP, EMFILE, ENAMETOOLONG, ENFILE, ENOENT, ENOTDIR};
    static const char *const error_names[] = {"EACCES", "ELOOP", "EMFILE", "ENAMETOOLONG", "ENFILE", "ENOENT", "ENOTDIR"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        DIR *result = p101_opendir(env, err, NULL);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (NULL));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_opendir", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            DIR *native_result = p101_opendir(native_env, native_err, "p101");
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_opendir: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_opendir: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_opendir: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_pathconf) */
static void test_p101_pathconf(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EINVAL, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR};
    static const char *const error_names[] = {"EACCES", "EINVAL", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EBADF, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR};
    static const char *const error_names[] = {"EACCES", "EBADF", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR};
    static const char *const error_names[] = {"EACCES", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR"};
#else
    static const int         errors[]      = {EACCES, EINVAL, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EOVERFLOW};
    static const char *const error_names[] = {"EACCES", "EINVAL", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EOVERFLOW"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        long result = p101_pathconf(env, err, NULL, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_pathconf", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            long native_result = p101_pathconf(native_env, native_err, "p101", 0);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_pathconf: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_pathconf: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_pathconf: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_readdir) */
static void test_p101_readdir(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EBADF, EFAULT, EINVAL, ENOENT, ENOTDIR};
    static const char *const error_names[] = {"EBADF", "EFAULT", "EINVAL", "ENOENT", "ENOTDIR"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EBADF, ENOENT, ENOMEM, EOVERFLOW};
    static const char *const error_names[] = {"EBADF", "ENOENT", "ENOMEM", "EOVERFLOW"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EBADF, ENOENT, ENOMEM, EOVERFLOW};
    static const char *const error_names[] = {"EBADF", "ENOENT", "ENOMEM", "EOVERFLOW"};
#else
    static const int         errors[]      = {EBADF, ENOENT, ENOMEM, EOVERFLOW};
    static const char *const error_names[] = {"EBADF", "ENOENT", "ENOMEM", "EOVERFLOW"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        struct dirent *result = p101_readdir(env, err, NULL);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (NULL));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_readdir", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            DIR *native_argument_2 = opendir(".");
            if(native_argument_2 == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            struct dirent *native_result = p101_readdir(native_env, native_err, native_argument_2);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_readdir: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            P101_NATIVE_CLEANUP_ERRNO(closedir(native_argument_2));
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_readdir: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_readdir: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_readlink) */
static void test_p101_readlink(struct p101_env *env, struct p101_error *err)
{
    char          argument_3[4];
    unsigned char argument_3_before[sizeof(argument_3)];
    memset(argument_3, 0xA5, sizeof(argument_3));
    memcpy(argument_3_before, argument_3, sizeof(argument_3));
#ifdef __linux__
    static const int         errors[]      = {EACCES, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR};
    static const char *const error_names[] = {"EACCES", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EBADF, EFAULT, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR};
    static const char *const error_names[] = {"EACCES", "EBADF", "EFAULT", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EBADF, EFAULT, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR};
    static const char *const error_names[] = {"EACCES", "EBADF", "EFAULT", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR"};
#else
    static const int         errors[]      = {EACCES, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR};
    static const char *const error_names[] = {"EACCES", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        ssize_t result = p101_readlink(env, err, NULL, argument_3, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == ((ssize_t)-1));
        EXPECT(memcmp(argument_3, argument_3_before, sizeof(argument_3)) == 0);
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_readlink", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            char    native_argument_3[PATH_MAX] = {0};
            ssize_t native_result               = p101_readlink(native_env, native_err, "p101", native_argument_3, 0);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_readlink: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_readlink: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_readlink: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_readlinkat) */
static void test_p101_readlinkat(struct p101_env *env, struct p101_error *err)
{
    char          argument_4[4];
    unsigned char argument_4_before[sizeof(argument_4)];
    memset(argument_4, 0xA5, sizeof(argument_4));
    memcpy(argument_4_before, argument_4, sizeof(argument_4));
#ifdef __linux__
    static const int         errors[]      = {EACCES, EBADF, EFAULT, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOMEM, ENOTDIR};
    static const char *const error_names[] = {"EACCES", "EBADF", "EFAULT", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOMEM", "ENOTDIR"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EBADF, ENOTDIR};
    static const char *const error_names[] = {"EBADF", "ENOTDIR"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EBADF, ENOTDIR};
    static const char *const error_names[] = {"EBADF", "ENOTDIR"};
#else
    static const int         errors[]      = {EACCES, EBADF, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR};
    static const char *const error_names[] = {"EACCES", "EBADF", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        ssize_t result = p101_readlinkat(env, err, 0, NULL, argument_4, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == ((ssize_t)-1));
        EXPECT(memcmp(argument_4, argument_4_before, sizeof(argument_4)) == 0);
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_readlinkat", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            char    native_argument_4[PATH_MAX] = {0};
            ssize_t native_result               = p101_readlinkat(native_env, native_err, 0, "p101", native_argument_4, 0);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_readlinkat: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_readlinkat: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_readlinkat: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_realpath) */
static void test_p101_realpath(struct p101_env *env, struct p101_error *err)
{
    char          argument_3[4];
    unsigned char argument_3_before[sizeof(argument_3)];
    memset(argument_3, 0xA5, sizeof(argument_3));
    memcpy(argument_3_before, argument_3, sizeof(argument_3));
#ifdef __linux__
    static const int         errors[]      = {EACCES, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOMEM, ENOTDIR};
    static const char *const error_names[] = {"EACCES", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOMEM", "ENOTDIR"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EBADF, EFAULT, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOMEM, ENOTDIR, EOVERFLOW, ERANGE};
    static const char *const error_names[] = {"EACCES", "EBADF", "EFAULT", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOMEM", "ENOTDIR", "EOVERFLOW", "ERANGE"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EBADF, EFAULT, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOMEM, ENOTDIR, EOVERFLOW, ERANGE};
    static const char *const error_names[] = {"EACCES", "EBADF", "EFAULT", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOMEM", "ENOTDIR", "EOVERFLOW", "ERANGE"};
#else
    static const int         errors[]      = {EACCES, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOMEM, ENOTDIR};
    static const char *const error_names[] = {"EACCES", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOMEM", "ENOTDIR"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        char *result = p101_realpath(env, err, NULL, argument_3);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (NULL));
        EXPECT(memcmp(argument_3, argument_3_before, sizeof(argument_3)) == 0);
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_realpath", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            char  native_argument_3[PATH_MAX] = {0};
            char *native_result               = p101_realpath(native_env, native_err, "p101", native_argument_3);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_realpath: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_realpath: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_realpath: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_renameat) */
static void test_p101_renameat(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EBADF, EBUSY, EDQUOT, EEXIST, EFAULT, EINVAL, EISDIR, ELOOP, EMLINK, ENAMETOOLONG, ENOENT, ENOMEM, ENOSPC, ENOTDIR, ENOTEMPTY, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EBUSY", "EDQUOT", "EEXIST", "EFAULT", "EINVAL", "EISDIR", "ELOOP", "EMLINK", "ENAMETOOLONG", "ENOENT", "ENOMEM", "ENOSPC", "ENOTDIR", "ENOTEMPTY", "EPERM", "EROFS"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EBADF, ENOTDIR};
    static const char *const error_names[] = {"EBADF", "ENOTDIR"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EBADF, EBUSY, EEXIST, EILSEQ, EINVAL, EIO, EISDIR, ELOOP, EMLINK, ENAMETOOLONG, ENOENT, ENOSPC, ENOTDIR, ENOTEMPTY, EPERM, EROFS, ETXTBSY, EXDEV};
    static const char *const error_names[] = {"EACCES", "EBADF", "EBUSY", "EEXIST", "EILSEQ", "EINVAL", "EIO", "EISDIR", "ELOOP", "EMLINK", "ENAMETOOLONG", "ENOENT", "ENOSPC", "ENOTDIR", "ENOTEMPTY", "EPERM", "EROFS", "ETXTBSY", "EXDEV"};
#else
    static const int         errors[]      = {EACCES, EBADF, EBUSY, EEXIST, EILSEQ, EINVAL, EIO, EISDIR, ELOOP, EMLINK, ENAMETOOLONG, ENOENT, ENOSPC, ENOTDIR, ENOTEMPTY, EPERM, EROFS, ETXTBSY, EXDEV};
    static const char *const error_names[] = {"EACCES", "EBADF", "EBUSY", "EEXIST", "EILSEQ", "EINVAL", "EIO", "EISDIR", "ELOOP", "EMLINK", "ENAMETOOLONG", "ENOENT", "ENOSPC", "ENOTDIR", "ENOTEMPTY", "EPERM", "EROFS", "ETXTBSY", "EXDEV"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_renameat(env, err, 0, NULL, 0, NULL);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_renameat", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_renameat(native_env, native_err, 0, "p101", 0, "p101");
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_renameat: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_renameat: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_renameat: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_rmdir) */
static void test_p101_rmdir(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EBUSY, EFAULT, EINVAL, ELOOP, ENAMETOOLONG, ENOENT, ENOMEM, ENOTDIR, ENOTEMPTY, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EBUSY", "EFAULT", "EINVAL", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOMEM", "ENOTDIR", "ENOTEMPTY", "EPERM", "EROFS"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EBUSY, EFAULT, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, ENOTEMPTY, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EBUSY", "EFAULT", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "ENOTEMPTY", "EPERM", "EROFS"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EBUSY, EFAULT, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, ENOTEMPTY, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EBUSY", "EFAULT", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "ENOTEMPTY", "EPERM", "EROFS"};
#else
    static const int         errors[]      = {EACCES, EBUSY, EEXIST, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, ENOTEMPTY, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EBUSY", "EEXIST", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "ENOTEMPTY", "EPERM", "EROFS"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_rmdir(env, err, NULL);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_rmdir", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_rmdir(native_env, native_err, "p101");
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_rmdir: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_rmdir: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_rmdir: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_scandir) */
static void test_p101_scandir(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, ELOOP, EMFILE, ENAMETOOLONG, ENFILE, ENOENT, ENOMEM, ENOTDIR, EOVERFLOW};
    static const char *const error_names[] = {"EACCES", "ELOOP", "EMFILE", "ENAMETOOLONG", "ENFILE", "ENOENT", "ENOMEM", "ENOTDIR", "EOVERFLOW"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EBADF, EINTR, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR};
    static const char *const error_names[] = {"EACCES", "EBADF", "EINTR", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EBADF, EINTR, ELOOP, ENAMETOOLONG, ENOENT, ENOSPC, ENOTDIR};
    static const char *const error_names[] = {"EACCES", "EBADF", "EINTR", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOSPC", "ENOTDIR"};
#else
    static const int         errors[]      = {EACCES, ELOOP, EMFILE, ENAMETOOLONG, ENFILE, ENOENT, ENOMEM, ENOTDIR, EOVERFLOW};
    static const char *const error_names[] = {"EACCES", "ELOOP", "EMFILE", "ENAMETOOLONG", "ENFILE", "ENOENT", "ENOMEM", "ENOTDIR", "EOVERFLOW"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_scandir(env, err, NULL, NULL, NULL, NULL);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_scandir", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            struct dirent **native_argument_3 = NULL;
            int             native_result     = p101_scandir(native_env, native_err, "p101", &native_argument_3, native_dirent_filter, native_dirent_compare);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_scandir: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_scandir: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_scandir: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_stat) */
static void test_p101_stat(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EBADF, EFAULT, ELOOP, ENAMETOOLONG, ENOENT, ENOMEM, ENOTDIR, EOVERFLOW};
    static const char *const error_names[] = {"EACCES", "EBADF", "EFAULT", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOMEM", "ENOTDIR", "EOVERFLOW"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EBADF, EFAULT, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EOVERFLOW};
    static const char *const error_names[] = {"EACCES", "EBADF", "EFAULT", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EOVERFLOW"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EFAULT, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EOVERFLOW};
    static const char *const error_names[] = {"EACCES", "EFAULT", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EOVERFLOW"};
#else
    static const int         errors[]      = {EACCES, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EOVERFLOW};
    static const char *const error_names[] = {"EACCES", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EOVERFLOW"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_stat(env, err, NULL, NULL);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_stat", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            struct stat native_argument_3 = {0};
            int         native_result     = p101_stat(native_env, native_err, "p101", &native_argument_3);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_stat: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_stat: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_stat: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_statvfs) */
static void test_p101_statvfs(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EFAULT, EINTR, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOMEM, ENOSYS, ENOTDIR, EOVERFLOW};
    static const char *const error_names[] = {"EACCES", "EFAULT", "EINTR", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOMEM", "ENOSYS", "ENOTDIR", "EOVERFLOW"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EFAULT, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR};
    static const char *const error_names[] = {"EACCES", "EFAULT", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EOVERFLOW};
    static const char *const error_names[] = {"EOVERFLOW"};
#else
    static const int         errors[]      = {EACCES, EINTR, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EOVERFLOW};
    static const char *const error_names[] = {"EACCES", "EINTR", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EOVERFLOW"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_statvfs(env, err, NULL, NULL);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_statvfs", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            struct statvfs native_argument_3 = {0};
            int            native_result     = p101_statvfs(native_env, native_err, "p101", &native_argument_3);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_statvfs: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_statvfs: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_statvfs: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_symlink) */
static void test_p101_symlink(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EEXIST, EILSEQ, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOSPC, ENOTDIR, EROFS};
    static const char *const error_names[] = {"EACCES", "EEXIST", "EILSEQ", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOSPC", "ENOTDIR", "EROFS"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EBADF, EDQUOT, EEXIST, EFAULT, EILSEQ, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOSPC, ENOTDIR, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EDQUOT", "EEXIST", "EFAULT", "EILSEQ", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOSPC", "ENOTDIR", "EROFS"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EBADF, EDQUOT, EEXIST, EFAULT, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOSPC, ENOTDIR, EOPNOTSUPP, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EDQUOT", "EEXIST", "EFAULT", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOSPC", "ENOTDIR", "EOPNOTSUPP", "EPERM", "EROFS"};
#else
    static const int         errors[]      = {EACCES, EEXIST, EILSEQ, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOSPC, ENOTDIR, EROFS};
    static const char *const error_names[] = {"EACCES", "EEXIST", "EILSEQ", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOSPC", "ENOTDIR", "EROFS"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_symlink(env, err, NULL, NULL);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_symlink", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_symlink(native_env, native_err, "p101", "p101");
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_symlink: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_symlink: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_symlink: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_symlinkat) */
static void test_p101_symlinkat(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EBADF, EDQUOT, EEXIST, EFAULT, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOMEM, ENOSPC, ENOTDIR, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EDQUOT", "EEXIST", "EFAULT", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOMEM", "ENOSPC", "ENOTDIR", "EPERM", "EROFS"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EBADF, EDQUOT, EEXIST, EFAULT, EILSEQ, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOSPC, ENOTDIR, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EDQUOT", "EEXIST", "EFAULT", "EILSEQ", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOSPC", "ENOTDIR", "EROFS"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EBADF, EDQUOT, EEXIST, EFAULT, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOSPC, ENOTDIR, EOPNOTSUPP, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EDQUOT", "EEXIST", "EFAULT", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOSPC", "ENOTDIR", "EOPNOTSUPP", "EPERM", "EROFS"};
#else
    static const int         errors[]      = {EACCES, EBADF, EEXIST, EILSEQ, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOSPC, ENOTDIR, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EEXIST", "EILSEQ", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOSPC", "ENOTDIR", "EROFS"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_symlinkat(env, err, NULL, 0, NULL);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_symlinkat", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_symlinkat(native_env, native_err, "p101", 0, "p101");
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_symlinkat: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_symlinkat: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_symlinkat: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_telldir) */
static void test_p101_telldir(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EBADF};
    static const char *const error_names[] = {"EBADF"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EIO};
    static const char *const error_names[] = {"EIO"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EIO};
    static const char *const error_names[] = {"EIO"};
#else
    static const int         errors[]      = {EIO};
    static const char *const error_names[] = {"EIO"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        long result = p101_telldir(env, err, NULL);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1L));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_telldir", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            DIR *native_argument_2 = opendir(".");
            if(native_argument_2 == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            long native_result = p101_telldir(native_env, native_err, native_argument_2);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_telldir: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            P101_NATIVE_CLEANUP_ERRNO(closedir(native_argument_2));
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_telldir: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_telldir: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_truncate) */
static void test_p101_truncate(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EFAULT, EFBIG, EINTR, EINVAL, EIO, EISDIR, ELOOP, ENAMETOOLONG, ENOENT, ENOSPC, ENOTDIR, EPERM, EROFS, ETXTBSY};
    static const char *const error_names[] = {"EACCES", "EFAULT", "EFBIG", "EINTR", "EINVAL", "EIO", "EISDIR", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOSPC", "ENOTDIR", "EPERM", "EROFS", "ETXTBSY"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EDEADLK, EFAULT, EFBIG, EINTR, EINVAL, EIO, EISDIR, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EROFS, ETXTBSY};
    static const char *const error_names[] = {"EACCES", "EDEADLK", "EFAULT", "EFBIG", "EINTR", "EINVAL", "EIO", "EISDIR", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EROFS", "ETXTBSY"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EFAULT, EFBIG, EINVAL, EIO, EISDIR, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EPERM, EROFS, ETXTBSY};
    static const char *const error_names[] = {"EACCES", "EFAULT", "EFBIG", "EINVAL", "EIO", "EISDIR", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EPERM", "EROFS", "ETXTBSY"};
#else
    static const int         errors[]      = {EACCES, EFBIG, EINTR, EINVAL, EIO, EISDIR, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EROFS};
    static const char *const error_names[] = {"EACCES", "EFBIG", "EINTR", "EINVAL", "EIO", "EISDIR", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EROFS"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_truncate(env, err, NULL, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_truncate", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_truncate(native_env, native_err, "p101", 0);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_truncate: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_truncate: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_truncate: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_unlink) */
static void test_p101_unlink(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EBADF, EBUSY, EFAULT, EINVAL, EIO, EISDIR, ELOOP, ENAMETOOLONG, ENOENT, ENOMEM, ENOTDIR, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EBUSY", "EFAULT", "EINVAL", "EIO", "EISDIR", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOMEM", "ENOTDIR", "EPERM", "EROFS"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EBADF, EBUSY, EFAULT, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, ENOTEMPTY, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EBUSY", "EFAULT", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "ENOTEMPTY", "EPERM", "EROFS"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EBADF, EFAULT, EINVAL, EIO, EISDIR, ELOOP, ENAMETOOLONG, ENOENT, ENOSPC, ENOTDIR, ENOTEMPTY, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EFAULT", "EINVAL", "EIO", "EISDIR", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOSPC", "ENOTDIR", "ENOTEMPTY", "EPERM", "EROFS"};
#else
    static const int         errors[]      = {EACCES, EBUSY, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EPERM, EROFS, ETXTBSY};
    static const char *const error_names[] = {"EACCES", "EBUSY", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EPERM", "EROFS", "ETXTBSY"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_unlink(env, err, NULL);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_unlink", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_unlink(native_env, native_err, "p101");
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_unlink: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_unlink: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_unlink: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_unlinkat) */
static void test_p101_unlinkat(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EBADF, EBUSY, EFAULT, EINVAL, EIO, EISDIR, ELOOP, ENAMETOOLONG, ENOENT, ENOMEM, ENOTDIR, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EBUSY", "EFAULT", "EINVAL", "EIO", "EISDIR", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOMEM", "ENOTDIR", "EPERM", "EROFS"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EBADF, EINVAL, ELOOP, ENOTDIR, ENOTEMPTY};
    static const char *const error_names[] = {"EBADF", "EINVAL", "ELOOP", "ENOTDIR", "ENOTEMPTY"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EBADF, EDEADLK, EINVAL, ENOTDIR, ENOTEMPTY};
    static const char *const error_names[] = {"EBADF", "EDEADLK", "EINVAL", "ENOTDIR", "ENOTEMPTY"};
#else
    static const int         errors[]      = {EACCES, EBADF, EBUSY, EEXIST, EINVAL, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, ENOTEMPTY, EPERM, EROFS, ETXTBSY};
    static const char *const error_names[] = {"EACCES", "EBADF", "EBUSY", "EEXIST", "EINVAL", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "ENOTEMPTY", "EPERM", "EROFS", "ETXTBSY"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_unlinkat(env, err, 0, NULL, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_unlinkat", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_unlinkat(native_env, native_err, 0, "p101", 0);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_unlinkat: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_unlinkat: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_unlinkat: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_utimensat) */
static void test_p101_utimensat(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EBADF, EFAULT, EINVAL, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EPERM, EROFS, ESRCH};
    static const char *const error_names[] = {"EACCES", "EBADF", "EFAULT", "EINVAL", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EPERM", "EROFS", "ESRCH"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EBADF, EFAULT, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EFAULT", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EPERM", "EROFS"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EBADF, EFAULT, EINVAL, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EFAULT", "EINVAL", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EPERM", "EROFS"};
#else
    static const int         errors[]      = {EACCES, EBADF, EINVAL, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EINVAL", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR", "EPERM", "EROFS"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_utimensat(env, err, 0, NULL, NULL, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_utimensat", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            struct timespec native_argument_4 = {0};
            int             native_result     = p101_utimensat(native_env, native_err, 0, "p101", &native_argument_4, 0);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_utimensat: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_utimensat: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_utimensat: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

int main(void)
{
    const char        *outcome_path;
    struct p101_error *err = NULL;
    struct p101_env   *env = NULL;
    int                status;

    outcome_path = getenv("P101_WRAPPER_OUTCOME_LOG");
    if(outcome_path != NULL && outcome_path[0] != '\0')
    {
        outcome_stream = fopen(outcome_path, "a");
        if(outcome_stream == NULL)
        {
            fprintf(stderr, "FAIL: cannot open wrapper outcome receipt\n");
            failures++;
        }
    }
    if(failures == 0)
    {
        err = p101_error_create(false);
    }
    if(err != NULL)
    {
        env = p101_env_create(err, NULL);
    }
    if(env == NULL)
    {
        failures++;
    }
    else
    {
        p101_env_set_fd_observer(env, count_fd_event, NULL);
        p101_env_set_alloc_observer(env, count_alloc_event, NULL);
        p101_env_set_resource_observer(env, count_resource_event, NULL);
        if(!native_child_process)
        {
            test_p101_access(env, err);
        }
        if(!native_child_process)
        {
            test_p101_basename(env, err);
        }
        if(!native_child_process)
        {
            test_p101_chdir(env, err);
        }
        if(!native_child_process)
        {
            test_p101_chmod(env, err);
        }
        if(!native_child_process)
        {
            test_p101_chown(env, err);
        }
        if(!native_child_process)
        {
            test_p101_closedir(env, err);
        }
        if(!native_child_process)
        {
            test_p101_dirfd(env, err);
        }
        if(!native_child_process)
        {
            test_p101_dirname(env, err);
        }
        if(!native_child_process)
        {
            test_p101_faccessat(env, err);
        }
        if(!native_child_process)
        {
            test_p101_fchdir(env, err);
        }
        if(!native_child_process)
        {
            test_p101_fchmod(env, err);
        }
        if(!native_child_process)
        {
            test_p101_fchmodat(env, err);
        }
        if(!native_child_process)
        {
            test_p101_fchown(env, err);
        }
        if(!native_child_process)
        {
            test_p101_fchownat(env, err);
        }
        if(!native_child_process)
        {
            test_p101_fdopendir(env, err);
        }
        if(!native_child_process)
        {
            test_p101_fnmatch(env, err);
        }
        if(!native_child_process)
        {
            test_p101_fpathconf(env, err);
        }
        if(!native_child_process)
        {
            test_p101_fstat(env, err);
        }
        if(!native_child_process)
        {
            test_p101_fstatat(env, err);
        }
        if(!native_child_process)
        {
            test_p101_fstatvfs(env, err);
        }
        if(!native_child_process)
        {
            test_p101_ftruncate(env, err);
        }
        if(!native_child_process)
        {
            test_p101_ftw(env, err);
        }
        if(!native_child_process)
        {
            test_p101_futimens(env, err);
        }
        if(!native_child_process)
        {
            test_p101_getcwd(env, err);
        }
        if(!native_child_process)
        {
            test_p101_glob(env, err);
        }
        if(!native_child_process)
        {
            test_p101_lchown(env, err);
        }
        if(!native_child_process)
        {
            test_p101_link(env, err);
        }
        if(!native_child_process)
        {
            test_p101_linkat(env, err);
        }
        if(!native_child_process)
        {
            test_p101_lstat(env, err);
        }
        if(!native_child_process)
        {
            test_p101_mkdir(env, err);
        }
        if(!native_child_process)
        {
            test_p101_mkdirat(env, err);
        }
        if(!native_child_process)
        {
            test_p101_mkdtemp(env, err);
        }
        if(!native_child_process)
        {
            test_p101_mknod(env, err);
        }
        if(!native_child_process)
        {
            test_p101_mkstemp(env, err);
        }
        if(!native_child_process)
        {
            test_p101_nftw(env, err);
        }
        if(!native_child_process)
        {
            test_p101_opendir(env, err);
        }
        if(!native_child_process)
        {
            test_p101_pathconf(env, err);
        }
        if(!native_child_process)
        {
            test_p101_readdir(env, err);
        }
        if(!native_child_process)
        {
            test_p101_readlink(env, err);
        }
        if(!native_child_process)
        {
            test_p101_readlinkat(env, err);
        }
        if(!native_child_process)
        {
            test_p101_realpath(env, err);
        }
        if(!native_child_process)
        {
            test_p101_renameat(env, err);
        }
        if(!native_child_process)
        {
            test_p101_rmdir(env, err);
        }
        if(!native_child_process)
        {
            test_p101_scandir(env, err);
        }
        if(!native_child_process)
        {
            test_p101_stat(env, err);
        }
        if(!native_child_process)
        {
            test_p101_statvfs(env, err);
        }
        if(!native_child_process)
        {
            test_p101_symlink(env, err);
        }
        if(!native_child_process)
        {
            test_p101_symlinkat(env, err);
        }
        if(!native_child_process)
        {
            test_p101_telldir(env, err);
        }
        if(!native_child_process)
        {
            test_p101_truncate(env, err);
        }
        if(!native_child_process)
        {
            test_p101_unlink(env, err);
        }
        if(!native_child_process)
        {
            test_p101_unlinkat(env, err);
        }
        if(!native_child_process)
        {
            test_p101_utimensat(env, err);
        }
    }
    p101_env_destroy(env);
    p101_error_destroy(err);
    if(outcome_stream != NULL && fclose(outcome_stream) != 0)
    {
        fprintf(stderr, "FAIL: cannot close wrapper outcome receipt\n");
        failures++;
    }
    if(native_child_process)
    {
        status = native_child_status;
        if(status == EXIT_SUCCESS && failures != 0)
        {
            status = EXIT_FAILURE;
        }
    }
    else
    {
        status = failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    return status;
}

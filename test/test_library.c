#include <p101_env/env.h>
#include <p101_error/error.h>
#include <p101_filesystem/filesystem.h>
#include <stdlib.h>

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
    p101_env_destroy(env);
    p101_error_destroy(err);
    return EXIT_SUCCESS;
}

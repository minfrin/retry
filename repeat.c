/**
 *    (C) 2016 Graham Leggett
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sysexits.h>

#include "config.h"

#define DEFAULT_DELAY 10

static struct option long_options[] =
{
    {"delay", required_argument, NULL, 'd'},
    {"message", required_argument, NULL, 'm'},
    {"until", required_argument, NULL, 'u'},
    {"while", required_argument, NULL, 'w'},
    {"help", no_argument, NULL, 'h'},
    {"version", no_argument, NULL, 'v'},
    {NULL, 0, NULL, 0}
};

static int help(const char *name, const char *msg, int code)
{
    fprintf(code ? stderr : stdout,
            "%sUsage: %s [-v] [-h] [-u until] [-w while] command ...\n"
            "\n"
            "The tool repeats the given command until the command is successful,"
            "backing off with a configurable delay between each attempt.\n"
            "\n"
            "  -d seconds, --delay=seconds\tThe number of seconds to back off\n"
            "\t\t\t\tafter each attempt.\n"
            "  -m message, --message=message\tA message to include in the notification\n"
            "\t\t\t\twhen repeat has backed off. Defaults to the\n"
            "\t\t\t\tcommand name.\n"
            "  -u criteria, --until=criteria\tKeep repeating the command until the\n"
            "\t\t\t\tcriteria is met. Default is 'success'.\n"
            "  -w criteria, --while=criteria\tKeep repeating the command while the\n"
            "\t\t\t\tcriteria is met.\n"
            "  -h, --help\t\t\tDisplay this help message.\n"
            "  -v, --version\t\t\tDisplay the version number.\n"
            "", msg ? msg : "", name);
    return code;
}

static int version()
{
    printf(PACKAGE_STRING "\n");
    return 0;
}

static int should_repeat(int status, const char *repeat_until, const char *repeat_while)
{

    /* TODO: implement criteria handling */

    return status;
}

int main (int argc, char **argv)
{
    const char *name = argv[0];
    const char *repeat_until = "success";
    const char *repeat_while = NULL;
    const char *message = NULL;
    char *endptr = NULL;
    int c, status = 0;
    long int delay = DEFAULT_DELAY;

    while ((c = getopt_long(argc, argv, "d:m:u:w:hv", long_options, NULL)) != -1) {

        switch (c)
        {
        case 'd':
            errno = 0;
            delay = strtol(optarg, &endptr, 10);

            if (errno || endptr[0] || delay < 1) {
                return help(name, "Delay must be bigger or equal to 1.\n", 6);
            }

            break;
        case 'm':
            message = optarg;

            break;
        case 'u':
            repeat_until = optarg;
            repeat_while = NULL;

            break;
        case 'w':
            repeat_until = NULL;
            repeat_while = optarg;

            break;
        case 'h':
            return help(name, NULL, 0);

        case 'v':
            return version();

        default:
            return help(name, NULL, 2);

        }

    }

    if (optind == argc) {
        return help(name, "No command specified.\n", 1);
    }

    while (1) {
        pid_t w, f;

        /* Clear any inherited settings */
        signal(SIGCHLD, SIG_DFL);

        f = fork();

        /* error */
        if (f < 0) {
            fprintf(stderr, "%s: Could not fork, giving up: %s", name,
                    strerror(errno));

            return 3;
        }

        /* child */
        else if (f == 0) {

            /* TODO: implement catching and recording of stdin so we only read once */

            execvp(argv[optind], argv + optind);

            fprintf(stderr, "%s: Could not execute '%s', giving up: %s\n", name,
                    argv[optind], strerror(errno));

            return 4;
        }

        /* parent */
        else {

            do {
                w = waitpid(f, &status, 0);
                if (w == -1 && errno != EINTR) {
                    break;
                }
            } while (w != f);

            /* waitpid failed, we give up */
            if (w == -1) {
                status = EXIT_FAILURE;

                fprintf(stderr, "%s: waitpid for '%s' failed, giving up: %s\n", name,
                        argv[optind], strerror(errno));

                break;
            }

            /* process normal exit, should we try again? */
            else if (WIFEXITED(status)) {
                status = WEXITSTATUS(status);

                if (!should_repeat(status, repeat_until, repeat_while)) {
                    break;
                }

                fprintf(stderr, "%s: '%s' returned %d, backing off for %ld seconds and trying again...\n",
                        name, message ? message : argv[optind], status, delay);

                sleep(delay);

                continue;
            }

            /* process received a signal, we must leave cleanly */
            else if (WIFSIGNALED(status)) {
                status = WTERMSIG(status) + 128;

                break;
            }

            /* otherwise weirdness, just leave */
            else {
                status = EX_OSERR;

                break;
            }
        }

    }

    return status;
}


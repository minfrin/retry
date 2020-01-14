/**
 *    (C) 2020 Graham Leggett
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
#include <poll.h>
#include <sys/uio.h>
#include <ctype.h>

#include "config.h"

#define DEFAULT_DELAY 10
#define DEFAULT_TIMES -1

#define STDIN_FD 0
#define STDOUT_FD 1
#define STDERR_FD 2

#define READ_FD 0
#define WRITE_FD 1

#define OFFSET(X, Y)  (X * 2 + Y)

#define PUMPS 2

#define BUFFER_SIZE (100 * 1024)

static struct option long_options[] =
{
    {"delay", required_argument, NULL, 'd'},
    {"message", required_argument, NULL, 'm'},
    {"until", required_argument, NULL, 'u'},
    {"while", required_argument, NULL, 'w'},
    {"times", required_argument, NULL, 't'},
    {"help", no_argument, NULL, 'h'},
    {"version", no_argument, NULL, 'v'},
    {NULL, 0, NULL, 0}
};

typedef struct pump_t {
    void *base;
    size_t len;
    off_t offset;
    int read_closed:1;
    int write_closed:1;
    int exit_on_close:1;
    int send_eof:1;
} pump_t;

static int help(const char *name, const char *msg, int code)
{
    fprintf(code ? stderr : stdout,
            "%sUsage: %s [-v] [-h] [-u until] [-w while] command ...\n"
            "\n"
            "The tool repeats the given command until the command is successful,"
            "backing off with a configurable delay between each attempt.\n"
            "\n"
            "Retry captures stdin into memory as the data is passed to the repeated\n"
            "command, and this captured stdin is then replayed should the command\n"
            "be repeated. This makes it possible to embed the retry tool into shell\n"
            "pipelines.\n"
            "\n"
            "Retry captures stdout into memory, and if the command was successful\n"
            "stdout is passed on to stdout as normal, while if the command was\n"
            "repeated stdout is passed to stderr instead. This ensures that output\n"
            "is passed to stdout once and once only."
            "\n"
            "  -d seconds, --delay=seconds\tThe number of seconds to back off\n"
            "\t\t\t\tafter each attempt.\n"
            "  -m message, --message=message\tA message to include in the notification\n"
            "\t\t\t\twhen repeat has backed off. Defaults to the\n"
            "\t\t\t\tcommand name.\n"
            "  -t times, --times=times\tThe number of times to retry\n"
            "\t\t\t\tthe command. By default we try forever.\n"
            "  -u criteria, --until=criteria\tKeep repeating the command until any one\n"
            "\t\t\t\tof the comma separated criteria is met.\n"
            "\t\t\t\tOptions include 'success', 'true', 'fail',\n"
            "\t\t\t\t'false', an integer or a range of integers.\n"
            "\t\t\t\tDefault is 'success'.\n"
            "  -w criteria, --while=criteria\tKeep repeating the command while any one\n"
            "\t\t\t\tof the comma separated criteria is met.\n"
            "\t\t\t\tOptions include 'success', 'true', 'fail',\n"
            "\t\t\t\t'false', an integer or a range of integers.\n"
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

static int status_match(int status, const char *criteria)
{
    int len;

    while (criteria) {

        const char *next;
        char *endptr;
        long r1, r2;

        next = strchr(criteria, ',');
        if (next) {
            len = next - criteria;
        }
        else {
            len = strlen(criteria);
        }

        if (!strncmp(criteria, "success", len) ||
                !strncmp(criteria, "true", len)) {
            return ((status == 0));
        }

        if (!strncmp(criteria, "fail", len) ||
                !strncmp(criteria, "false", len)) {
            return ((status != 0));
        }

        if (!isdigit(*criteria)) {
            return -1;
        }

        r1 = strtol(criteria, &endptr, 10);
        if (!endptr || !*endptr || *endptr == ',') {
            return ((status == r1));
        }

        if (*endptr++ != '-') {
            return -1;
        }

        if (!isdigit(*endptr)) {
            return -1;
        }

        r2 = strtol(endptr, &endptr, 10);
        if (!endptr || !*endptr || *endptr == ',') {
            return ((status >= r1 && status <= r2));
        }

        if (endptr && *endptr && *endptr != ',') {
            return -1;
        }

        criteria = next;
    }

    return 1;
}

static int pump(const char *name, pump_t *pumps, struct pollfd *fds)
{
    nfds_t nfds = PUMPS * 2;
    int i;

    while (1) {
        int stay = 0;

        for (i = 0; i < PUMPS; i++) {

            fds[OFFSET(i, READ_FD)].events = 0;
            fds[OFFSET(i, WRITE_FD)].events = 0;

            if (!pumps[i].read_closed) {
                fds[OFFSET(i, READ_FD)].events = POLLIN;
                stay = 1;
            }

            if (!pumps[i].write_closed
                    && (pumps[i].send_eof || pumps[i].len > pumps[i].offset)) {
                fds[OFFSET(i, WRITE_FD)].events = POLLOUT;
                stay = 1;
            }

            if (pumps[i].read_closed && pumps[i].exit_on_close) {
                stay = 0;
                break;
            }

        }

        if (!stay) {
            break;
        }

        if (poll(fds, nfds, -1) < 0) {

            fprintf(stderr, "%s: Could not poll, giving up: %s\n", name,
                    strerror(errno));

            return -1;
        }

        for (i = 0; i < PUMPS; i++) {

            if (fds[OFFSET(i, READ_FD)].revents & POLLIN) {
                void *base;
                ssize_t num;

                base = realloc(pumps[i].base, pumps[i].len + BUFFER_SIZE);
                if (!base) {

                    fprintf(stderr, "%s: Out of memory, giving up.\n", name);

                    return -1;
                }
                pumps[i].base = base;

                num = read(fds[OFFSET(i, READ_FD)].fd, base + pumps[i].len, BUFFER_SIZE);

                if (num < 0) {

                    fprintf(stderr, "%s: Could not read, giving up: %s\n", name,
                            strerror(errno));

                    return -1;
                }
                else if (num == 0) {
                    pumps[i].read_closed = 1;
                    pumps[i].send_eof = 1;
                }
                else {
                    pumps[i].len += num;
                }

            }

            if ((fds[OFFSET(i, READ_FD)].revents & POLLHUP) ||
                    (fds[OFFSET(i, READ_FD)].revents & POLLERR) ||
                    (fds[OFFSET(i, READ_FD)].revents & POLLNVAL)) {

                pumps[i].read_closed = 1;
                pumps[i].send_eof = 1;

            }

            if (fds[OFFSET(i, WRITE_FD)].revents & POLLOUT) {
                ssize_t num;

                num = write(fds[OFFSET(i, WRITE_FD)].fd,
                        pumps[i].base + pumps[i].offset,
                        pumps[i].len - pumps[i].offset);

                if (num < 0) {

                    fprintf(stderr, "%s: Could not write, giving up: %s\n", name,
                            strerror(errno));

                    return -1;
                }
                else {
                    pumps[i].offset += num;
                }

                if (pumps[i].read_closed && pumps[i].offset == pumps[i].len) {

                    pumps[i].write_closed = 1;

                }

            }

            if ((fds[OFFSET(i, WRITE_FD)].revents & POLLERR) ||
                    (fds[OFFSET(i, WRITE_FD)].revents & POLLNVAL)) {

                pumps[i].write_closed = 1;

            }

        }

    }

    return 0;
}

int main (int argc, char **argv)
{
    const char *name = argv[0];
    const char *repeat_until = "success";
    const char *repeat_while = NULL;
    const char *message = NULL;
    char *endptr = NULL;
    pump_t pumps[PUMPS] = { 0 };
    int c, status = 0, i;
    long int delay = DEFAULT_DELAY;
    long int times = DEFAULT_TIMES;

    while ((c = getopt_long(argc, argv, "d:m:u:w:hv", long_options, NULL)) != -1) {

        switch (c)
        {
        case 'd':
            errno = 0;
            delay = strtol(optarg, &endptr, 10);

            if (errno || endptr[0] || delay < 0) {
                return help(name, "Delay must be bigger or equal to 0.\n", EXIT_FAILURE);
            }

            break;
        case 'm':
            message = optarg;

            break;
        case 't':
            errno = 0;
            times = strtol(optarg, &endptr, 10);

            if (errno || endptr[0] || times < -1) {
                return help(name, "Times must be bigger or equal to -1.\n", EXIT_FAILURE);
            }

            break;
        case 'u':
            if (status_match(0, optarg) == -1) {
                return help(name, "Until must contain comma separated numbers, ranges, 'success/true' or 'fail/false'.\n", EXIT_FAILURE);
            }
            repeat_until = optarg;
            repeat_while = NULL;

            break;
        case 'w':
            if (status_match(0, optarg) == -1) {
                return help(name, "While must contain comma separated numbers, ranges, 'success/true' or 'fail/false'.\n", EXIT_FAILURE);
            }
            repeat_until = NULL;
            repeat_while = optarg;

            break;
        case 'h':
            return help(name, NULL, 0);

        case 'v':
            return version();

        default:
            return help(name, NULL, EXIT_FAILURE);

        }

    }

    if (optind == argc) {
        return help(name, "No command specified.\n", EXIT_FAILURE);
    }

    while (times) {
        pid_t w, f;
        int inpair[2], outpair[2];

        if (pipe(inpair) || pipe(outpair)) {
            fprintf(stderr, "%s: Could not create pipe, giving up: %s", name,
                    strerror(errno));

            status = EXIT_FAILURE;
            break;
        }

        /* Clear any inherited settings */
        signal(SIGCHLD, SIG_DFL);

        f = fork();

        /* error */
        if (f < 0) {
            fprintf(stderr, "%s: Could not fork, giving up: %s", name,
                    strerror(errno));

            status = EXIT_FAILURE;
            break;
        }

        /* child */
        else if (f == 0) {

            dup2(inpair[READ_FD], STDIN_FD);
            close(inpair[READ_FD]);
            close(inpair[WRITE_FD]);
            dup2(outpair[WRITE_FD], STDOUT_FD);
            close(outpair[READ_FD]);
            close(outpair[WRITE_FD]);

            execvp(argv[optind], argv + optind);

            fprintf(stderr, "%s: Could not execute '%s', giving up: %s\n", name,
                    argv[optind], strerror(errno));

            status = EXIT_FAILURE;
            break;
        }

        /* parent */
        else {
            struct pollfd fds[PUMPS * 2];

            /* handle stdin */

            fds[OFFSET(STDIN_FD, READ_FD)].fd = STDIN_FD;
            fds[OFFSET(STDIN_FD, WRITE_FD)].fd = inpair[WRITE_FD];
            close(inpair[READ_FD]);

            /* handle stdout */

            fds[OFFSET(STDOUT_FD, READ_FD)].fd = outpair[READ_FD];
            fds[OFFSET(STDOUT_FD, WRITE_FD)].fd = STDOUT_FD;
            close(outpair[WRITE_FD]);

            /* prevent write to stdout, we will handle it later */
            pumps[STDOUT_FD].write_closed = 1;

            /* if stdout closes, the pump must exit */
            pumps[STDOUT_FD].exit_on_close = 1;

            /* pump all data */
            if (pump(name, pumps, fds)) {
                status = EXIT_FAILURE;
                break;
            }

            close(inpair[WRITE_FD]);
            close(outpair[READ_FD]);

            /* reset stdin in case we repeat the command */
            pumps[STDIN_FD].offset = 0;

            /* wait for the child process to be done */
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

                if ((repeat_until && status_match(status, repeat_until))
                        || (repeat_while && !status_match(status, repeat_while))
                        || (!repeat_until && !repeat_while && status)) {

                    /* success - write stdout to stdout */
                    fwrite(pumps[STDOUT_FD].base, pumps[STDOUT_FD].len, 1, stdout);

                    break;
                }

                /* failure - write stdout to stderr */
                fwrite(pumps[STDOUT_FD].base, pumps[STDOUT_FD].len, 1, stderr);

                if (delay) {
                    fprintf(stderr,
                            "%s: '%s' returned %d, backing off for %ld second%s and trying again...\n",
                            name, message ? message : argv[optind], status,
                            delay, delay > 1 ? "s" : "");

                    sleep(delay);
                }
                else {
                    fprintf(stderr,
                            "%s: '%s' returned %d, trying again...\n",
                            name, message ? message : argv[optind], status);
                }

                if (times > 0) {
                    times--;
                }

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

    for (i = 0; i < PUMPS; i++) {
        if (pumps[i].base) {
            free(pumps[i].base);
            pumps[i].base = NULL;
        }
    }

    return status;
}


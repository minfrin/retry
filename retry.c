/**
 *    Copyright (C) 2020 Graham Leggett <minfrin@sharp.fm>
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
    {"exponential-backoff", no_argument, NULL, 'x'},
    {"message", required_argument, NULL, 'm'},
    {"until", required_argument, NULL, 'u'},
    {"while", required_argument, NULL, 'w'},
    {"times", required_argument, NULL, 't'},
    {"help", no_argument, NULL, 'h'},
    {"version", no_argument, NULL, 'v'},
    {NULL, 0, NULL, 0}
};

typedef struct pump_t {
    struct pollfd *rpollfd;
    struct pollfd *wpollfd;
    void *base;
    size_t len;
    off_t offset;
    int rfd;
    int wfd;
    int read_closed:1;
    int write_closed:1;
    int exit_on_close:1;
    int no_close:1;
    int send_eof:1;
} pump_t;

static int help(const char *name, const char *msg, int code)
{
    const char *n;

    n = strrchr(name, '/');
    if (!n) {
        n = name;
    }
    else {
        n++;
    }

    fprintf(code ? stderr : stdout,
            "%s\n"
            "\n"
            "NAME\n"
            "  %s - Repeat command until a criteria is met, usually success.\n"
            "\n"
            "SYNOPSIS\n"
            "  %s [-v] [-h] [-u until] [-w while] command ...\n"
            "\n"
            "DESCRIPTION\n"
            "\n"
            "  The tool repeats the given command until the command is successful,\n"
            "  backing off with a configurable delay between each attempt.\n"
            "\n"
            "  Retry captures stdin into memory as the data is passed to the repeated\n"
            "  command, and this captured stdin is then replayed should the command\n"
            "  be repeated. This makes it possible to embed the retry tool into shell\n"
            "  pipelines.\n"
            "\n"
            "  Retry captures stdout into memory, and if the command was successful\n"
            "  stdout is passed on to stdout as normal, while if the command was\n"
            "  repeated stdout is passed to stderr instead. This ensures that output\n"
            "  is passed to stdout once and once only.\n"
            "\n"
            "OPTIONS\n"
            "  -d seconds, --delay=seconds  The number of seconds to back off\n"
            "    after each attempt.\n"
            "\n"
            "  -x, --exponential-backoff    Backoff exponentially after each\n"
            "    attempt, starting with the number of seconds specified by -d/--delay\n"
            "    and doubling it at each step.\n"
            "\n"
            "  -m message, --message=message  A message to include in the notification\n"
            "    when repeat has backed off. Defaults to the\n"
            "    command name.\n"
            "\n"
            "  -t times, --times=times  The number of times to retry\n"
            "    the command. By default we try forever.\n"
            "\n"
            "  -u criteria, --until=criteria  Keep repeating the command until any one\n"
            "    of the comma separated criteria is met.\n"
            "    Options include 'success', 'true', 'fail',\n"
            "    'false', an integer or a range of integers.\n"
            "    Default is 'success'.\n"
            "\n"
            "  -w criteria, --while=criteria  Keep repeating the command while any one\n"
            "    of the comma separated criteria is met.\n"
            "    Options include 'success', 'true', 'fail',\n"
            "    'false', an integer or a range of integers.\n"
            "\n"
            "  -h, --help  Display this help message.\n"
            "\n"
            "  -v, --version  Display the version number.\n"
            "\n"
            "RETURN VALUE\n"
            "  The retry tool returns the return code from the\n"
            "  command being executed, once the criteria is reached.\n"
            "\n"
            "  If the command was interrupted with a signal, the return\n"
            "  code is the signal number plus 128.\n"
            "\n"
            "  If the command could not be executed, or if the options\n"
            "  are invalid, the status 1 is returned.\n"
            "\n"
            "EXAMPLES\n"
            "  In this basic example, we repeat the command forever.\n"
            "\n"
            "\t~$ retry --until=success false\n"
            "\tretry: 'false' returned 1, backing off for 10 seconds and trying again...\n"
            "\tretry: 'false' returned 1, backing off for 10 seconds and trying again...\n"
            "\tretry: 'false' returned 1, backing off for 10 seconds and trying again...\n"
            "\t^C\n"
            "\n"
            "  In this more complex example, each invocation of curl is\n"
            "  retried until curl succeeds, at which point stdout is\n"
            "  passed once and once only to the next element in the\n"
            "  pipeline.\n"
            "\n"
            "\t~$ retry curl --fail http://localhost/entities | \\\\ \n"
            "\tjq ... | \\\\ \n"
            "\tretry curl --fail -X POST http://localhost/resource | \\\\ \n"
            "\tlogger -t resource-init\n"
            "\n"
            "AUTHOR\n"
            "  Graham Leggett <minfrin@sharp.fm>\n"
            "", msg ? msg : "", n, n);
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

static int pump(const char *name, pump_t *pumps)
{
    int i;

    while (1) {
        nfds_t nfds = 0;
        struct pollfd fds[PUMPS * 2] = { 0 };

        for (i = 0; i < PUMPS; i++) {

            pumps[i].rpollfd = NULL;
            pumps[i].wpollfd = NULL;

            if (!pumps[i].read_closed) {
                pumps[i].rpollfd = fds + nfds;
                fds[nfds].events = POLLIN;
                fds[nfds].fd = pumps[i].rfd;
                fds[nfds].revents = 0;
                nfds++;
            }

            if (!pumps[i].write_closed
                    && (pumps[i].send_eof || pumps[i].len > pumps[i].offset)) {
                pumps[i].wpollfd = fds + nfds;
                fds[nfds].events = POLLOUT;
                fds[nfds].fd = pumps[i].wfd;
                fds[nfds].revents = 0;
                nfds++;
            }

            if (pumps[i].read_closed && pumps[i].exit_on_close) {
                nfds = 0;
                break;
            }

        }

        if (!nfds) {
            break;
        }

        if (poll(fds, nfds, -1) < 0) {

            fprintf(stderr, "%s: Could not poll, giving up: %s\n", name,
                    strerror(errno));

            return -1;
        }

        for (i = 0; i < PUMPS; i++) {

            if (pumps[i].rpollfd) {

                if (pumps[i].rpollfd->revents & POLLIN) {
                    void *base;
                    ssize_t num;

                    base = realloc(pumps[i].base, pumps[i].len + BUFFER_SIZE);
                    if (!base) {

                        fprintf(stderr, "%s: Out of memory, giving up.\n",
                                name);

                        return -1;
                    }
                    pumps[i].base = base;

                    num = read(pumps[i].rfd, base + pumps[i].len, BUFFER_SIZE);

                    if (num < 0) {

                        fprintf(stderr, "%s: Could not read, giving up: %s\n",
                                name, strerror(errno));

                        return -1;
                    } else if (num == 0) {
                        pumps[i].read_closed = 1;
                        pumps[i].send_eof = 1;
                        close(pumps[i].rfd);
                    } else {
                        pumps[i].len += num;
                    }

                }

                if ((pumps[i].rpollfd->revents & POLLHUP)
                        || (pumps[i].rpollfd->revents & POLLERR)
                        || (pumps[i].rpollfd->revents & POLLNVAL)) {

                    pumps[i].read_closed = 1;
                    pumps[i].send_eof = 1;
                    close(pumps[i].rfd);

                }

            }

            if (pumps[i].wpollfd) {

                if (pumps[i].wpollfd->revents & POLLOUT) {
                    ssize_t num;

                    num = write(pumps[i].wfd, pumps[i].base + pumps[i].offset,
                            pumps[i].len - pumps[i].offset);

                    if (num < 0) {

                        fprintf(stderr, "%s: Could not write, giving up: %s\n",
                                name, strerror(errno));

                        return -1;
                    } else {
                        pumps[i].offset += num;
                    }

                    if (pumps[i].read_closed
                            && pumps[i].offset == pumps[i].len) {

                        pumps[i].write_closed = 1;
                        close(pumps[i].wfd);

                    }

                }

                if ((pumps[i].wpollfd->revents & POLLHUP)
                        || (pumps[i].wpollfd->revents & POLLERR)
                        || (pumps[i].wpollfd->revents & POLLNVAL)) {

                    pumps[i].write_closed = 1;
                    close(pumps[i].wfd);

                }

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
    int exponential = 0;
    long int times = DEFAULT_TIMES;

    while ((c = getopt_long(argc, argv, "d:xm:t:u:w:hv", long_options, NULL)) != -1) {

        switch (c)
        {
        case 'd':
            errno = 0;
            delay = strtol(optarg, &endptr, 10);

            if (errno || endptr[0] || delay < 0) {
                return help(name, "Delay must be bigger or equal to 0.\n", EXIT_FAILURE);
            }

            break;
        case 'x':
            exponential = 1;

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

            /* handle stdin */

            pumps[STDIN_FD].rfd = dup(STDIN_FD);
            pumps[STDIN_FD].wfd = inpair[WRITE_FD];
            close(inpair[READ_FD]);

            /* handle stdout */

            pumps[STDOUT_FD].rfd = outpair[READ_FD];
            pumps[STDOUT_FD].wfd = dup(STDOUT_FD);
            close(outpair[WRITE_FD]);

            /* prevent write to stdout, we will handle it later */
            pumps[STDOUT_FD].write_closed = 1;

            /* if stdout closes, the pump must exit */
            pumps[STDOUT_FD].exit_on_close = 1;

            /* pump all data */
            if (pump(name, pumps)) {
                status = EXIT_FAILURE;
                break;
            }

            close(inpair[WRITE_FD]);
            close(outpair[READ_FD]);
            close(pumps[STDIN_FD].rfd);
            close(pumps[STDOUT_FD].wfd);

            /* reset stdin in case we repeat the command */
            pumps[STDIN_FD].offset = 0;
            pumps[STDIN_FD].write_closed = 0;

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

                /* reset stdout for a go-around */
                free(pumps[STDOUT_FD].base);
                memset(&pumps[STDOUT_FD], 0, sizeof(pump_t));

                if (delay) {
                    fprintf(stderr,
                            "%s: %s returned %d, backing off for %ld second%s and trying again...\n",
                            name, message ? message : argv[optind], status,
                            delay, delay > 1 ? "s" : "");

                    sleep(delay);
                    if (exponential) {
                        delay = delay * 2;
                    }
                }
                else {
                    fprintf(stderr,
                            "%s: %s returned %d, trying again...\n",
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


#define WLR_USE_UNSTABLE
#define _POSIX_C_SOURCE 200809L

#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "nano-compositor.h"

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [options]\n", prog);
    fprintf(stderr, "  -s, --startup-cmd <cmd>  Command to run after compositor starts\n");
    fprintf(stderr, "  -h, --help               Show this help message\n");
}

int main(int argc, char *argv[]) {
    const char *startup_cmd = NULL;

    static struct option long_options[] = {
        {"startup-cmd", required_argument, NULL, 's'},
        {"help",        no_argument,       NULL, 'h'},
        {0, 0, 0, 0},
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "s:h", long_options, NULL)) != -1) {
        switch (opt) {
        case 's':
            startup_cmd = optarg;
            break;
        case 'h':
            print_usage(argv[0]);
            return 0;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    wlr_log_init(WLR_DEBUG, NULL);

    struct nano_server server = {0};
    if (!nano_server_init(&server)) {
        wlr_log(WLR_ERROR, "Failed to initialize nano-compositor");
        return 1;
    }

    if (startup_cmd) {
        pid_t pid = fork();
        if (pid < 0) {
            wlr_log(WLR_ERROR, "Failed to fork for startup command");
        } else if (pid == 0) {
            execl("/bin/sh", "/bin/sh", "-c", startup_cmd, (char *)NULL);
            _exit(127);
        } else {
            wlr_log(WLR_INFO, "Started startup command '%s' with pid %d",
                startup_cmd, pid);
        }
    }

    nano_server_run(&server);
    nano_server_destroy(&server);

    return 0;
}

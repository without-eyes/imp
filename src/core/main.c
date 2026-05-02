#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <getopt.h>
#include "core/imp.h"
#include "cli/dashboard.h"

// Project Metadata
#define IMP_VERSION "v1.0.0"
#define AUTHOR_NAME "without eyes"

// Command Line Configuration
#define SHORT_OPTIONS "dihv"

static void print_help() {
    printf("I.M.P. %s by %s\n\n", IMP_VERSION, AUTHOR_NAME);
    printf("Usage: imp [OPTIONS]\n\n");
    printf("Options:\n");
    printf("  -d, --daemon        Start daemon mode\n");
    printf("  -i, --interactible  Start interactive dashboard\n");
    printf("  -h, --help          Show this manual\n");
    printf("  -v, --version       Show version information\n");
}

int main(int argc, char *argv[]) {
    if (argc == 1) {
        print_help();
        return EXIT_SUCCESS;
    }

    bool run_daemon = false;
    bool run_ui = false;

    int opt;
    static struct option long_options[] = {
        {"daemon",       no_argument, 0, 'd'},
        {"interactible", no_argument, 0, 'i'},
        {"help",         no_argument, 0, 'h'},
        {"version",      no_argument, 0, 'v'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, SHORT_OPTIONS, long_options, NULL)) != -1) {
        switch (opt) {
            case 'd':
                run_daemon = true;
                break;
            
            case 'i':
                run_ui = true;
                break;
            
            case 'v':
                printf("I.M.P. %s\n", IMP_VERSION);
                return EXIT_SUCCESS;
            
            case 'h':
            default:
                print_help();
                return (opt == 'h') ? EXIT_SUCCESS : EXIT_FAILURE;
        }
    }

    if (run_daemon && run_ui) {
        fprintf(stderr, "Error: Cannot run both daemon (-d) and interactive (-i) mode in the same process.\n");
        return EXIT_FAILURE;
    }

    if (run_daemon) {
        imp_daemonize();
        imp_run();
        imp_cleanup();
    } else if (run_ui) {
        run_interactive_dashboard();
    }

    return EXIT_SUCCESS;
}
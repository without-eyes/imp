#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <getopt.h>
#include "core/imp.h"
#include "cli/dashboard.h"

#define IMP_VERSION "v1.0.0"

static void print_help() {
    printf("I.M.P. %s by without eyes\n\n", IMP_VERSION);
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
        return 0;
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

    while ((opt = getopt_long(argc, argv, "dihv", long_options, NULL)) != -1) {
        switch (opt) {
            case 'd':
                run_daemon = true;
                break;
            
            case 'i':
                run_ui = true;
                break;
            
            case 'v':
                printf("I.M.P. %s\n", IMP_VERSION);
                return 0;
            
            case 'h':
            default:
                print_help();
                return (opt == 'h') ? 0 : 1;
        }
    }

    if (run_daemon && run_ui) {
        printf("Error: Cannot run both daemon (-d) and interactive (-i) mode in the same process.\n");
        return 1;
    }

    if (run_daemon) {
        imp_daemonize();
        imp_run();
        imp_cleanup();
    } else if (run_ui) {
        run_interactive_dashboard();
    }

    return 0;
}
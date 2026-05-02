# I.M.P.
**Intelligent Management & Protection Daemon**

![Version](https://img.shields.io/badge/version-1.0.0-blue.svg)
![Platform](https://img.shields.io/badge/platform-Linux-lightgrey.svg)
![Language](https://img.shields.io/badge/language-C-green.svg)
![License](https://img.shields.io/badge/license-MIT-orange.svg)

I.M.P. is a lightweight, high-performance Linux system daemon written in POSIX C. It provides modular, real-time system monitoring, resource prioritization, automated cleanup, and proactive network security scanning.

The system is split into a robust Core engine and dynamically loaded shared object (`.so`) modules, communicating via a centralized Unix Domain Socket IPC broker. It also features a zero-dependency, live Terminal Dashboard for real-time telemetry.

## Table of Contents

- [Features](#features)
- [Architecture](#architecture)
- [Directory Structure](#directory-structure)
- [Prerequisites](#prerequisites)
- [Build & Installation](#build--installation)
- [Usage](#usage)
- [Configuration](#configuration)
- [Testing](#testing)
- [Acknowledgements](#acknowledgements)
- [License](#license)

## Features

*   **Prioritizer Module:** Automatically throttles RAM-heavy processes and boosts the CPU priority (`nice` values) of mission-critical applications.
*   **Memory Module:** Monitors disk partitions for critical usage thresholds and automatically cleans up old log/temp files based on wildcard masks and age limits.
*   **Security Module:** Scans critical system files (e.g., `/etc/shadow`, `/etc/sudoers`) for insecure permissions and monitors the TCP stack directly via `/proc/net/tcp` for unauthorized active connections (e.g., unexpected SSH sessions).
*   **Live Dashboard:** An interactive, `ncurses`-free Terminal UI that visualizes CPU, RAM, and Swap usage, while streaming live IPC event logs from the daemon.

## Architecture

I.M.P. relies on a strict parent-child Supervisor architecture:

1.  **Core Supervisor:** Reads the JSON configuration, daemonizes the process, and spawns the IPC Broker thread.
2.  **Dynamic Modules:** The Core uses `dlopen` to load `imp_*.so` modules at runtime. Each module is forked into its own isolated process.
3.  **Crash Recovery:** If a module process crashes, the Core Supervisor intercepts the `SIGCHLD` signal and automatically restarts the module (up to a configured maximum crash limit).

## Directory Structure
```text
imp/
├── config/           # Daemon and module configs
├── include/          # Public header files (*.h)
├── modules/          # Pluggable system modules (Memory, Security, Prioritizer)
├── src/
│   ├── cli/          # Dashboard and UI logic
│   ├── core/         # Daemon core, supervisor, and module loader 
│   └── utils/        # Shared utilities (Logger, IPC, JSON parser)
├── tests/            # Google Test suites for all components
├── Makefile          # Build system configuration
├── install.sh        # System installation script
└── uninstall.sh      # System removal script
```

## Prerequisites

*   GCC or Clang
*   GNU Make
*   Linux Kernel (relies heavily on `/proc` and `sysfs` filesystems)
*   Standard POSIX utilities

## Build & Installation

Clone the repository and use the provided scripts to build and install the daemon to your system:
```bash
git clone [https://github.com/yourusername/imp.git](https://github.com/yourusername/imp.git)
cd imp
./install.sh
```

To cleanly remove I.M.P. and its associated services from your system, use:
```bash
./uninstall.sh
```

## Usage

I.M.P. provides a single binary that handles both the background daemon and the interactive UI.

```bash
# Start the daemon in the background
sudo imp --daemon

# Launch the live interactive dashboard
sudo imp --interactible

# Display the help manual
imp --help

# Display version information
imp --version
```

## Configuration

I.M.P. is configured via a single JSON file, typically located at `/etc/imp/imp.json` and [`config/default.conf`](config/default.conf). Changes to this file require a daemon restart.
```json
{
  "log_level": "INFO",
  "log_file": "/var/log/imp/imp.log",
  "pid_file": "/var/run/imp.pid",
  "modules": [
    {
      "name": "Prioritizer",
      "enabled": true,
      "so_path": "/usr/lib/imp/imp_prioritizer.so",
      "config": {
        "check_interval_sec": 10,
        "ram_threshold_mb": 1024,
        "important_processes": ["sshd", "nginx", "imp"]
      }
    },
    {
      "name": "Memory",
      "enabled": true,
      "so_path": "/usr/lib/imp/imp_memory.so",
      "config": {
        "check_interval_sec": 3600,
        "critical_disk_usage_percent": 90,
        "targets": [
          { "path": "/var/log", "mask": "*.log", "max_age_days": 30 }
        ]
      }
    }
  ]
}
```

## Testing

I.M.P. maintains a strict testing standard to ensure absolute stability at the system level. The project includes a comprehensive suite of unit and integration tests built with Google Test (`gtest`), achieving near 100% coverage across core logic, IPC, and dynamic modules.

To compile and run the test suite:
```bash
make test
```

## Acknowledgements

* **[cJSON](https://github.com/DaveGamble/cJSON):** Used for the ultra-lightweight, fast parsing of the daemon's main configuration files and IPC event messages. Its single-file architecture fits perfectly within I.M.P.'s zero-dependency philosophy.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
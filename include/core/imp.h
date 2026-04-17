#ifndef IMP_H
#define IMP_H

#define CONFIG_PATH "config/default.conf"
#define CONFIG_MAX_SIZE 2048

void imp_daemonize(void);

int imp_run(void);

int imp_cleanup(void);

#endif // IMP_H
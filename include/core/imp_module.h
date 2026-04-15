#ifndef IMP_MODULE_H
#define IMP_MODULE_H

#define IMP_MODULE_SYM "imp_module_export"

typedef struct {
    const char* name;
    const char* version;

    int  (*init)(const char* jsonConfig); 
    void (*run)(void);
    void (*cleanup)(void);
} ImpModule;

#endif // IMP_MODULE_H
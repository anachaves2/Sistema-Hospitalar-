#ifndef LOG_H
#define LOG_H

#include <stdio.h>

// Severidades de log (1=mais crítico, 5=debug)
typedef enum {
    LOG_CRITICAL = 1,
    LOG_ERROR    = 2,
    LOG_WARNING  = 3,
    LOG_INFO     = 4,
    LOG_DEBUG    = 5
} log_severity_t;

// Inicializa o logger (abre ficheiro e inicializa o mutex)
int  log_init(const char *log_path);

// Fechar logger
void log_close(void);

// Evento principal (thread-safe)
void log_event(log_severity_t severity,
               const char *component,
               const char *event_type,
               const char *details);

// Converter severidade para texto
const char *log_severity_to_string(log_severity_t s);

#endif

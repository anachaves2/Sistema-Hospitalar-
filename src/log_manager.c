#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>

#include "log.h"

// Módulo de logging centralizado.
// Fornece registo thread-safe de eventos do sistema para ficheiro,
// com severidades definidas no enunciado.

// Ficheiro de log e mutex global
static FILE *log_file = NULL;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

// Converter severidade para string
const char *log_severity_to_string(log_severity_t s) {
    switch (s) {
        case LOG_CRITICAL: return "CRITICAL";
        case LOG_ERROR:    return "ERROR";
        case LOG_WARNING:  return "WARNING";
        case LOG_INFO:     return "INFO";
        case LOG_DEBUG:    return "DEBUG";
        default:           return "UNKNOWN";
    }
}

// Inicializar logger (thread-safe)
int log_init(const char *log_path) {
    pthread_mutex_lock(&log_mutex);

    log_file = fopen(log_path, "a");
    // Se o logger não estiver inicializado, ignora silenciosamente o evento
    if (!log_file) {
        fprintf(stderr, "[LOG] Erro ao abrir ficheiro de log (%s): %s\n",
                log_path, strerror(errno));
        pthread_mutex_unlock(&log_mutex);
        return -1;
    }

    time_t now = time(NULL);
    fprintf(log_file,
            "================ LOG START (%s) ================\n",
            ctime(&now));
    fflush(log_file);

    pthread_mutex_unlock(&log_mutex);
    return 0;
}

// Fechar logger
void log_close(void) {
    pthread_mutex_lock(&log_mutex);

    if (log_file) {
        time_t now = time(NULL);
        fprintf(log_file,
                "================ LOG END (%s) ==================\n",
                ctime(&now));
        fclose(log_file);
        log_file = NULL;
    }

    pthread_mutex_unlock(&log_mutex);
}

// Registar evento (thread-safe)
void log_event(log_severity_t severity,
               const char *component,
               const char *event_type,
               const char *details) {
    if (!log_file) return;

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    char timestamp[32];
    strftime(timestamp, sizeof(timestamp),
             "%Y-%m-%d %H:%M:%S", &tm_now);

    pthread_mutex_lock(&log_mutex);

    fprintf(log_file,
            "[%s] [%s] [%s] [%s] %s\n",
            timestamp,
            component,
            log_severity_to_string(severity),
            event_type,
            details ? details : "");

    fflush(log_file);
    pthread_mutex_unlock(&log_mutex);
}

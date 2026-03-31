#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <semaphore.h>
#include <fcntl.h>
#include <errno.h>

#include "hospital.h"
#include "ipc.h"
#include "sync.h"
#include "config.h"
#include "stats.h"
#include "log.h"

// Processo PHARMACY: recebe pedidos via named pipe (pharmacy_pipe),
// simula o tempo de preparação (limitado por semáforo) e notifica o SYSTEM
// através da fila RESPONSES quando a medicação está pronta.

static void safe_copy(char *dst, size_t dstsz, const char *src) {
    if (!dst || dstsz == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    (void)snprintf(dst, dstsz, "%.*s", (int)dstsz - 1, src);
}

static int parse_pharmacy_pipe_line(const char *line,
                                    int *urgent,
                                    int *operation_id,
                                    char *patient_id, size_t patient_sz,
                                    char *med, size_t med_sz)
{
    // "PHARMACY_REQUEST urgent=%d op=%d patient=%15s med=%63s"
    int u=0, op=0;
    char pid[64]={0}, m[64]={0};

    int n = sscanf(line,
                   "PHARMACY_REQUEST urgent=%d op=%d patient=%63s med=%63s",
                   &u, &op, pid, m);

    if (n < 3) return -1;

    *urgent = u;
    *operation_id = op;
    snprintf(patient_id, patient_sz, "%s", pid);

    if (n < 4 || m[0] == '\0') snprintf(med, med_sz, "GENERIC_MED");
    else snprintf(med, med_sz, "%s", m);

    return 0;
}

void pharmacy_main_loop(void) {
    printf("[PHARMACY] Processo iniciado (PID=%d)\n", getpid());
    log_event(LOG_INFO, "PHARMACY", "START", "pharmacy_main_loop iniciado");

    int fd = open("pharmacy_pipe", O_RDWR);
    if (fd == -1) {
        perror("[PHARMACY] open pharmacy_pipe");
        log_event(LOG_ERROR, "PHARMACY", "PIPE_OPEN_FAIL", strerror(errno));
        return;
    }

    char line[512];
    size_t line_len = 0;

    while (1) {
        char c;
        // Leitura incremental (char a char) para reconstruir mensagens terminadas em '\n'
        // sem depender de tamanhos fixos de read().
        ssize_t n = read(fd, &c, 1);

        if (n < 0) {
            if (errno == EINTR) continue;
            perror("[PHARMACY] read pharmacy_pipe");
            log_event(LOG_ERROR, "PHARMACY", "PIPE_READ_FAIL", strerror(errno));
            break;
        }
        if (n == 0) continue;

        if (c != '\n') {
            if (line_len + 1 < sizeof(line)) line[line_len++] = c;
            continue;
        }

        // linha completa
        line[line_len] = '\0';
        line_len = 0;

        if (strcmp(line, "SHUTDOWN") == 0) {
            log_event(LOG_INFO, "PHARMACY", "SHUTDOWN", "shutdown recebido (pipe)");
            break;
        }

        int urgent=0, op=0;
        char patient_id[MAX_PATIENT_ID_LENGTH];
        char med[64];

        if (parse_pharmacy_pipe_line(line, &urgent, &op, patient_id, sizeof(patient_id), med, sizeof(med)) != 0) {
            log_event(LOG_WARNING, "PHARMACY", "PIPE_BAD_LINE", line);
            continue;
        }

        // Limita o nº de preparações em paralelo (recurso partilhado da farmácia)
        if (g_sem_pharmacy) sem_wait(g_sem_pharmacy);

        // duração preparação
        int prep_ms = 1000;
        if (g_config.pharmacy_preparation_time_min > 0 &&
            g_config.pharmacy_preparation_time_max >= g_config.pharmacy_preparation_time_min) {
            int min = g_config.pharmacy_preparation_time_min;
            int max = g_config.pharmacy_preparation_time_max;
            int dur = min + (rand() % (max - min + 1));
            prep_ms = dur * g_config.time_unit_ms;
        }
        usleep(prep_ms * 1000);

        stats_inc_pharmacy_request(urgent ? 1 : 0);

        // resposta ao SYSTEM via RESPONSES
        hospital_message_t resp;
        memset(&resp, 0, sizeof(resp));
        resp.mtype    = 2;
        resp.msg_type = MSG_PHARMACY_READY;

        safe_copy(resp.source, sizeof(resp.source), "PHARMACY");
        safe_copy(resp.target, sizeof(resp.target), "SYSTEM");
        safe_copy(resp.patient_id, sizeof(resp.patient_id), patient_id);

        resp.operation_id = op;
        resp.timestamp    = time(NULL);

        snprintf(resp.data, sizeof(resp.data),
                 "PHARMACY_READY patient=%s op=%d med=%s urgent=%d",
                 resp.patient_id, resp.operation_id, med, urgent);

        send_to_responses(&resp);
        log_event(LOG_INFO, "PHARMACY", "READY", resp.data);

        if (g_sem_pharmacy) sem_post(g_sem_pharmacy);
    }

    close(fd);
    printf("[PHARMACY] A terminar.\n");
    log_event(LOG_INFO, "PHARMACY", "STOP", "pharmacy_main_loop terminou");
}

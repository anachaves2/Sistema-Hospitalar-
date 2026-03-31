#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>

#include "hospital.h"
#include "ipc.h"
#include "sync.h"
#include "config.h"
#include "stats.h"
#include "log.h"

// Processo LAB: recebe pedidos via named pipe (lab_pipe), limita a execução com semáforos
// por laboratório (Lab1/Lab2), simula a duração dos testes e notifica o SYSTEM através
// da fila RESPONSES quando os resultados estão prontos.

static void safe_copy(char *dst, size_t dstsz, const char *src) {
    if (!dst || dstsz == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    (void)snprintf(dst, dstsz, "%.*s", (int)dstsz - 1, src);
}

static int parse_lab_pipe_line(const char *line, int *urgent, int *operation_id, char *patient_id, size_t patient_sz, int *lab_choice)
{
    // "LAB_REQUEST urgent=%d op=%d patient=%63s lab=%d"
    int u=0, op=0, lab=1;
    char pid[64]={0};

    int n = sscanf(line, "LAB_REQUEST urgent=%d op=%d patient=%63s lab=%d", &u, &op, pid, &lab);
    if (n < 3) return -1;

    if (lab != 1 && lab != 2) lab = 1;

    *urgent = u;
    *operation_id = op;
    *lab_choice = lab;
    snprintf(patient_id, patient_sz, "%s", pid);
    return 0;
}

void laboratory_main_loop(void) {
    printf("[LAB] Processo iniciado (PID=%d)\n", getpid());
    log_event(LOG_INFO, "LAB", "START", "laboratory_main_loop iniciado");

    int fd = open("lab_pipe", O_RDWR);
    if (fd == -1) {
        perror("[LAB] open lab_pipe");
        log_event(LOG_ERROR, "LAB", "PIPE_OPEN_FAIL", strerror(errno));
        return;
    }

    char line[512];
    size_t line_len = 0;

    while (1) {
        char c;
        // Leitura incremental (char a char) para reconstruir pedidos terminados em '\n'
        ssize_t n = read(fd, &c, 1);

        if (n < 0) {
            if (errno == EINTR) continue;
            perror("[LAB] read lab_pipe");
            log_event(LOG_ERROR, "LAB", "PIPE_READ_FAIL", strerror(errno));
            break;
        }
        if (n == 0) continue;

        if (c != '\n') {
            if (line_len + 1 < sizeof(line)) line[line_len++] = c;
            continue;
        }

        line[line_len] = '\0';
        line_len = 0;

        if (strcmp(line, "SHUTDOWN") == 0) {
            log_event(LOG_INFO, "LAB", "SHUTDOWN", "shutdown recebido (pipe)");
            break;
        }

        int urgent=0, op=0, lab=1;
        char patient_id[MAX_PATIENT_ID_LENGTH];

        if (parse_lab_pipe_line(line, &urgent, &op, patient_id, sizeof(patient_id), &lab) != 0) {
            log_event(LOG_WARNING, "LAB", "PIPE_BAD_LINE", line);
            continue;
        }

        // Recurso partilhado: limita o nº de testes simultâneos em cada laboratório
        if (lab == 1 && g_sem_lab1) sem_wait(g_sem_lab1);
        if (lab == 2 && g_sem_lab2) sem_wait(g_sem_lab2);

        // duração
        int dur_units = 2;
        if (lab == 1 &&
            g_config.lab1_test_min_duration > 0 &&
            g_config.lab1_test_max_duration >= g_config.lab1_test_min_duration) {
            int min = g_config.lab1_test_min_duration;
            int max = g_config.lab1_test_max_duration;
            dur_units = min + (rand() % (max - min + 1));
        } else if (lab == 2 &&
                   g_config.lab2_test_min_duration > 0 &&
                   g_config.lab2_test_max_duration >= g_config.lab2_test_min_duration) {
            int min = g_config.lab2_test_min_duration;
            int max = g_config.lab2_test_max_duration;
            dur_units = min + (rand() % (max - min + 1));
        }
        usleep(dur_units * g_config.time_unit_ms * 1000);

        // stats
        if (g_stats_shm) {
            pthread_mutex_lock(&g_stats_shm->mutex);
            if (lab == 1) g_stats_shm->total_lab_tests_lab1++;
            else g_stats_shm->total_lab_tests_lab2++;
            pthread_mutex_unlock(&g_stats_shm->mutex);
        }

        stats_inc_lab_request(urgent ? 1 : 0);
        stats_add_lab_turnaround((double)dur_units);

        // resposta
        hospital_message_t resp;
        memset(&resp, 0, sizeof(resp));
        resp.mtype    = 2;
        resp.msg_type = MSG_LAB_RESULTS_READY;

        safe_copy(resp.source, sizeof(resp.source), "LAB");
        safe_copy(resp.target, sizeof(resp.target), "SYSTEM");
        safe_copy(resp.patient_id, sizeof(resp.patient_id), patient_id);

        resp.operation_id = op;
        resp.timestamp    = time(NULL);

        snprintf(resp.data, sizeof(resp.data),
                 "LAB_RESULTS_READY patient=%s op=%d lab=%d duration_ut=%d urgent=%d",
                 resp.patient_id, resp.operation_id, lab, dur_units, urgent);

        send_to_responses(&resp);
        log_event(LOG_INFO, "LAB", "RESULT_READY", resp.data);

        if (lab == 1 && g_sem_lab1) sem_post(g_sem_lab1);
        if (lab == 2 && g_sem_lab2) sem_post(g_sem_lab2);
    }

    close(fd);
    printf("[LAB] A terminar.\n");
    log_event(LOG_INFO, "LAB", "STOP", "laboratory_main_loop terminou");
}

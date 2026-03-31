#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <semaphore.h>
#include <fcntl.h>
#include <errno.h>

#include "ipc.h"
#include "hospital.h"
#include "sync.h"
#include "config.h"
#include "stats.h"
#include "log.h"

// Processo SURGERY: recebe transferências via named pipe (surgery_pipe),
// reserva recursos (equipa médica + sala BO), solicita dependências (LAB/FARMÁCIA)
// e executa a cirurgia simulando tempos configuráveis. No fim, notifica o SYSTEM
// através da fila RESPONSES.

static void safe_copy(char *dst, size_t dstsz, const char *src) {
    if (!dst || dstsz == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    (void)snprintf(dst, dstsz, "%.*s", (int)dstsz - 1, src);
}

static int rand_between(int a, int b) {
    if (b < a) { int t=a; a=b; b=t; }
    if (a == b) return a;
    return a + (rand() % (b - a + 1));
}

static void bo_duration_bounds(int bo_idx, int *min_out, int *max_out) {
    if (bo_idx == 0) { *min_out = g_config.bo1_min_duration; *max_out = g_config.bo1_max_duration; return; }
    if (bo_idx == 1) { *min_out = g_config.bo2_min_duration; *max_out = g_config.bo2_max_duration; return; }
    *min_out = g_config.bo3_min_duration; *max_out = g_config.bo3_max_duration;
}

static int parse_surgery_pipe_line(const char *line, int *urgent, int *operation_id, char *patient_id, size_t patient_sz)
{
    // "SURGERY_TRANSFER urgent=%d op=%d patient=%15s"
    int u=0, op=0;
    char pid[64]={0};

    int n = sscanf(line, "SURGERY_TRANSFER urgent=%d op=%d patient=%63s", &u, &op, pid);
    if (n < 3) return -1;

    *urgent = u;
    *operation_id = op;
    snprintf(patient_id, patient_sz, "%s", pid);
    return 0;
}

// Aguarda resultados de LAB e/ou PHARMACY para um paciente/operação específicos.
// Se o caso for urgente, tenta consumir primeiro mensagens URGENT para reduzir latência;
// caso contrário bloqueia na fila NORMAL.

static int wait_for_dependency(int need_lab, int need_pharm, int op, const char *patient, int urgent) {
    // Assume-se que mensagens para SURGERY são relevantes para operações em curso; outras são ignoradas.
    int got_lab = (need_lab ? 0 : 1);
    int got_ph  = (need_pharm ? 0 : 1);

    hospital_message_t m;

    printf("[SURGERY] A aguardar deps: LAB=%d PHARM=%d (patient=%s op=%d)\n",
           need_lab, need_pharm, patient, op);
    fflush(stdout);

    while (!(got_lab && got_ph)) {

        // tenta primeiro URGENT se for urgente, senão NORMAL
        int rc;
        if (urgent) {
            rc = receive_from_urgent_nowait(&m);
            if (rc == 1) rc = receive_from_normal(&m); // bloqueia no NORMAL
            else if (rc == -1) rc = receive_from_normal(&m);
        } else {
            rc = receive_from_normal(&m); // bloqueia
        }

        if (rc != 0) continue;

        // filtra target
        if (strcmp(m.target, "SURGERY") != 0 && strcmp(m.target, "ALL") != 0) {
            continue;
        }

        // filtra paciente/op
        if (strcmp(m.patient_id, patient) != 0) continue;
        if (m.operation_id != op) continue;

        if (m.msg_type == MSG_LAB_RESULTS_READY) {
            got_lab = 1;
            printf("[SURGERY] OK: PREOP/LAB recebido (%s)\n", m.data);
            fflush(stdout);
        } else if (m.msg_type == MSG_PHARMACY_READY) {
            got_ph = 1;
            printf("[SURGERY] OK: FARMACIA recebida (%s)\n", m.data);
            fflush(stdout);
        } else {
            // outras msgs para surgery
        }
    }

    printf("[SURGERY] Dependencias OK: LAB=%d PHARM=%d (patient=%s op=%d)\n",
           got_lab, got_ph, patient, op);
    fflush(stdout);

    return 0;
}

static void send_pharmacy_request_pipe(int urgent, int op, const char *patient, const char *med) {
    char line[256];
    snprintf(line, sizeof(line),
             "PHARMACY_REQUEST urgent=%d op=%d patient=%s med=%s",
             urgent, op, patient, (med && *med) ? med : "GENERIC_MED");
    if (fifo_write_line("pharmacy_pipe", line) == -1) {
        log_event(LOG_ERROR, "SURGERY", "PHARM_PIPE_FAIL", strerror(errno));
    } else {
        log_event(LOG_INFO, "SURGERY", "PHARM_PIPE_REQ", line);
    }
}

static void send_lab_request_pipe(int urgent, int op, const char *patient, int lab_choice) {
    char line[256];
    snprintf(line, sizeof(line),
             "LAB_REQUEST urgent=%d op=%d patient=%s lab=%d",
             urgent, op, patient, lab_choice);
    if (fifo_write_line("lab_pipe", line) == -1) {
        log_event(LOG_ERROR, "SURGERY", "LAB_PIPE_FAIL", strerror(errno));
    } else {
        log_event(LOG_INFO, "SURGERY", "LAB_PIPE_REQ", line);
    }
}

void surgery_main_loop(void) {
    printf("[SURGERY] Processo iniciado (PID=%d)\n", getpid());
    fflush(stdout);
    log_event(LOG_INFO, "SURGERY", "START", "surgery_main_loop iniciado");

    int fd = open("surgery_pipe", O_RDWR);
    if (fd == -1) {
        perror("[SURGERY] open surgery_pipe");
        log_event(LOG_ERROR, "SURGERY", "PIPE_OPEN_FAIL", strerror(errno));
        return;
    }

    printf("[SURGERY] Ligado ao surgery_pipe (fd=%d)\n", fd);
    fflush(stdout);

    char line[512];
    size_t line_len = 0;

    while (1) {
        char c;
        ssize_t n = read(fd, &c, 1);

        if (n < 0) {
            if (errno == EINTR) continue;
            perror("[SURGERY] read surgery_pipe");
            log_event(LOG_ERROR, "SURGERY", "PIPE_READ_FAIL", strerror(errno));
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
            printf("[SURGERY] SHUTDOWN recebido (pipe)\n");
            fflush(stdout);
            log_event(LOG_INFO, "SURGERY", "SHUTDOWN", "shutdown recebido (pipe)");
            break;
        }

        int urgent=0, op=0;
        char patient_id[MAX_PATIENT_ID_LENGTH];

        if (parse_surgery_pipe_line(line, &urgent, &op, patient_id, sizeof(patient_id)) != 0) {
            printf("[SURGERY] Linha inválida no pipe: '%s'\n", line);
            fflush(stdout);
            log_event(LOG_WARNING, "SURGERY", "PIPE_BAD_LINE", line);
            continue;
        }

        // Print de entrada
        printf("[SURGERY] REQUEST transfer patient=%s op=%d urgent=%d\n", patient_id, op, urgent);
        fflush(stdout);

        // log
        {
            char details[256];
            snprintf(details, sizeof(details), "transfer patient=%s op=%d urgent=%d", patient_id, op, urgent);
            log_event(LOG_INFO, "SURGERY", "REQUEST", details);
        }

        // equipa médica
        if (g_sem_medical_teams) {
            printf("[SURGERY] A aguardar equipa médica...\n");
            fflush(stdout);
            sem_wait(g_sem_medical_teams);
            printf("[SURGERY] Equipa médica adquirida.\n");
            fflush(stdout);
        }

        // Atribuição de sala:
        // tenta BO1/BO2 de forma não bloqueante (trywait). Se ambos ocupados, bloqueia em BO3.
        // Estratégia simples para evitar espera ativa e garantir progresso.
        sem_t *bo_sem = NULL;
        int bo_idx = -1;
        const char *bo_name = NULL;

        if (g_sem_bo1 && sem_trywait(g_sem_bo1) == 0) {
            bo_sem = g_sem_bo1; bo_idx = 0; bo_name = "BO1";
        } else if (g_sem_bo2 && sem_trywait(g_sem_bo2) == 0) {
            bo_sem = g_sem_bo2; bo_idx = 1; bo_name = "BO2";
        } else {
            if (g_sem_bo3) sem_wait(g_sem_bo3);
            bo_sem = g_sem_bo3; bo_idx = 2; bo_name = "BO3";
        }

        printf("[SURGERY] Sala atribuída: %s (patient=%s)\n", bo_name, patient_id);
        fflush(stdout);

        // Pedido de dependências pré-operatórias (LAB + FARMÁCIA) para esta cirurgia
        printf("[SURGERY] A pedir PHARMACY e LAB (patient=%s)\n", patient_id);
        fflush(stdout);
        // pede recursos
        send_pharmacy_request_pipe(urgent, op, patient_id, "GENERIC_MED");
        send_lab_request_pipe(urgent, op, patient_id, 1);

        //espera pelas confirmações (reencaminhadas pelo SYSTEM)
        wait_for_dependency(1, 1,
            op,
            patient_id,
            urgent
        );

        // cirurgia
        int min_d=60, max_d=120;
        bo_duration_bounds(bo_idx, &min_d, &max_d);

        int dur_units = rand_between(min_d, max_d);
        printf("[SURGERY] Início cirurgia patient=%s sala=%s dur_ut=%d\n", patient_id, bo_name, dur_units);
        fflush(stdout);

        usleep(dur_units * g_config.time_unit_ms * 1000);

        stats_inc_surgery(bo_idx);
        stats_add_surgery_wait((double)dur_units);

        // limpeza
        int clean_units = rand_between(g_config.cleanup_min_time, g_config.cleanup_max_time);
        printf("[SURGERY] Limpeza sala=%s clean_ut=%d\n", bo_name, clean_units);
        fflush(stdout);

        usleep(clean_units * g_config.time_unit_ms * 1000);

        // libertar recursos
        if (bo_sem) sem_post(bo_sem);
        if (g_sem_medical_teams) sem_post(g_sem_medical_teams);

        printf("[SURGERY] Fim cirurgia patient=%s sala=%s\n", patient_id, bo_name);
        fflush(stdout);

        // Notificação do término da cirurgia ao gestor central (SYSTEM) via fila RESPONSES,
        // permitindo atualização de estado e estatísticas sem acoplamento direto entre processos.
        hospital_message_t done;
        memset(&done, 0, sizeof(done));
        done.mtype    = 2;
        done.msg_type = MSG_SURGERY_DONE;

        safe_copy(done.source, sizeof(done.source), "SURGERY");
        safe_copy(done.target, sizeof(done.target), "SYSTEM");
        safe_copy(done.patient_id, sizeof(done.patient_id), patient_id);

        done.operation_id = op;
        done.timestamp    = time(NULL);

        snprintf(done.data, sizeof(done.data),
                 "SURGERY_DONE patient=%s sala=%s dur_ut=%d clean_ut=%d urgent=%d",
                 done.patient_id, bo_name, dur_units, clean_units, urgent);

        if (send_to_responses(&done) != 0) {
            perror("[SURGERY] send_to_responses");
        } else {
            printf("[SURGERY] DONE_SENT -> RESPONSES: %s\n", done.data);
            fflush(stdout);
        }

        log_event(LOG_INFO, "SURGERY", "DONE_SENT", done.data);
    }

    close(fd);
    printf("[SURGERY] A terminar.\n");
    fflush(stdout);
    log_event(LOG_INFO, "SURGERY", "STOP", "surgery_main_loop terminou");
}

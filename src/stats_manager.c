#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>

#include "stats.h"
#include "log.h"

global_statistics_t *g_stats_shm = NULL;
static int g_shmid_stats = -1;

static key_t stats_key(void) {
    // ftok requer um ficheiro existente; usa-se config/config.txt como referência
    return ftok("config/config.txt", 'A'); // A = stats
}

static void lock_stats(void) {
    pthread_mutex_lock(&g_stats_shm->mutex);
}
static void unlock_stats(void) {
    pthread_mutex_unlock(&g_stats_shm->mutex);
}

int stats_init(int owner) {
    key_t key = stats_key();
    if (key == (key_t)-1) {
        perror("ftok stats");
        return -1;
    }

    int flags = 0666 | (owner ? IPC_CREAT : 0);
    g_shmid_stats = shmget(key, sizeof(global_statistics_t), flags);
    if (g_shmid_stats == -1) {
        perror("shmget stats");
        return -1;
    }

    void *addr = shmat(g_shmid_stats, NULL, 0);
    if (addr == (void*)-1) {
        perror("shmat stats");
        return -1;
    }

    g_stats_shm = (global_statistics_t*)addr;

    if (owner) {
        memset(g_stats_shm, 0, sizeof(*g_stats_shm));
        g_stats_shm->system_start_time = time(NULL);

        // mutex partilhado entre processos
        pthread_mutexattr_init(&g_stats_shm->mutex_attr);
        pthread_mutexattr_setpshared(&g_stats_shm->mutex_attr, PTHREAD_PROCESS_SHARED);
        pthread_mutex_init(&g_stats_shm->mutex, &g_stats_shm->mutex_attr);
    }

    return 0;
}

void stats_close(int owner) {
    (void)owner;

    if (g_stats_shm) {
        shmdt(g_stats_shm);
        g_stats_shm = NULL;
    }
    // IPC_RMID é feito no gestor central para evitar remoção enquanto outros processos ainda usam o segmento
}

/* ===== incrementos simples ===== */
void stats_inc_emergency(void) {
    if (!g_stats_shm) return;
    lock_stats();
    g_stats_shm->total_emergency_patients++;
    g_stats_shm->total_operations++;
    unlock_stats();
}

void stats_inc_appointment(void) {
    if (!g_stats_shm) return;
    lock_stats();
    g_stats_shm->total_appointments++;
    g_stats_shm->total_operations++;
    unlock_stats();
}

void stats_inc_lab_request(int urgent) {
    if (!g_stats_shm) return;
    lock_stats();
    if (urgent) g_stats_shm->urgent_lab_tests++;
    // não sabemos lab1/lab2 aqui; isso é contabilizado no processo LAB
    g_stats_shm->total_operations++;
    unlock_stats();
}

void stats_inc_pharmacy_request(int urgent) {
    if (!g_stats_shm) return;
    lock_stats();
    g_stats_shm->total_pharmacy_requests++;
    if (urgent) g_stats_shm->urgent_requests++;
    else g_stats_shm->normal_requests++;
    g_stats_shm->total_operations++;
    unlock_stats();
}

void stats_inc_surgery(int bo_index) {
    if (!g_stats_shm) return;
    lock_stats();
    if (bo_index == 0) g_stats_shm->total_surgeries_bo1++;
    else if (bo_index == 1) g_stats_shm->total_surgeries_bo2++;
    else if (bo_index == 2) g_stats_shm->total_surgeries_bo3++;
    g_stats_shm->total_operations++;
    unlock_stats();
}

/* ===== acumuladores tempos ===== */
void stats_add_emergency_wait(double ut) {
    if (!g_stats_shm) return;
    lock_stats();
    g_stats_shm->total_emergency_wait_time += ut;
    g_stats_shm->completed_emergencies++;
    unlock_stats();
}

void stats_add_appointment_wait(double ut) {
    if (!g_stats_shm) return;
    lock_stats();
    g_stats_shm->total_appointment_wait_time += ut;
    g_stats_shm->completed_appointments++;
    unlock_stats();
}

void stats_add_surgery_wait(double ut) {
    if (!g_stats_shm) return;
    lock_stats();
    g_stats_shm->total_surgery_wait_time += ut;
    g_stats_shm->completed_surgeries++;
    unlock_stats();
}

void stats_add_lab_turnaround(double ut) {
    if (!g_stats_shm) return;
    lock_stats();
    g_stats_shm->total_lab_turnaround_time += ut;
    unlock_stats();
}

void stats_add_pharmacy_response(double ut) {
    if (!g_stats_shm) return;
    lock_stats();
    g_stats_shm->total_pharmacy_response_time += ut;
    unlock_stats();
}

/* ===== formatação ===== */
static double avg(double total, int n) {
    if (n <= 0) return 0.0;
    return total / (double)n;
}

void display_statistics_console(void) {
    if (!g_stats_shm) return;

    lock_stats();

    time_t now = time(NULL);
    int up = (int)difftime(now, g_stats_shm->system_start_time);

    printf("==========================================\n");
    printf("ESTATÍSTICAS DO SISTEMA HOSPITALAR\n");
    printf("==========================================\n");
    printf("Timestamp: %s", ctime(&now));
    printf("Tempo Operação: %d segundos\n", up);

    printf("\nCENTRO DE TRIAGEM\n------------------\n");
    printf("Total Emergências: %d\n", g_stats_shm->total_emergency_patients);
    printf("Total Consultas:   %d\n", g_stats_shm->total_appointments);
    printf("Tempo Médio Espera (Emerg.): %.2f ut\n",
           avg(g_stats_shm->total_emergency_wait_time, g_stats_shm->completed_emergencies));
    printf("Tempo Médio Espera (Consul.): %.2f ut\n",
           avg(g_stats_shm->total_appointment_wait_time, g_stats_shm->completed_appointments));
    printf("Pacientes Transferidos: %d\n", g_stats_shm->critical_transfers);
    printf("Pacientes Rejeitados:   %d\n", g_stats_shm->rejected_patients);

    printf("\nBLOCOS OPERATÓRIOS\n------------------\n");
    printf("BO1 (Cardiologia): %d\n", g_stats_shm->total_surgeries_bo1);
    printf("BO2 (Ortopedia):   %d\n", g_stats_shm->total_surgeries_bo2);
    printf("BO3 (Neurologia):  %d\n", g_stats_shm->total_surgeries_bo3);
    printf("Cirurgias Canceladas: %d\n", g_stats_shm->cancelled_surgeries);
    printf("Tempo Médio Espera (Cirurgia): %.2f ut\n",
           avg(g_stats_shm->total_surgery_wait_time, g_stats_shm->completed_surgeries));

    printf("\nFARMÁCIA CENTRAL\n----------------\n");
    printf("Total Pedidos: %d\n", g_stats_shm->total_pharmacy_requests);
    printf("Pedidos Urgentes: %d\n", g_stats_shm->urgent_requests);
    printf("Tempo Médio Resposta: %.2f ut\n",
           avg(g_stats_shm->total_pharmacy_response_time, g_stats_shm->total_pharmacy_requests));
    printf("Reposições Stock: %d\n", g_stats_shm->auto_restocks);
    printf("Esgotamentos: %d\n", g_stats_shm->stock_depletions);

    printf("\nLABORATÓRIOS\n------------\n");
    printf("LAB1 análises: %d\n", g_stats_shm->total_lab_tests_lab1);
    printf("LAB2 análises: %d\n", g_stats_shm->total_lab_tests_lab2);
    printf("Análises Urgentes: %d\n", g_stats_shm->urgent_lab_tests);
    printf("Tempo Médio Total (LAB): %.2f ut\n",
           avg(g_stats_shm->total_lab_turnaround_time,
               g_stats_shm->total_lab_tests_lab1 + g_stats_shm->total_lab_tests_lab2));

    printf("\nGLOBAIS\n-------\n");
    printf("Total Operações: %d\n", g_stats_shm->total_operations);
    printf("Erros Sistema: %d\n", g_stats_shm->system_errors);
    printf("==========================================\n");

    unlock_stats();
}

static void make_timestamp(char *out, size_t out_sz) {
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    strftime(out, out_sz, "%Y%m%d_%H%M%S", &tm_now);
}

int stats_write_snapshot(const char *filepath) {
    if (!g_stats_shm) return -1;

    FILE *f = fopen(filepath, "w");
    if (!f) {
        char err[320];
        snprintf(err, sizeof(err), "%s | fopen failed: %s", filepath, strerror(errno));
        log_event(LOG_ERROR, "SYSTEM", "STATS_SNAPSHOT_CREATE_FAILED", err);
        return -1;
    }

    lock_stats();

    time_t now = time(NULL);
    int up = (int)difftime(now, g_stats_shm->system_start_time);

    fprintf(f, "==========================================\n");
    fprintf(f, "ESTATÍSTICAS DO SISTEMA HOSPITALAR\n");
    fprintf(f, "==========================================\n");
    fprintf(f, "Timestamp: %s", ctime(&now));
    fprintf(f, "Tempo Operação: %d segundos\n", up);

    fprintf(f, "\nCENTRO DE TRIAGEM\n------------------\n");
    fprintf(f, "Total Emergências: %d\n", g_stats_shm->total_emergency_patients);
    fprintf(f, "Total Consultas:   %d\n", g_stats_shm->total_appointments);
    fprintf(f, "Tempo Médio Espera (Emerg.): %.2f ut\n",
            avg(g_stats_shm->total_emergency_wait_time, g_stats_shm->completed_emergencies));
    fprintf(f, "Tempo Médio Espera (Consul.): %.2f ut\n",
            avg(g_stats_shm->total_appointment_wait_time, g_stats_shm->completed_appointments));
    fprintf(f, "Pacientes Transferidos: %d\n", g_stats_shm->critical_transfers);
    fprintf(f, "Pacientes Rejeitados:   %d\n", g_stats_shm->rejected_patients);

    fprintf(f, "\nBLOCOS OPERATÓRIOS\n------------------\n");
    fprintf(f, "BO1 (Cardiologia): %d\n", g_stats_shm->total_surgeries_bo1);
    fprintf(f, "BO2 (Ortopedia):   %d\n", g_stats_shm->total_surgeries_bo2);
    fprintf(f, "BO3 (Neurologia):  %d\n", g_stats_shm->total_surgeries_bo3);
    fprintf(f, "Cirurgias Canceladas: %d\n", g_stats_shm->cancelled_surgeries);
    fprintf(f, "Tempo Médio Espera (Cirurgia): %.2f ut\n",
            avg(g_stats_shm->total_surgery_wait_time, g_stats_shm->completed_surgeries));

    fprintf(f, "\nFARMÁCIA CENTRAL\n----------------\n");
    fprintf(f, "Total Pedidos: %d\n", g_stats_shm->total_pharmacy_requests);
    fprintf(f, "Pedidos Urgentes: %d\n", g_stats_shm->urgent_requests);
    fprintf(f, "Tempo Médio Resposta: %.2f ut\n",
            avg(g_stats_shm->total_pharmacy_response_time, g_stats_shm->total_pharmacy_requests));
    fprintf(f, "Reposições Stock: %d\n", g_stats_shm->auto_restocks);
    fprintf(f, "Esgotamentos: %d\n", g_stats_shm->stock_depletions);

    fprintf(f, "\nLABORATÓRIOS\n------------\n");
    fprintf(f, "LAB1 análises: %d\n", g_stats_shm->total_lab_tests_lab1);
    fprintf(f, "LAB2 análises: %d\n", g_stats_shm->total_lab_tests_lab2);
    fprintf(f, "Análises Urgentes: %d\n", g_stats_shm->urgent_lab_tests);
    fprintf(f, "Tempo Médio Total (LAB): %.2f ut\n",
            avg(g_stats_shm->total_lab_turnaround_time,
                g_stats_shm->total_lab_tests_lab1 + g_stats_shm->total_lab_tests_lab2));

    fprintf(f, "\nGLOBAIS\n-------\n");
    fprintf(f, "Total Operações: %d\n", g_stats_shm->total_operations);
    fprintf(f, "Erros Sistema: %d\n", g_stats_shm->system_errors);
    fprintf(f, "==========================================\n");

    unlock_stats();

    fclose(f);
    return 0;
}

int save_statistics_snapshot(void) {
    char ts[32];
    make_timestamp(ts, sizeof(ts));

    char path[256];
    snprintf(path, sizeof(path), "results/stats_snapshots/stats_snapshot_%s.txt", ts);

    return stats_write_snapshot(path);
}

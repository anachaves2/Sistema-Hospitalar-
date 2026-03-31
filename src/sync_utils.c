#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <errno.h>
#include <string.h>

#include "sync.h"

//Ponteiros globais
sem_t *g_sem_bo1 = NULL;
sem_t *g_sem_bo2 = NULL;
sem_t *g_sem_bo3 = NULL;

sem_t *g_sem_medical_teams = NULL;

sem_t *g_sem_lab1 = NULL;
sem_t *g_sem_lab2 = NULL;

sem_t *g_sem_pharmacy = NULL;

static sem_t *open_named_sem(const char *name, unsigned int initial_value) {
    sem_t *s = sem_open(name, O_CREAT, 0644, initial_value);
    if (s == SEM_FAILED) {
        fprintf(stderr, "[SYNC] sem_open(%s) falhou: %s\n", name, strerror(errno));
        return NULL;
    }
    return s;
}

int init_all_sync(void) {
    // BO rooms (1 operação por sala)
    g_sem_bo1 = open_named_sem(SEM_NAME_BO1, SEM_INIT_BO_ROOM);
    g_sem_bo2 = open_named_sem(SEM_NAME_BO2, SEM_INIT_BO_ROOM);
    g_sem_bo3 = open_named_sem(SEM_NAME_BO3, SEM_INIT_BO_ROOM);

    // equipas médicas
    g_sem_medical_teams = open_named_sem(SEM_NAME_MED_TEAMS, SEM_INIT_MED_TEAMS);

    // labs
    g_sem_lab1 = open_named_sem(SEM_NAME_LAB1, SEM_INIT_LAB_SLOTS);
    g_sem_lab2 = open_named_sem(SEM_NAME_LAB2, SEM_INIT_LAB_SLOTS);

    // farmácia
    g_sem_pharmacy = open_named_sem(SEM_NAME_PHARMACY, SEM_INIT_PHARMACY);

    if (!g_sem_bo1 || !g_sem_bo2 || !g_sem_bo3 ||
        !g_sem_medical_teams || !g_sem_lab1 || !g_sem_lab2 ||
        !g_sem_pharmacy) {
        fprintf(stderr, "[SYNC] Falha na inicialização de um ou mais semáforos POSIX.\n");
        return -1;
    }

    return 0;
}

static void close_and_unlink(const char *name, sem_t **s) {
    if (s && *s) {
        if (sem_close(*s) == -1) {
            fprintf(stderr, "[SYNC] sem_close(%s) falhou: %s\n", name, strerror(errno));
        }
        // unlink pode falhar com ENOENT se já tiver sido removido
        if (sem_unlink(name) == -1 && errno != ENOENT) {
            fprintf(stderr, "[SYNC] sem_unlink(%s) falhou: %s\n", name, strerror(errno));
        }
        *s = NULL;
    }
}

void cleanup_all_sync(void) {
    close_and_unlink(SEM_NAME_BO1, &g_sem_bo1);
    close_and_unlink(SEM_NAME_BO2, &g_sem_bo2);
    close_and_unlink(SEM_NAME_BO3, &g_sem_bo3);

    close_and_unlink(SEM_NAME_MED_TEAMS, &g_sem_medical_teams);

    close_and_unlink(SEM_NAME_LAB1, &g_sem_lab1);
    close_and_unlink(SEM_NAME_LAB2, &g_sem_lab2);

    close_and_unlink(SEM_NAME_PHARMACY, &g_sem_pharmacy);

    printf("[SYNC] Semáforos POSIX fechados e removidos.\n");
}

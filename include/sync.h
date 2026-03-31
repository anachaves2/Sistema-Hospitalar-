#ifndef SYNC_H
#define SYNC_H

#include <semaphore.h>

// Semáforos POSIX
// Controlam concorrência de recursos (BOs, equipas médicas, laboratórios, farmácia)

// Nomes (sem_unlink/sem_open)
#define SEM_NAME_BO1           "/sem_bo1"
#define SEM_NAME_BO2           "/sem_bo2"
#define SEM_NAME_BO3           "/sem_bo3"
#define SEM_NAME_MED_TEAMS     "/sem_med_teams"
#define SEM_NAME_LAB1          "/sem_lab1"
#define SEM_NAME_LAB2          "/sem_lab2"
#define SEM_NAME_PHARMACY      "/sem_pharmacy"

// Capacidades (valores iniciais típicos; podem vir de config)
#define SEM_INIT_BO_ROOM       1   // 1 cirurgia por sala
#define SEM_INIT_MED_TEAMS     2   // valor por defeito
#define SEM_INIT_LAB_SLOTS     2   // valor por defeito
#define SEM_INIT_PHARMACY      4   // valor por defeito

// Handlers globais (acessíveis por todos os módulos)
extern sem_t *g_sem_bo1;
extern sem_t *g_sem_bo2;
extern sem_t *g_sem_bo3;

extern sem_t *g_sem_medical_teams;

extern sem_t *g_sem_lab1;
extern sem_t *g_sem_lab2;

extern sem_t *g_sem_pharmacy;

// Inicialização / Cleanup
// - init: sem_open com O_CREAT e valores iniciais
// - cleanup: sem_close + sem_unlink
int  init_all_sync(void);
void cleanup_all_sync(void);

#endif

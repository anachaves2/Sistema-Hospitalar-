#ifndef STATS_H
#define STATS_H

#include "hospital.h"

// Inicializa/fecha acesso ao segmento SHM de estatísticas.
// owner=1 no Gestor Central (cria e inicializa)
// owner=0 nos filhos (só anexa)
int  stats_init(int owner);
void stats_close(int owner);

// Helpers de atualização (process-safe + thread-safe via mutex na SHM)
void stats_inc_emergency(void);
void stats_inc_appointment(void);
void stats_inc_lab_request(int urgent);
void stats_inc_pharmacy_request(int urgent);
void stats_inc_surgery(int bo_index); // 0=BO1,1=BO2,2=BO3

// Tempos acumulados em "ut" (unidades de simulação)
void stats_add_emergency_wait(double ut);
void stats_add_appointment_wait(double ut);
void stats_add_surgery_wait(double ut);
void stats_add_lab_turnaround(double ut);
void stats_add_pharmacy_response(double ut);

// Impressão (SIGUSR1)
void display_statistics_console(void);

// Snapshot ficheiro (SIGUSR2)
int save_statistics_snapshot(void);

// Snapshot para caminho específico (útil para final_stats)
int stats_write_snapshot(const char *filepath);

#endif

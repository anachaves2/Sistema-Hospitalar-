#ifndef IPC_H
#define IPC_H

#include "hospital.h"

// ===== Message Queues =====
// Criar/limpar filas de mensagens (urgente, normal e respostas)
int create_all_message_queues(void);
void cleanup_message_queues(void);
int connect_message_queues(void);


int fifo_write_line(const char *fifo_path, const char *line);

// Recebe preferindo mensagens com maior prioridade (mtype menor) na fila normal
int receive_from_normal_hi_first(hospital_message_t *msg);


// Enviar/receber genérico
int send_message(int mqid, hospital_message_t *msg);
int receive_message_priority(int mqid, hospital_message_t *msg);

// Helpers por fila (se preferires manter o teu estilo)
int send_to_urgent(hospital_message_t *msg);
int receive_from_urgent(hospital_message_t *msg);
int receive_from_urgent_nowait(hospital_message_t *msg);
int receive_from_urgent_type(hospital_message_t *msg, long type);

int send_to_normal(hospital_message_t *msg);
int receive_from_normal(hospital_message_t *msg);
int receive_from_normal_nowait(hospital_message_t *msg);

int send_to_responses(hospital_message_t *msg);
int receive_from_responses(hospital_message_t *msg);

// ===== Shared Memory =====
// estatísticas é utilizada; os restantes segmentos podem ser opcionais/extensões.
int create_all_shared_memory(void);
void cleanup_shared_memory(void);

// Ponteiros globais mapeados via SHM (expostos para acesso pelos módulos)
extern global_statistics_t *g_stats_shm;
extern surgery_block_shm_t *g_surgery_shm;
extern pharmacy_shm_t *g_pharmacy_shm;
extern lab_queue_shm_t *g_lab_shm;
extern critical_log_shm_t *g_critlog_shm;

// ===== Named Pipes =====
int create_all_named_pipes(void);
void cleanup_named_pipes(void);

// ===== Semáforos POSIX =====
int create_all_semaphores(void);
void cleanup_semaphores(void);

#endif

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>

#include "hospital.h"
#include "ipc.h"
#include "config.h"
#include "log.h"

#define MAX_TRIAGE_QUEUE 100
#define INBOX_MAX 256

// Processo TRIAGE: recebe pedidos (URGENT/NORMAL), coloca-os numa inbox interna
// e mantém duas filas (emergências e consultas) ordenadas por critérios definidos.
// Uma thread de monitorização degrada a estabilidade ao longo do tempo e sinaliza críticos.

static triage_patient_t emerg_queue[MAX_TRIAGE_QUEUE];
static int emerg_size = 0;

static triage_patient_t appt_queue[MAX_TRIAGE_QUEUE];
static int appt_size = 0;

static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static unsigned long g_arrival_counter = 0;

static volatile int triage_shutdown = 0;

static hospital_message_t inbox[INBOX_MAX];
static int inbox_head = 0, inbox_tail = 0, inbox_count = 0;

static pthread_mutex_t inbox_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  inbox_cond  = PTHREAD_COND_INITIALIZER;

// Inbox interna (buffer circular) para desacoplar receção (threads bloqueantes em MQ)
// do processamento principal (loop da TRIAGE), evitando contenção e perda de mensagens.
static void inbox_push(const hospital_message_t *m) {
    pthread_mutex_lock(&inbox_mutex);

    if (inbox_count == INBOX_MAX) {
        log_event(LOG_WARNING, "TRIAGE", "INBOX_FULL_DROP", m->data);
        pthread_mutex_unlock(&inbox_mutex);
        return;
    }

    inbox[inbox_tail] = *m;
    inbox_tail = (inbox_tail + 1) % INBOX_MAX;
    inbox_count++;

    pthread_cond_signal(&inbox_cond);
    pthread_mutex_unlock(&inbox_mutex);
}

static int inbox_pop(hospital_message_t *out) {
    pthread_mutex_lock(&inbox_mutex);

    while (inbox_count == 0 && !triage_shutdown) {
        pthread_cond_wait(&inbox_cond, &inbox_mutex);
    }

    if (triage_shutdown) {
        pthread_mutex_unlock(&inbox_mutex);
        return -1;
    }

    *out = inbox[inbox_head];
    inbox_head = (inbox_head + 1) % INBOX_MAX;
    inbox_count--;

    pthread_mutex_unlock(&inbox_mutex);
    return 0;
}

/* =========================
   receiver threads (bloqueantes)
   ========================= */
static void *urgent_receiver_thread(void *arg) {
    (void)arg;
    hospital_message_t msg;

    printf("[TRIAGE] URGENT_RX iniciado\n");
    fflush(stdout);
    log_event(LOG_INFO, "TRIAGE", "URGENT_RX_START", "receiver URGENT iniciado");

    while (!triage_shutdown) {
        // Thread dedicada: bloqueia na fila de mensagens e encaminha para a inbox interna.
        if (receive_from_urgent(&msg) != 0) continue;
        inbox_push(&msg);
    }

    log_event(LOG_INFO, "TRIAGE", "URGENT_RX_STOP", "receiver URGENT terminou");
    return NULL;
}

static void *normal_receiver_thread(void *arg) {
    (void)arg;
    hospital_message_t msg;

    printf("[TRIAGE] NORMAL_RX iniciado\n");
    fflush(stdout);
    log_event(LOG_INFO, "TRIAGE", "NORMAL_RX_START", "receiver NORMAL iniciado");

    while (!triage_shutdown) {
        // Thread dedicada: bloqueia na fila NORMAL e encaminha para a inbox interna.
        if (receive_from_normal_hi_first(&msg) != 0) continue; // BLOQUEANTE
        inbox_push(&msg);
    }

    log_event(LOG_INFO, "TRIAGE", "NORMAL_RX_STOP", "receiver NORMAL terminou");
    return NULL;
}

/* =========================
   Helpers: parse
   ========================= */
static int parse_triage_fields(
    const char *data,
    int *triage_level,
    int *stability,
    int *scheduled
) {
    if (!data) return -1;

    const char *pt = strstr(data, "triage=");
    const char *ps = strstr(data, "stability=");
    if (!pt || !ps) return -1;

    *triage_level = atoi(pt + 7);
    *stability    = atoi(ps + 10);

    if (*triage_level < 1 || *triage_level > 5) return -1;

    if (scheduled) {
        const char *psch = strstr(data, "scheduled=");
        if (psch) *scheduled = atoi(psch + 10);
        else      *scheduled = 0;   // fallback
    }

    return 0;
}

/* =========================
   Ordenação emergências vs consultas
   ========================= */

/* Emergências: pacientes críticos primeiro, seguidos da prioridade clínica,
   nível de triagem e, por fim, ordem de chegada */
static int emerg_cmp(const triage_patient_t *a, const triage_patient_t *b) {
    if (a->is_critical != b->is_critical) return (b->is_critical - a->is_critical);

    if (a->priority != b->priority) return (a->priority - b->priority);

    if (a->triage_level != b->triage_level) return (a->triage_level - b->triage_level);

    if (a->arrival_order < b->arrival_order) return -1;
    if (a->arrival_order > b->arrival_order) return 1;
    return 0;
}

static void sort_emerg_queue(void) {
    for (int i = 0; i < emerg_size - 1; i++) {
        for (int j = 0; j < emerg_size - 1 - i; j++) {
            if (emerg_cmp(&emerg_queue[j], &emerg_queue[j + 1]) > 0) {
                triage_patient_t tmp = emerg_queue[j];
                emerg_queue[j] = emerg_queue[j + 1];
                emerg_queue[j + 1] = tmp;
            }
        }
    }
}

/* Consultas: ordenadas por hora marcada (scheduled_time).
   Em caso de empate, usa-se a ordem de chegada (arrival_order). */
static int appointment_cmp(const triage_patient_t *a,
                           const triage_patient_t *b) {
    if (a->scheduled_time != b->scheduled_time)
        return a->scheduled_time - b->scheduled_time;

    // desempate: quem chegou primeiro
    if (a->arrival_order < b->arrival_order) return -1;
    if (a->arrival_order > b->arrival_order) return 1;

    return 0;
}

static void sort_appointments(void) {
    for (int i = 0; i < appt_size - 1; i++) {
        for (int j = 0; j < appt_size - 1 - i; j++) {
            if (appointment_cmp(&appt_queue[j],
                                &appt_queue[j + 1]) > 0) {
                triage_patient_t tmp = appt_queue[j];
                appt_queue[j] = appt_queue[j + 1];
                appt_queue[j + 1] = tmp;
            }
        }
    }
}

/* =========================
   Enqueue
   ========================= */
static void enqueue_emergency(const char *patient_id, int triage_level, int stability, int priority) {
    if (emerg_size >= MAX_TRIAGE_QUEUE) {
        log_event(LOG_ERROR, "TRIAGE", "EMERG_QUEUE_FULL", patient_id);
        return;
    }

    triage_patient_t *p = &emerg_queue[emerg_size];
    (void)snprintf(p->id, sizeof(p->id), "%.*s", (int)sizeof(p->id) - 1, patient_id);

    p->forwarded_to_surgery = 0;
    p->triage_level = triage_level;
    p->stability = stability;
    p->priority = priority;

    // crítico se stability <= limiar
    p->is_critical = (stability <= g_config.triage_critical_stability) ? 1 : 0;

    p->arrival_order = g_arrival_counter++;

    emerg_size++;
    sort_emerg_queue();

    char details[160];
    (void)snprintf(details, sizeof(details),
                   "enqueue_emergency %s t=%d s=%d crit=%d prio=%d",
                   p->id, p->triage_level, p->stability, p->is_critical, p->priority);
    log_event(LOG_INFO, "TRIAGE", "ENQUEUE_EMERGENCY", details);
}

static void enqueue_appointment(const char *patient_id,
                                int triage_level,
                                int stability,
                                int scheduled) {
    if (appt_size >= MAX_TRIAGE_QUEUE) {
        log_event(LOG_ERROR, "TRIAGE", "APPT_QUEUE_FULL", patient_id);
        return;
    }

    triage_patient_t *p = &appt_queue[appt_size];

    snprintf(p->id, sizeof(p->id), "%.*s",
             (int)sizeof(p->id) - 1, patient_id);

    p->triage_level = triage_level;
    p->stability = stability;
    p->is_critical = 0;
    p->scheduled_time = scheduled;
    p->arrival_order = g_arrival_counter++;
    p->forwarded_to_surgery = 0;

    appt_size++;
    sort_appointments();

    char details[128];
    snprintf(details, sizeof(details),
             "enqueue_appt %s t=%d s=%d scheduled=%d",
             p->id, p->triage_level, p->stability, p->scheduled_time);

    log_event(LOG_INFO, "TRIAGE", "ENQUEUE_APPOINTMENT", details);
}


static void print_queues(void) {
    printf("\n[TRIAGE] ===== ESTADO DAS FILAS =====\n");

    /* ---------- EMERGENCY QUEUE ---------- */
    printf("[TRIAGE] EMERGENCY size=%d\n", emerg_size);
    for (int i = 0; i < emerg_size; i++) {
        printf("  [E%d] id=%s prio=%d crit=%d triage=%d stab=%d arrival=%lu\n",
               i,
               emerg_queue[i].id,
               emerg_queue[i].priority,
               emerg_queue[i].is_critical,
               emerg_queue[i].triage_level,
               emerg_queue[i].stability,
               emerg_queue[i].arrival_order);
    }

    /* ---------- APPOINTMENT QUEUE ---------- */
    printf("[TRIAGE] APPOINTMENT size=%d\n", appt_size);
    for (int i = 0; i < appt_size; i++) {
        printf("  [A%d] id=%s triage=%d stab=%d scheduled=%d arrival=%lu\n",
               i,
               appt_queue[i].id,
               appt_queue[i].triage_level,
               appt_queue[i].stability,
               appt_queue[i].scheduled_time,
               appt_queue[i].arrival_order);
    }

    printf("[TRIAGE] ============================\n\n");
    fflush(stdout);
}

/* =========================
   Monitor thread
   ========================= */
void *triage_monitor_thread(void *arg) {
    (void)arg;
    printf("[TRIAGE] Thread monitor iniciada.\n");
    fflush(stdout);
    log_event(LOG_INFO, "TRIAGE", "MONITOR_START", "stability monitor iniciado");

    while (!triage_shutdown) {
        usleep(g_config.time_unit_ms * 1000);

        pthread_mutex_lock(&queue_mutex);

        int i = 0;
        while (i < emerg_size) {
            emerg_queue[i].stability -= 1;

            if (emerg_queue[i].stability <= 0) {
                printf("[TRIAGE] REMOVER patient=%s (stability=0)\n", emerg_queue[i].id);
                fflush(stdout);

                log_event(LOG_WARNING, "TRIAGE", "PATIENT_REMOVED_STAB0", emerg_queue[i].id);

                for (int j = i; j < emerg_size - 1; j++) emerg_queue[j] = emerg_queue[j + 1];
                emerg_size--;
                continue;
            }

            // paciente ficou/está crítico
            if (emerg_queue[i].stability <= g_config.triage_critical_stability) {
                int became_critical = 0;

                if (!emerg_queue[i].is_critical) {
                    emerg_queue[i].is_critical = 1;
                    became_critical = 1;

                    printf("[TRIAGE] CRITICAL patient=%s triage=%d stability=%d\n",
                           emerg_queue[i].id, emerg_queue[i].triage_level, emerg_queue[i].stability);
                    fflush(stdout);
                }

                // alerta só quando passa a crítico (evita spam)
                if (became_critical) {
                    hospital_message_t alert;
                    memset(&alert, 0, sizeof(alert));

                    alert.mtype    = 1; // URGENT
                    alert.msg_type = MSG_CRITICAL_STATUS;

                    snprintf(alert.source, sizeof(alert.source), "TRIAGE");
                    snprintf(alert.target, sizeof(alert.target), "SYSTEM");
                    snprintf(alert.patient_id, sizeof(alert.patient_id), "%s", emerg_queue[i].id);
                    alert.timestamp = time(NULL);

                    snprintf(alert.data, sizeof(alert.data),
                             "CRITICAL patient=%s triage=%d stability=%d",
                             emerg_queue[i].id, emerg_queue[i].triage_level, emerg_queue[i].stability);

                    if (send_to_urgent(&alert) != 0) {
                        perror("[TRIAGE] send_to_urgent (CRITICAL)");
                    }
                    log_event(LOG_CRITICAL, "TRIAGE", "CRITICAL_ALERT", alert.data);

                    // reordenar: críticos e prioridade à frente
                    sort_emerg_queue();
                    i = 0;
                    continue;
                }

                // encaminhar para cirurgia
                if (!emerg_queue[i].forwarded_to_surgery) {
                    char line[256];
                    int op_id = 0; // op_id é gerido/atribuído no módulo SURGERY

                    snprintf(line, sizeof(line),
                             "SURGERY_TRANSFER urgent=%d op=%d patient=%s",
                             1, op_id, emerg_queue[i].id);

                    printf("[TRIAGE] A enviar para surgery_pipe: %s\n", line);
                    fflush(stdout);

                    // Encaminhamento para cirurgia via Named Pipe (IPC simples e desacoplado do MQ),
                    // usado para sinalizar transferência imediata de casos críticos.
                    if (fifo_write_line("surgery_pipe", line) == -1) {
                        perror("[TRIAGE] fifo_write_line surgery_pipe");
                        log_event(LOG_ERROR, "TRIAGE", "SURGERY_PIPE_FAIL", strerror(errno));
                        // não remove, tenta novamente no próximo tick
                    } else {
                        printf("[TRIAGE] SURGERY_PIPE_SENT patient=%s\n", emerg_queue[i].id);
                        fflush(stdout);

                        log_event(LOG_INFO, "TRIAGE", "SURGERY_PIPE_SENT", line);
                        emerg_queue[i].forwarded_to_surgery = 1;

                        // remove da fila de emergências
                        for (int j = i; j < emerg_size - 1; j++) emerg_queue[j] = emerg_queue[j + 1];
                        emerg_size--;

                        sort_emerg_queue();
                        continue; // não incrementa i
                    }
                }
            }

            i++;
        }

        pthread_mutex_unlock(&queue_mutex);
    }

    printf("[TRIAGE] Thread monitor terminada.\n");
    fflush(stdout);
    log_event(LOG_INFO, "TRIAGE", "MONITOR_STOP", "stability monitor terminado");
    return NULL;
}

/* =========================
   MAIN LOOP TRIAGE
   ========================= */
void triage_main_loop(void) {
    printf("[TRIAGE] Processo iniciado (PID=%d)\n", getpid());
    log_event(LOG_INFO, "TRIAGE", "START", "triage_main_loop iniciado");

    pthread_t monitor_thread;
    if (pthread_create(&monitor_thread, NULL, triage_monitor_thread, NULL) != 0) {
        perror("[TRIAGE] pthread_create monitor_thread");
    }

    pthread_t th_rx_u, th_rx_n;
    if (pthread_create(&th_rx_u, NULL, urgent_receiver_thread, NULL) != 0) {
        perror("[TRIAGE] pthread_create urgent_receiver_thread");
    }
    if (pthread_create(&th_rx_n, NULL, normal_receiver_thread, NULL) != 0) {
        perror("[TRIAGE] pthread_create normal_receiver_thread");
    }

    hospital_message_t msg;

    while (!triage_shutdown) {
        if (inbox_pop(&msg) != 0) break;

        printf("[TRIAGE] RECEBIDO -> mtype=%ld type=%d target=%s patient=%s data=%s\n",
               msg.mtype, msg.msg_type, msg.target, msg.patient_id, msg.data);
        fflush(stdout);

        // shutdown
        if (msg.msg_type == MSG_TYPE_SHUTDOWN &&
            (strcmp(msg.target, "TRIAGE") == 0 || strcmp(msg.target, "ALL") == 0)) {
            triage_shutdown = 1;
            break;
        }

        // target filter
        if (strcmp(msg.target, "TRIAGE") != 0 && strcmp(msg.target, "ALL") != 0) {
            continue;
        }

        if (msg.msg_type != MSG_NEW_EMERGENCY && msg.msg_type != MSG_NEW_APPOINTMENT) {
            log_event(LOG_WARNING, "TRIAGE", "MSG_IGNORED", msg.data);
            continue;
        }

        int t = 0, s = 0, scheduled = 0;

        /* parse depende do tipo */
        if (msg.msg_type == MSG_NEW_EMERGENCY) {
            if (parse_triage_fields(msg.data, &t, &s, NULL) != 0) {
                log_event(LOG_WARNING, "TRIAGE", "BAD_DATA", msg.data);
                continue;
            }
        } else { // MSG_NEW_APPOINTMENT
            if (parse_triage_fields(msg.data, &t, &s, &scheduled) != 0) {
                log_event(LOG_WARNING, "TRIAGE", "BAD_DATA", msg.data);
                continue;
            }
        }

        pthread_mutex_lock(&queue_mutex);

        if (msg.msg_type == MSG_NEW_EMERGENCY) {
            // prioridade vem do mtype
            int prio = (int)msg.mtype;
            if (prio < 1 || prio > 3) prio = 3;

            enqueue_emergency(msg.patient_id, t, s, prio);
        } else {
            //APPOINTMENT ordena por scheduled_time
            enqueue_appointment(msg.patient_id, t, s, scheduled);
        }

        print_queues();
        pthread_mutex_unlock(&queue_mutex);
    }

    printf("[TRIAGE] Shutdown recebido. A terminar.\n");
    log_event(LOG_INFO, "TRIAGE", "SHUTDOWN", "triage_main_loop a terminar");

    // acordar inbox_pop se estiver bloqueado
    pthread_mutex_lock(&inbox_mutex);
    triage_shutdown = 1;
    pthread_cond_broadcast(&inbox_cond);
    pthread_mutex_unlock(&inbox_mutex);

    pthread_join(th_rx_u, NULL);
    pthread_join(th_rx_n, NULL);
    pthread_join(monitor_thread, NULL);
}

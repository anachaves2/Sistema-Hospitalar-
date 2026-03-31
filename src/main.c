#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>

#include "hospital.h"
#include "ipc.h"
#include "sync.h"
#include "config.h"
#include "log.h"
#include "stats.h"

// Gestor Central do sistema hospitalar.
// Responsável por inicializar IPC, lançar processos/threads,
// encaminhar mensagens e gerir o encerramento gracioso.

// Config global
system_config_t g_config;

// Flags de sinais (handlers só levantam flags)
static volatile sig_atomic_t g_shutdown_flag = 0;
static volatile sig_atomic_t g_sigusr1_flag  = 0;
static volatile sig_atomic_t g_sigusr2_flag  = 0;

//PIDs filhos
static pid_t pid_triage=-1, pid_surgery=-1, pid_pharmacy=-1, pid_laboratory=-1;

// Threads
static pthread_t th_cmd_reader;
static pthread_t th_responses;
static pthread_t th_urgent_router;

// protótipos de loops (definidos nos processos)
void triage_main_loop(void);
void surgery_main_loop(void);
void pharmacy_main_loop(void);
void laboratory_main_loop(void);

static void on_sigint(int sig){ (void)sig; g_shutdown_flag = 1; }
static void on_sigusr1(int sig){ (void)sig; g_sigusr1_flag = 1; }
static void on_sigusr2(int sig){ (void)sig; g_sigusr2_flag = 1; }

static void install_signals(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, NULL);

    sa.sa_handler = on_sigusr1;
    sigaction(SIGUSR1, &sa, NULL);

    sa.sa_handler = on_sigusr2;
    sigaction(SIGUSR2, &sa, NULL);
}

static void ensure_dir(const char *path) {
    if (mkdir(path, 0777) == -1 && errno != EEXIST) {
        perror("[SYSTEM] mkdir");
    }
}

static long priority_from_triage(int triage, int stability) {
    if (triage < 1) triage = 1;
    if (triage > 5) triage = 5;

    // 1 = URGENT (crítico)
    if (stability <= g_config.triage_critical_stability) return 1;

    // 2 = HIGH (triage 1-2)
    if (triage <= 2) return 2;

    // 3 = NORMAL (triage 3-5)
    return 3;
}


static int create_required_dirs(void) {
    ensure_dir("logs");
    ensure_dir("results");
    ensure_dir("results/lab_results");
    ensure_dir("results/pharmacy_deliveries");
    ensure_dir("results/stats_snapshots");
    return 0;
}

static int create_required_fifos(void) {
    const char *pipes[] = {"input_pipe","triage_pipe","surgery_pipe","pharmacy_pipe","lab_pipe"};
    for (size_t i = 0; i < sizeof(pipes)/sizeof(pipes[0]); i++) {
        if (mkfifo(pipes[i], 0666) == -1 && errno != EEXIST) {
            perror("[SYSTEM] mkfifo");
            return -1;
        }
    }
    return 0;
}

/* -------------------------
   Child runners
   ------------------------- */
static void run_triage(void) {
    if (connect_message_queues() != 0) {
        fprintf(stderr, "[TRIAGE] Falha a ligar às message queues\n");
        _exit(1);
    }

    (void)stats_init(0);
    triage_main_loop();
    _exit(0);
}

static void run_surgery(void) {
    if (connect_message_queues() != 0) {
        fprintf(stderr, "[TRIAGE] Falha a ligar às message queues\n");
        _exit(1);
    }

    (void)stats_init(0);
    surgery_main_loop();
    _exit(0);
}

static void run_pharmacy(void) {
    if (connect_message_queues() != 0) {
        fprintf(stderr, "[TRIAGE] Falha a ligar às message queues\n");
        _exit(1);
    }

    (void)stats_init(0);
    pharmacy_main_loop();
    _exit(0);
}

static void run_laboratory(void) {
    if (connect_message_queues() != 0) {
        fprintf(stderr, "[TRIAGE] Falha a ligar às message queues\n");
        _exit(1);
    }

    (void)stats_init(0);
    laboratory_main_loop();
    _exit(0);
}


/* -------------------------
   Respostas (fila RESPONSES)
   Guarda ficheiros lab/pharmacy
   ------------------------- */
static void make_timestamp(char *buf, size_t sz) {
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    strftime(buf, sz, "%Y%m%d_%H%M%S", &tm_now);
}

static void write_lab_result_file(const hospital_message_t *msg) {
    char ts[32], filename[256];
    make_timestamp(ts, sizeof(ts));
    (void)snprintf(filename, sizeof(filename),
                   "results/lab_results/lab_results_%s_%s.txt",
                   msg->patient_id, ts);

    FILE *f = fopen(filename, "w");
    if (!f) return;

    fprintf(f, "=====================================\n");
    fprintf(f, "RELATORIO DE ANALISES LABORATORIAIS\n");
    fprintf(f, "=====================================\n");
    fprintf(f, "Paciente: %s\n", msg->patient_id);
    fprintf(f, "Origem: %s | Destino: %s\n", msg->source, msg->target);
    fprintf(f, "Timestamp: %ld\n", (long)msg->timestamp);
    fprintf(f, "-------------------------------------\n");
    fprintf(f, "%s\n", msg->data);
    fprintf(f, "=====================================\n");
    fclose(f);

    log_event(LOG_INFO, "SYSTEM", "LAB_RESULT_FILE", filename);
}

static void write_pharmacy_file(const hospital_message_t *msg) {
    char ts[32], filename[256];
    make_timestamp(ts, sizeof(ts));
    (void)snprintf(filename, sizeof(filename),
                   "results/pharmacy_deliveries/pharmacy_delivery_%d_%s.txt",
                   msg->operation_id, ts);

    FILE *f = fopen(filename, "w");
    if (!f) return;

    fprintf(f, "=========================================\n");
    fprintf(f, "COMPROVATIVO DE ENTREGA - FARMACIA\n");
    fprintf(f, "=========================================\n");
    fprintf(f, "Pedido/Op: %d\n", msg->operation_id);
    fprintf(f, "Paciente/Destino: %s\n", msg->patient_id);
    fprintf(f, "Prioridade: %ld\n", msg->mtype);
    fprintf(f, "-----------------------------------------\n");
    fprintf(f, "%s\n", msg->data);
    fprintf(f, "=========================================\n");
    fclose(f);

    log_event(LOG_INFO, "SYSTEM", "PHARMACY_DELIVERY_FILE", filename);
}

static void *responses_listener(void *arg) {
    (void)arg;
    hospital_message_t msg;

    log_event(LOG_INFO, "SYSTEM", "RESPONSES_LISTENER", "iniciado");

    while (!g_shutdown_flag) {
        if (receive_from_responses(&msg) != 0) continue;

        if (msg.msg_type == MSG_TYPE_SHUTDOWN) break;

        //comportamento atual: guardar ficheiros
        if (msg.msg_type == MSG_LAB_RESULTS_READY) {
            write_lab_result_file(&msg);
        } else if (msg.msg_type == MSG_PHARMACY_READY) {
            write_pharmacy_file(&msg);
        } else if (msg.msg_type == MSG_SURGERY_DONE) {
            log_event(LOG_INFO, "SYSTEM", "SURGERY_DONE", msg.data);
        } else {
            log_event(LOG_INFO, "SYSTEM", "RESPONSE", msg.data);
        }

        //reencaminhar para SURGERY
        if (msg.msg_type == MSG_LAB_RESULTS_READY ||
            msg.msg_type == MSG_PHARMACY_READY ||
            msg.msg_type == MSG_SURGERY_DONE) {

            hospital_message_t fwd;
            memset(&fwd, 0, sizeof(fwd));

            // Mantém o tipo para a SURGERY saber o que chegou
            fwd.msg_type = msg.msg_type;

            // Prioridade
            fwd.mtype = (msg.mtype == 1) ? 1 : 3;

            snprintf(fwd.source, sizeof(fwd.source), "SYSTEM");
            snprintf(fwd.target, sizeof(fwd.target), "SURGERY");

            snprintf(fwd.patient_id, sizeof(fwd.patient_id), "%s", msg.patient_id);
            fwd.operation_id = msg.operation_id;
            fwd.timestamp = time(NULL);

            // passa o payload
            snprintf(fwd.data, sizeof(fwd.data), "%s", msg.data);

            // manda para a fila que a SURGERY vai ler
            if (fwd.mtype == 1) send_to_urgent(&fwd);
            else               send_to_normal(&fwd);
        }
    }

    log_event(LOG_INFO, "SYSTEM", "RESPONSES_LISTENER", "terminado");
    return NULL;
}

/* -------------------------
   urgent_router:
   - lê URGENT
   - se for alerta crítico do TRIAGE => encaminha para SURGERY
   ------------------------- */
static int is_for_system(const hospital_message_t *m) {
    return (strcmp(m->target, "SYSTEM") == 0 || strcmp(m->target, "ALL") == 0);
}

static void *urgent_router(void *arg) {
    (void)arg;
    hospital_message_t msg;

    log_event(LOG_INFO, "SYSTEM", "URGENT_ROUTER", "iniciado");

    while (!g_shutdown_flag) {
        if (receive_from_urgent_type(&msg, 99) != 0)
            continue;

        if (msg.msg_type == MSG_TYPE_SHUTDOWN)
            break;

        if (msg.msg_type == MSG_CRITICAL_STATUS && is_for_system(&msg)) {

            hospital_message_t fwd;
            memset(&fwd, 0, sizeof(fwd));

            fwd.mtype = 1;
            fwd.msg_type = MSG_TRANSFER_PATIENT;

            snprintf(fwd.source, sizeof(fwd.source), "SYSTEM");
            snprintf(fwd.target, sizeof(fwd.target), "SURGERY");
            snprintf(fwd.patient_id, sizeof(fwd.patient_id), "%s", msg.patient_id);

            fwd.timestamp = time(NULL);
            fwd.operation_id = msg.operation_id;

            snprintf(fwd.data, sizeof(fwd.data),
                     "TRANSFER_TO_SURGERY patient=%s",
                     fwd.patient_id);

            send_to_urgent(&fwd);

            // stats
            if (g_stats_shm) {
                pthread_mutex_lock(&g_stats_shm->mutex);
                g_stats_shm->critical_transfers++;
                pthread_mutex_unlock(&g_stats_shm->mutex);
            }

            log_event(LOG_CRITICAL, "SYSTEM",
                      "CRITICAL_FORWARD_SURGERY", fwd.data);
        }
    }

    log_event(LOG_INFO, "SYSTEM", "URGENT_ROUTER", "terminado");
    return NULL;
}

/* -------------------------
   Parsing mínimo de comandos do input_pipe
   ------------------------- */
static void dispatch_emergency(const char *id, int triage, int stability) {
    hospital_message_t msg;
    memset(&msg, 0, sizeof(msg));

    // validações básicas
    if (triage < 1) triage = 1;
    if (triage > 5) triage = 5;
    if (stability < 0) stability = 0;

    msg.msg_type = MSG_NEW_EMERGENCY;

    // prioridade clínica
    msg.mtype = priority_from_triage(triage, stability);

    snprintf(msg.source, sizeof(msg.source), "SYSTEM");
    snprintf(msg.target, sizeof(msg.target), "TRIAGE");
    snprintf(msg.patient_id, sizeof(msg.patient_id), "%.*s",
             (int)sizeof(msg.patient_id) - 1, id);
    msg.timestamp = time(NULL);

    snprintf(msg.data, sizeof(msg.data),
             "EMERGENCY triage=%d stability=%d",
             triage, stability);

    int r;
    if (msg.mtype == 1) r = send_to_urgent(&msg);
    else                r = send_to_normal(&msg); // NORMAL guarda mtype=2/3

    printf("[SYSTEM] ENVIAR EMERGENCY → fila=%s mtype=%ld triage=%d stability=%d\n",
           (msg.mtype == 1) ? "URGENT" : "NORMAL",
           msg.mtype, triage, stability);
    fflush(stdout);

    if (r != 0) perror("[SYSTEM] send emergency");

    stats_inc_emergency();
}

static void dispatch_appointment(const char *id, int triage, int stability, int scheduled) {
    hospital_message_t msg;
    memset(&msg, 0, sizeof(msg));

    if (triage < 1) triage = 1;
    if (triage > 5) triage = 5;
    if (scheduled < 0) scheduled = 0;

    msg.msg_type = MSG_NEW_APPOINTMENT;
    msg.mtype = 3; // consultas -> NORMAL (MQ_NORMAL)

    snprintf(msg.source, sizeof(msg.source), "SYSTEM");
    snprintf(msg.target, sizeof(msg.target), "TRIAGE");
    snprintf(msg.patient_id, sizeof(msg.patient_id), "%.*s",
             (int)sizeof(msg.patient_id) - 1, id);

    msg.timestamp = time(NULL);

    snprintf(msg.data, sizeof(msg.data),
             "APPOINTMENT triage=%d stability=%d scheduled=%d",
             triage, stability, scheduled);

    int r = send_to_normal(&msg);

    printf("[SYSTEM] ENVIAR APPOINTMENT → fila=NORMAL mtype=%ld triage=%d stability=%d scheduled=%d\n",
           msg.mtype, triage, stability, scheduled);
    fflush(stdout);

    if (r != 0) perror("[SYSTEM] send appointment");

    stats_inc_appointment();
}

// Thread que lê o FIFO input_pipe
static void *command_reader(void *arg) {
    (void)arg;

    int fd = open("input_pipe", O_RDWR);
    if (fd == -1) {
        perror("[SYSTEM] open input_pipe");
        g_shutdown_flag = 1;
        return NULL;
    }

    printf("[SYSTEM] command_reader: ligado ao input_pipe (fd=%d)\n", fd);
    log_event(LOG_INFO, "SYSTEM", "CMD_READER", "ligado ao input_pipe");

    char line[512];
    size_t line_len = 0;

    // fallback se APPOINTMENT vier sem "scheduled"
    static int g_next_scheduled = 1;

    while (!g_shutdown_flag) {
        char c;
        ssize_t nread = read(fd, &c, 1);

        if (nread < 0) {
            if (errno == EINTR) continue;
            perror("[SYSTEM] read input_pipe");
            break;
        }
        if (nread == 0) {
            continue;
        }

        if (c != '\n') {
            if (line_len + 1 < sizeof(line)) line[line_len++] = c;
            continue;
        }

        // fecha linha
        line[line_len] = '\0';
        line_len = 0;

        // trim left
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') continue;

        printf("[SYSTEM] CMD_RECEBIDO: '%s'\n", p);
        fflush(stdout);

        if (strncmp(p, "EMERGENCY", 9) == 0) {
            char id[MAX_PATIENT_ID_LENGTH];
            int t, s;

            if (sscanf(p, "EMERGENCY %15s %d %d", id, &t, &s) == 3) {
                dispatch_emergency(id, t, s);
            } else {
                log_event(LOG_WARNING, "SYSTEM", "CMD_INVALID", p);
                printf("[SYSTEM] CMD_INVALID (EMERGENCY): %s\n", p);
                fflush(stdout);
            }

        } else if (strncmp(p, "APPOINTMENT", 11) == 0) {
            char id[MAX_PATIENT_ID_LENGTH];
            int t, s, scheduled;

            int n = sscanf(p, "APPOINTMENT %15s %d %d %d", id, &t, &s, &scheduled);

            if (n == 3) {
                scheduled = g_next_scheduled++;
                dispatch_appointment(id, t, s, scheduled);
            } else if (n == 4) {
                dispatch_appointment(id, t, s, scheduled);
            } else {
                log_event(LOG_WARNING, "SYSTEM", "CMD_INVALID", p);
                printf("[SYSTEM] CMD_INVALID (APPOINTMENT): %s\n", p);
                fflush(stdout);
            }

        } else if (strncmp(p, "STATUS", 6) == 0) {
            display_statistics_console();

        } else {
            log_event(LOG_WARNING, "SYSTEM", "CMD_UNKNOWN", p);
        }
    }

    close(fd);
    printf("[SYSTEM] command_reader: terminou\n");
    return NULL;
}

static void terminate_children(void) {
    if (pid_triage > 0) kill(pid_triage, SIGTERM);
    if (pid_surgery > 0) kill(pid_surgery, SIGTERM);
    if (pid_pharmacy > 0) kill(pid_pharmacy, SIGTERM);
    if (pid_laboratory > 0) kill(pid_laboratory, SIGTERM);

    if (pid_triage > 0) waitpid(pid_triage, NULL, 0);
    if (pid_surgery > 0) waitpid(pid_surgery, NULL, 0);
    if (pid_pharmacy > 0) waitpid(pid_pharmacy, NULL, 0);
    if (pid_laboratory > 0) waitpid(pid_laboratory, NULL, 0);
}

// Encerramento gracioso do sistema:
// - termina threads
// - notifica processos via MQ e FIFOs
// - aguarda filhos
// - guarda estatísticas finais
// - liberta recursos IPC

static void graceful_shutdown(void) {
    log_event(LOG_INFO, "SYSTEM", "SHUTDOWN", "Iniciando encerramento gracioso");

    // parar thread de comandos
    pthread_cancel(th_cmd_reader);
    pthread_join(th_cmd_reader, NULL);

    // mensagem shutdown por MQ (para TRIAGE/routers/listeners que leem MQ)
    hospital_message_t shut;
    memset(&shut, 0, sizeof(shut));
    shut.mtype = 1;
    shut.msg_type = MSG_TYPE_SHUTDOWN;

    snprintf(shut.source, sizeof(shut.source), "SYSTEM");
    snprintf(shut.target, sizeof(shut.target), "ALL");
    shut.timestamp = time(NULL);
    snprintf(shut.data, sizeof(shut.data), "shutdown");

    send_to_urgent(&shut);
    send_to_normal(&shut);
    send_to_responses(&shut);

    // shutdown por PIPE (para processos que agora leem FIFOs)
    // (se não existir leitor ainda, O_NONBLOCK evita bloqueio)
    if (fifo_write_line("triage_pipe",   "SHUTDOWN") == -1) { /* ignora */ }
    if (fifo_write_line("surgery_pipe",  "SHUTDOWN") == -1) { /* ignora */ }
    if (fifo_write_line("pharmacy_pipe", "SHUTDOWN") == -1) { /* ignora */ }
    if (fifo_write_line("lab_pipe",      "SHUTDOWN") == -1) { /* ignora */ }

    // terminar urgent_router e responses_listener
    pthread_join(th_urgent_router, NULL);
    pthread_join(th_responses, NULL);

    // terminar filhos
    terminate_children();

    // snapshot final
    stats_write_snapshot("results/stats_snapshots/final_stats.txt");

    // cleanup
    stats_close(1);
    cleanup_message_queues();
    cleanup_all_sync();
    log_close();
}

// Loop principal do gestor central.
// Reage a sinais:
//  - SIGUSR1: mostrar estatísticas
//  - SIGUSR2: guardar snapshot

int main(void) {
    setbuf(stdout, NULL);   // desativa buffering do stdout
    install_signals();
    create_required_dirs();
    (void)create_required_fifos();

    if (load_config("config/config.txt", &g_config) != 0) {
        set_default_config(&g_config);
        printf("[SYSTEM] Aviso: config.txt não encontrado. Usar defaults.\n");
    }
    printf("[SYSTEM] CONFIG: time_unit_ms=%d triage_critical_stability=%d\n",
       g_config.time_unit_ms, g_config.triage_critical_stability);
    fflush(stdout);


    if (log_init("logs/hospital_log.txt") != 0) {
        fprintf(stderr, "[SYSTEM] Falha a iniciar log\n");
        return 1;
    }

    if (stats_init(1) != 0) {
        fprintf(stderr, "[SYSTEM] Falha a iniciar stats SHM\n");
        return 1;
    }

    if (create_all_message_queues() != 0) {
        fprintf(stderr, "[SYSTEM] Falha a criar message queues\n");
        return 1;
    }

    if (init_all_sync() != 0) {
        fprintf(stderr, "[SYSTEM] Falha a iniciar semáforos\n");
        return 1;
    }

    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    printf("[SYSTEM] Gestor Central iniciado. PID=%d\n", getpid());
    log_event(LOG_INFO, "SYSTEM", "START", "Gestor Central iniciado");

    // Lançar processos principais do sistema (TRIAGE, SURGERY, PHARMACY, LAB)
    pid_triage = fork();
    if (pid_triage == 0) run_triage();

    pid_surgery = fork();
    if (pid_surgery == 0) run_surgery();

    pid_pharmacy = fork();
    if (pid_pharmacy == 0) run_pharmacy();

    pid_laboratory = fork();
    if (pid_laboratory == 0) run_laboratory();

    // thread responses
    if (pthread_create(&th_responses, NULL, responses_listener, NULL) != 0) {
        perror("[SYSTEM] pthread_create responses_listener");
        g_shutdown_flag = 1;
    }

    // thread urgent router
    if (pthread_create(&th_urgent_router, NULL, urgent_router, NULL) != 0) {
        perror("[SYSTEM] pthread_create urgent_router");
        g_shutdown_flag = 1;
    }

    // thread comandos (FIFO)
    if (pthread_create(&th_cmd_reader, NULL, command_reader, NULL) != 0) {
        perror("[SYSTEM] pthread_create command_reader");
        g_shutdown_flag = 1;
    }

    // loop principal reage a SIGUSR1/SIGUSR2
    while (!g_shutdown_flag) {
        if (g_sigusr1_flag) {
            g_sigusr1_flag = 0;
            display_statistics_console();
        }
        if (g_sigusr2_flag) {
            g_sigusr2_flag = 0;
            save_statistics_snapshot();
        }
        usleep(50 * 1000);
    }

    graceful_shutdown();
    return 0;
}

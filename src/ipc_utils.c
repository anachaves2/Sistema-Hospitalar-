#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <fcntl.h>
#include <unistd.h>

#include "ipc.h"

// Implementação dos mecanismos IPC baseados em System V Message Queues
// e Named Pipes, usados para comunicação entre os módulos do sistema.

// Filas principais: URGENT, NORMAL e RESPONSES
static int mq_urgent_id    = -1;
static int mq_normal_id    = -1;
static int mq_responses_id = -1;

// Receção bloqueante e segura de mensagens, ignorando sinais (EINTR)
static int mq_receive_safe(int mq_id, hospital_message_t *msg) {
    if (mq_id == -1) {
        fprintf(stderr, "[IPC] receive: MQ não inicializada\n");
        errno = EINVAL;
        return -1;
    }

    while (1) {
        ssize_t r = msgrcv(
            mq_id,
            msg,
            sizeof(hospital_message_t) - sizeof(long),
            0,
            0
        );

        if (r >= 0) return 0;

        if (errno == EINTR) continue;
        perror("msgrcv");
        return -1;
    }
}

// Receção não bloqueante: retorna 0 se recebeu, 1 se não havia mensagens,
// -1 em caso de erro real
static int mq_receive_nowait(int mq_id, hospital_message_t *msg) {
    if (mq_id == -1) {
        errno = EINVAL;
        return -1;
    }

    ssize_t r = msgrcv(
        mq_id,
        msg,
        sizeof(hospital_message_t) - sizeof(long),
        0,
        IPC_NOWAIT
    );

    if (r >= 0) return 0;

    if (errno == ENOMSG) return 1;  // sem mensagens
    if (errno == EINTR)  return 1;  // não bloqueia

    // erro real
    return -1;
}

int connect_message_queues(void) {
    key_t key_urgent, key_normal, key_resp;

    key_urgent = ftok("config/config.txt", 'U');
    if (key_urgent == -1) { perror("ftok URGENT"); return -1; }

    key_normal = ftok("config/config.txt", 'N');
    if (key_normal == -1) { perror("ftok NORMAL"); return -1; }

    key_resp = ftok("config/config.txt", 'R');
    if (key_resp == -1) { perror("ftok RESPONSES"); return -1; }

    mq_urgent_id = msgget(key_urgent, 0666);
    if (mq_urgent_id == -1) { perror("msgget URGENT"); return -1; }

    mq_normal_id = msgget(key_normal, 0666);
    if (mq_normal_id == -1) { perror("msgget NORMAL"); return -1; }

    mq_responses_id = msgget(key_resp, 0666);
    if (mq_responses_id == -1) { perror("msgget RESPONSES"); return -1; }

    printf("[IPC] (child) connected: URGENT=%d NORMAL=%d RESP=%d\n",
           mq_urgent_id, mq_normal_id, mq_responses_id);

    return 0;
}

int fifo_write_line(const char *fifo_path, const char *line) {
    for (int tries = 0; tries < 20; tries++) {
        int fd = open(fifo_path, O_WRONLY | O_NONBLOCK);
        if (fd != -1) {
            dprintf(fd, "%s\n", line);
            close(fd);
            return 0;
        }

        if (errno == ENXIO) { // ainda não há leitor
            usleep(50 * 1000);
            continue;
        }

        return -1; // erro real
    }

    errno = ENXIO;
    return -1;
}

int receive_from_normal_hi_first(hospital_message_t *msg) {
    if (mq_normal_id == -1) { errno = EINVAL; return -1; }

    // mtype negativo (-3) faz com que o msgrcv devolva a mensagem
    // com o menor mtype positivo <= 3, priorizando mensagens mais urgentes
    ssize_t r = msgrcv(
        mq_normal_id,
        msg,
        sizeof(hospital_message_t) - sizeof(long),
        -3,
        0
    );
    return (r >= 0) ? 0 : -1;
}


int send_message(int mqid, hospital_message_t *msg) {
    if (mqid == -1) {
        fprintf(stderr, "[IPC] send: MQ não inicializada\n");
        errno = EINVAL;
        return -1;
    }

    if (msg->mtype < 1 || msg->mtype > 3) {
        msg->mtype = 3; // default = NORMAL
    }

    if (msgsnd(mqid, msg, sizeof(hospital_message_t) - sizeof(long), 0) == -1) {
        perror("msgsnd");
        return -1;
    }
    return 0;
}

int receive_message_priority(int mqid, hospital_message_t *msg) {
    return mq_receive_safe(mqid, msg);
}

int create_all_message_queues(void) {
    key_t key_urgent, key_normal, key_resp;

    key_urgent = ftok("config/config.txt", 'U');
    if (key_urgent == -1) { perror("ftok URGENT"); return -1; }

    key_normal = ftok("config/config.txt", 'N');
    if (key_normal == -1) { perror("ftok NORMAL"); return -1; }

    key_resp = ftok("config/config.txt", 'R');
    if (key_resp == -1) { perror("ftok RESPONSES"); return -1; }

    mq_urgent_id = msgget(key_urgent, IPC_CREAT | 0666);
    if (mq_urgent_id == -1) { perror("msgget URGENT"); return -1; }

    mq_normal_id = msgget(key_normal, IPC_CREAT | 0666);
    if (mq_normal_id == -1) { perror("msgget NORMAL"); return -1; }

    mq_responses_id = msgget(key_resp, IPC_CREAT | 0666);
    if (mq_responses_id == -1) { perror("msgget RESPONSES"); return -1; }

    printf("[IPC] MQ URGENT id=%d\n", mq_urgent_id);
    printf("[IPC] MQ NORMAL id=%d\n", mq_normal_id);
    printf("[IPC] MQ RESPONSES id=%d\n", mq_responses_id);

    return 0;
}

void cleanup_message_queues(void) {
    if (mq_urgent_id != -1)    msgctl(mq_urgent_id, IPC_RMID, NULL);
    if (mq_normal_id != -1)    msgctl(mq_normal_id, IPC_RMID, NULL);
    if (mq_responses_id != -1) msgctl(mq_responses_id, IPC_RMID, NULL);

    mq_urgent_id = mq_normal_id = mq_responses_id = -1;
}

// wrappers por fila
int send_to_urgent(hospital_message_t *msg)     { return send_message(mq_urgent_id, msg); }
int receive_from_urgent(hospital_message_t *m)  { return receive_message_priority(mq_urgent_id, m); }
int receive_from_urgent_nowait(hospital_message_t *msg) {
    return mq_receive_nowait(mq_urgent_id, msg);
}
int receive_from_urgent_type(hospital_message_t *msg, long type) {
    if (mq_urgent_id == -1) return -1;

    ssize_t r = msgrcv(
        mq_urgent_id,
        msg,
        sizeof(hospital_message_t) - sizeof(long),
        type,          // <<< FILTRO POR mtype
        0
    );
    return (r >= 0) ? 0 : -1;
}


int send_to_normal(hospital_message_t *msg)     { return send_message(mq_normal_id, msg); }
int receive_from_normal(hospital_message_t *m)  { return receive_message_priority(mq_normal_id, m); }
int receive_from_normal_nowait(hospital_message_t *msg) {
    return mq_receive_nowait(mq_normal_id, msg);
}

int send_to_responses(hospital_message_t *msg)  { return send_message(mq_responses_id, msg); }
int receive_from_responses(hospital_message_t *m) { return receive_message_priority(mq_responses_id, m); }

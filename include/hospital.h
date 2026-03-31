#ifndef HOSPITAL_H
#define HOSPITAL_H

#include <time.h>
#include <pthread.h>

#define MAX_PATIENT_ID_LENGTH 16
#define MSG_SURGERY_DONE  777

// ============================
// Message Queues
// Estrutura base de mensagem usada na comunicação inter-processos.
// O campo mtype é utilizado para encaminhamento e/ou prioridade,
// conforme o módulo que envia a mensagem.
// ============================
typedef struct {
    long mtype; // tipo/prioridade da mensagem (dependente do módulo)

    // Cabeçalho
    int msg_type; // tipo lógico da mensagem
    char source[20]; // módulo emissor
    char target[20]; // módulo destinatário
    char patient_id[MAX_PATIENT_ID_LENGTH]; // identificador do paciente
    int operation_id; // id da operação associada
    time_t timestamp; // instante de criação

    // Payload livre (texto simples ou formato estruturado)
    char data[512];
} hospital_message_t;

// Tipos de mensagem
#define MSG_NEW_EMERGENCY        1
#define MSG_NEW_APPOINTMENT      2
#define MSG_NEW_SURGERY          3
#define MSG_PHARMACY_REQUEST     4
#define MSG_LAB_REQUEST          5
#define MSG_PHARMACY_READY       6
#define MSG_LAB_RESULTS_READY    7
#define MSG_CRITICAL_STATUS      8
#define MSG_TRANSFER_PATIENT     9
#define MSG_REJECT_PATIENT       10

// Mensagens internas de controlo
#define MSG_TYPE_SHUTDOWN        99

// ============================
// Estruturas auxiliares
// ============================

// Estrutura que representa um paciente na fila de triagem
// Usada para ordenação, deteção de estado crítico e encaminhamento
typedef struct {
    char id[MAX_PATIENT_ID_LENGTH]; // identificador do paciente
    int triage_level; // nível de triagem atribuído
    int stability; // valor de estabilidade clínica
    int is_critical; // 1 se paciente crítico, 0 caso contrário
    unsigned long arrival_order; // ordem de chegada (para desempates)
    int forwarded_to_surgery;// indica se já foi encaminhado
    int priority; // 1=URGENT, 2=HIGH, 3=NORMAL
    int scheduled_time; // tempo agendado (consultas)
} triage_patient_t;

// ============================
// Tipos de Memória Partilhada (SHM)
// ============================

// Estatísticas globais do sistema (SHM)
// Atualizadas concorrentemente por vários processos,
// protegidas por mutex POSIX partilhado.
typedef struct {
    pthread_mutex_t mutex; // mutex partilhado entre processos
    pthread_mutexattr_t mutex_attr; // atributos do mutex (pshared)

    // Métricas da Triagem
    int total_emergency_patients;
    int total_appointments;
    double total_emergency_wait_time;
    double total_appointment_wait_time;
    int completed_emergencies;
    int completed_appointments;
    int critical_transfers;
    int rejected_patients;

    // Métricas dos Blocos Operatórios
    int total_surgeries_bo1;
    int total_surgeries_bo2;
    int total_surgeries_bo3;
    double total_surgery_wait_time;
    int completed_surgeries;
    int cancelled_surgeries;
    double bo1_utilization_time;
    double bo2_utilization_time;
    double bo3_utilization_time;

    // Métricas da Farmácia
    int total_pharmacy_requests;
    int urgent_requests;
    int normal_requests;
    double total_pharmacy_response_time;
    int stock_depletions;
    int auto_restocks;

    // Métricas dos Laboratórios
    int total_lab_tests_lab1;
    int total_lab_tests_lab2;
    int total_preop_tests;
    double total_lab_turnaround_time;
    int urgent_lab_tests;

    // Métricas globais
    int total_operations;
    int system_errors;
    time_t system_start_time;
    int simulation_time_units;

} global_statistics_t;

//Estado blocos operatórios
typedef struct {
    int room_id;
    int status; // 0=FREE, 1=OCCUPIED, 2=CLEANING
    char current_patient[MAX_PATIENT_ID_LENGTH];
    int surgery_start_time;
    int estimated_end_time;
    pthread_mutex_t mutex;
} surgery_room_t;

typedef struct {
    surgery_room_t rooms[3];
    int medical_teams_available;
    pthread_mutex_t teams_mutex;
} surgery_block_shm_t;

//Stock farmácia
typedef struct {
    char name[30];
    int current_stock;
    int reserved;
    int threshold;
    int max_capacity;
    pthread_mutex_t mutex;
} medication_stock_t;

typedef struct {
    medication_stock_t medications[15];
    int total_active_requests;
    pthread_mutex_t global_mutex;
} pharmacy_shm_t;

//Fila laboratórios
typedef struct {
    char request_id[20];
    char patient_id[MAX_PATIENT_ID_LENGTH];
    int test_type;
    int priority;
    int status;
    time_t request_time;
    time_t completion_time;
} lab_request_entry_t;

typedef struct {
    lab_request_entry_t queue_lab1[50];
    lab_request_entry_t queue_lab2[50];
    int lab1_count;
    int lab2_count;
    int lab1_available_slots; // 0-2
    int lab2_available_slots; // 0-2
    pthread_mutex_t lab1_mutex;
    pthread_mutex_t lab2_mutex;
} lab_queue_shm_t;

//Log eventos críticos
typedef struct {
    time_t timestamp;
    char event_type[30];
    char component[20];
    char description[256];
    int severity; // 1-5
} critical_event_t;

typedef struct {
    critical_event_t events[1000];
    int event_count;
    int current_index;
    pthread_mutex_t mutex;
} critical_log_shm_t;

#endif

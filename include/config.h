#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>

#define MAX_MED_NAME 32
#define MED_COUNT    15

typedef struct {
    char name[MAX_MED_NAME];
    int initial_stock;
    int threshold;
} med_config_t;

typedef struct {
    // ===== Globais =====
    int time_unit_ms; // duração de 1 unidade de simulação (ms)

    int max_emergency_patients; // capacidade máxima de emergências
    int max_appointments; // capacidade máxima de consultas
    int max_surgeries_pending; // cirurgias pendentes máximas

    // ===== Triagem =====
    int triage_simultaneous_patients; // nº máximo de atendimentos simultâneos
    int triage_critical_stability; // estabilidade <= este valor => paciente crítico
    int triage_emergency_duration; // duração do atendimento de emergência (unidades)
    int triage_appointment_duration; // duração do atendimento de consulta (unidades)

    // ===== Blocos operatórios =====
    int bo1_min_duration, bo1_max_duration;
    int bo2_min_duration, bo2_max_duration;
    int bo3_min_duration, bo3_max_duration;

    int cleanup_min_time, cleanup_max_time; // tempo de limpeza (unidades de simulação)
    int max_medical_teams; // nº máximo de equipas médicas disponíveis

    // ===== Farmácia =====
    int pharmacy_preparation_time_min; // tempo de preparação (unidades de simulação)
    int pharmacy_preparation_time_max; // tempo máximo de preparação (unidades)

    int auto_restock_enabled; // (extensão) 1=ativa reposição automática
    int restock_quantity_multiplier; // (extensão) multiplicador aplicado ao threshold

    // ===== Laboratórios =====
    int lab1_test_min_duration, lab1_test_max_duration;
    int lab2_test_min_duration, lab2_test_max_duration;
    int max_simultaneous_tests_lab1;  // nº máximo de testes simultâneos no Lab1
    int max_simultaneous_tests_lab2; // nº máximo de testes simultâneos no Lab2

    // ===== Stock inicial =====
    //Configuração de medicamentos (extensão): nome + stock inicial + threshold
    med_config_t meds[MED_COUNT];

} system_config_t;

// Carrega config/config.txt e faz parsing/validação
int load_config(const char *filename, system_config_t *config);

// Opcional mas útil: preencher defaults se faltar config.txt
void set_default_config(system_config_t *config);

// Config global (usada por todos os módulos)
extern system_config_t g_config;

#endif

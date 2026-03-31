#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "config.h"

/* helper trim */
static char *ltrim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}
static void rtrim(char *s) {
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n-1])) s[--n] = '\0';
}

static int parse_int(const char *v, int *out) {
    if (!v || !*v) return -1;
    char *end = NULL;
    long x = strtol(v, &end, 10);
    if (end == v) return -1;
    *out = (int)x;
    return 0;
}

void set_default_config(system_config_t *c) {
    memset(c, 0, sizeof(*c));

    // Globais
    c->time_unit_ms = 500;
    c->max_emergency_patients = 50;
    c->max_appointments = 100;
    c->max_surgeries_pending = 30;

    // Triagem
    c->triage_simultaneous_patients = 3;
    c->triage_critical_stability = 50;
    c->triage_emergency_duration = 15;
    c->triage_appointment_duration = 10;

    // BO
    c->bo1_min_duration = 50;  c->bo1_max_duration = 100;
    c->bo2_min_duration = 30;  c->bo2_max_duration = 60;
    c->bo3_min_duration = 60;  c->bo3_max_duration = 120;

    c->cleanup_min_time = 10;
    c->cleanup_max_time = 20;
    c->max_medical_teams = 2;

    // Farmácia
    c->pharmacy_preparation_time_min = 5;
    c->pharmacy_preparation_time_max = 10;
    c->auto_restock_enabled = 1;
    c->restock_quantity_multiplier = 2;

    // Labs
    c->lab1_test_min_duration = 10;
    c->lab1_test_max_duration = 20;
    c->lab2_test_min_duration = 15;
    c->lab2_test_max_duration = 30;
    c->max_simultaneous_tests_lab1 = 2;
    c->max_simultaneous_tests_lab2 = 2;

    // meds: deixamos vazio; quando não existir config de meds, o módulo farmácia pode usar defaults próprios
    for (int i = 0; i < MED_COUNT; i++) {
        c->meds[i].name[0] = '\0';
        c->meds[i].initial_stock = 0;
        c->meds[i].threshold = 0;
    }
}

static int set_key_value(system_config_t *c, const char *key, const char *val) {
    // Globais
    if (strcmp(key, "TIME_UNIT_MS") == 0) return parse_int(val, &c->time_unit_ms);
    if (strcmp(key, "MAX_EMERGENCY_PATIENTS") == 0) return parse_int(val, &c->max_emergency_patients);
    if (strcmp(key, "MAX_APPOINTMENTS") == 0) return parse_int(val, &c->max_appointments);
    if (strcmp(key, "MAX_SURGERIES_PENDING") == 0) return parse_int(val, &c->max_surgeries_pending);

    // Triagem
    if (strcmp(key, "TRIAGE_SIMULTANEOUS_PATIENTS") == 0) return parse_int(val, &c->triage_simultaneous_patients);
    if (strcmp(key, "TRIAGE_CRITICAL_STABILITY") == 0) return parse_int(val, &c->triage_critical_stability);
    if (strcmp(key, "TRIAGE_EMERGENCY_DURATION") == 0) return parse_int(val, &c->triage_emergency_duration);
    if (strcmp(key, "TRIAGE_APPOINTMENT_DURATION") == 0) return parse_int(val, &c->triage_appointment_duration);

    // BO
    if (strcmp(key, "BO1_MIN_DURATION") == 0) return parse_int(val, &c->bo1_min_duration);
    if (strcmp(key, "BO1_MAX_DURATION") == 0) return parse_int(val, &c->bo1_max_duration);
    if (strcmp(key, "BO2_MIN_DURATION") == 0) return parse_int(val, &c->bo2_min_duration);
    if (strcmp(key, "BO2_MAX_DURATION") == 0) return parse_int(val, &c->bo2_max_duration);
    if (strcmp(key, "BO3_MIN_DURATION") == 0) return parse_int(val, &c->bo3_min_duration);
    if (strcmp(key, "BO3_MAX_DURATION") == 0) return parse_int(val, &c->bo3_max_duration);

    if (strcmp(key, "CLEANUP_MIN_TIME") == 0) return parse_int(val, &c->cleanup_min_time);
    if (strcmp(key, "CLEANUP_MAX_TIME") == 0) return parse_int(val, &c->cleanup_max_time);
    if (strcmp(key, "MAX_MEDICAL_TEAMS") == 0) return parse_int(val, &c->max_medical_teams);

    // Farmácia
    if (strcmp(key, "PHARMACY_PREPARATION_TIME_MIN") == 0) return parse_int(val, &c->pharmacy_preparation_time_min);
    if (strcmp(key, "PHARMACY_PREPARATION_TIME_MAX") == 0) return parse_int(val, &c->pharmacy_preparation_time_max);
    if (strcmp(key, "AUTO_RESTOCK_ENABLED") == 0) return parse_int(val, &c->auto_restock_enabled);
    if (strcmp(key, "RESTOCK_QUANTITY_MULTIPLIER") == 0) return parse_int(val, &c->restock_quantity_multiplier);

    // Labs
    if (strcmp(key, "LAB1_TEST_MIN_DURATION") == 0) return parse_int(val, &c->lab1_test_min_duration);
    if (strcmp(key, "LAB1_TEST_MAX_DURATION") == 0) return parse_int(val, &c->lab1_test_max_duration);
    if (strcmp(key, "LAB2_TEST_MIN_DURATION") == 0) return parse_int(val, &c->lab2_test_min_duration);
    if (strcmp(key, "LAB2_TEST_MAX_DURATION") == 0) return parse_int(val, &c->lab2_test_max_duration);
    if (strcmp(key, "MAX_SIMULTANEOUS_TESTS_LAB1") == 0) return parse_int(val, &c->max_simultaneous_tests_lab1);
    if (strcmp(key, "MAX_SIMULTANEOUS_TESTS_LAB2") == 0) return parse_int(val, &c->max_simultaneous_tests_lab2);

    // Meds: formato MEDNAME=stock:threshold
    // Se a key não for uma das acima, tentamos interpretar como medicamento
    {
        int stock = 0, thr = 0;
        const char *colon = strchr(val, ':');
        if (!colon) return 0; // chave desconhecida -> ignorar
        stock = atoi(val);
        thr = atoi(colon + 1);

        // inserir na primeira slot vazia
        for (int i = 0; i < MED_COUNT; i++) {
            if (c->meds[i].name[0] == '\0') {
               (void)snprintf(c->meds[i].name, sizeof(c->meds[i].name), "%.*s",(int)sizeof(c->meds[i].name) - 1, key);
                c->meds[i].initial_stock = stock;
                c->meds[i].threshold = thr;
                return 0;
            }
        }
        // se cheio, ignorar
        return 0;
    }
}

int load_config(const char *filename, system_config_t *config) {
    set_default_config(config);

    FILE *f = fopen(filename, "r");
    if (!f) return -1;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *p = ltrim(line);
        rtrim(p);

        // ignorar vazio / comentários
        if (*p == '\0' || *p == '#') continue;

        // cortar comentários inline
        char *hash = strchr(p, '#');
        if (hash) {
            *hash = '\0';
            rtrim(p);
            if (*p == '\0') continue;
        }

        char *eq = strchr(p, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = ltrim(p);
        rtrim(key);

        char *val = ltrim(eq + 1);
        rtrim(val);

        if (*key == '\0' || *val == '\0') continue;

        set_key_value(config, key, val);
    }

    fclose(f);
    return 0;
}

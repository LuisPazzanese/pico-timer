#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"

#define TRIGGER_PIN 17
#define ECHO_PIN 16

typedef enum {
    STATE_STOPPED = 0,
    STATE_RUNNING = 1,
} system_state_t;

typedef struct {
    uint32_t elapsed_s;
    float distance_cm;
    bool is_error;
} reading_t;

static volatile system_state_t system_state = STATE_STOPPED;
static volatile uint32_t period_ms = 3000;
static volatile uint64_t echo_start_us = 0;
static uint64_t start_time_us = 0;
static volatile uint64_t echo_timeout_alarm = 0;
static volatile uint64_t periodic_alarm = 0;
static volatile bool reading_in_progress = false;

static reading_t last_reading = {0, 0.0f, false};
static volatile bool new_reading_available = false;

static float calculate_distance(uint32_t pulse_us) {
    return pulse_us / 58.0f;
}

static void gpio_callback(uint gpio, uint32_t events) {
    if (gpio != ECHO_PIN) return;
    
    uint64_t current_time = time_us_64();
    
    if (events & GPIO_IRQ_EDGE_RISE) {
        echo_start_us = current_time;
    } else if (events & GPIO_IRQ_EDGE_FALL) {
        uint32_t pulse_duration = current_time - echo_start_us;
        float distance_cm = calculate_distance(pulse_duration);
        
        if (echo_timeout_alarm > 0) {
            cancel_alarm(echo_timeout_alarm);
            echo_timeout_alarm = 0;
        }
        
        uint32_t elapsed_s = (current_time - start_time_us) / 1000000;
        
        last_reading.elapsed_s = elapsed_s;
        last_reading.distance_cm = distance_cm;
        last_reading.is_error = false;
        new_reading_available = true;
        
        reading_in_progress = false;
    }
}

static int64_t timeout_alarm_callback(alarm_id_t id, void *user_data) {
    echo_timeout_alarm = 0;
    
    if (system_state == STATE_RUNNING && reading_in_progress) {
        uint64_t current_time = time_us_64();
        uint32_t elapsed_s = (current_time - start_time_us) / 1000000;
        
        last_reading.elapsed_s = elapsed_s;
        last_reading.distance_cm = 0.0f;
        last_reading.is_error = true;
        new_reading_available = true;
        
        reading_in_progress = false;
    }
    
    return 0;
}

static void send_trigger_pulse(void) {
    gpio_put(TRIGGER_PIN, 0);
    sleep_us(2);
    gpio_put(TRIGGER_PIN, 1);
    sleep_us(10);
    gpio_put(TRIGGER_PIN, 0);
}

static void trigger_reading(void) {
    if (reading_in_progress) {
        return;
    }
    
    reading_in_progress = true;
    echo_timeout_alarm = add_alarm_in_ms(30, timeout_alarm_callback, NULL, false);
    send_trigger_pulse();
}

static int64_t periodic_read_alarm_callback(alarm_id_t id, void *user_data) {
    if (system_state == STATE_RUNNING) {
        trigger_reading();
        periodic_alarm = add_alarm_in_ms(period_ms, periodic_read_alarm_callback, NULL, false);
    }
    return 0;
}

static void init_sensor_gpio(void) {
    gpio_init(TRIGGER_PIN);
    gpio_set_dir(TRIGGER_PIN, GPIO_OUT);
    gpio_put(TRIGGER_PIN, 0);
    
    gpio_init(ECHO_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);
    
    gpio_set_irq_enabled_with_callback(ECHO_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
}

static void start_readings(void) {
    if (system_state == STATE_RUNNING) {
        printf("Already running\n");
        return;
    }
    
    system_state = STATE_RUNNING;
    start_time_us = time_us_64();
    printf("Leitura iniciada. Periodo: %u ms\n", period_ms);
    
    periodic_alarm = add_alarm_in_ms(0, periodic_read_alarm_callback, NULL, false);
}

static void stop_readings(void) {
    if (system_state == STATE_STOPPED) {
        printf("Already stopped\n");
        return;
    }
    
    system_state = STATE_STOPPED;
    reading_in_progress = false;
    
    if (echo_timeout_alarm > 0) {
        cancel_alarm(echo_timeout_alarm);
        echo_timeout_alarm = 0;
    }
    
    if (periodic_alarm > 0) {
        cancel_alarm(periodic_alarm);
        periodic_alarm = 0;
    }
    
    printf("Leitura parada\n");
}

static void set_period(uint32_t period_s) {
    if (period_s < 1 || period_s > 3600) {
        printf("Periodo invalido. Use entre 1 e 3600 segundos\n");
        return;
    }
    
    period_ms = period_s * 1000;
    printf("Periodo definido para %u segundos\n", period_s);
}

static void process_command(char *cmd) {
    if (cmd == NULL || strlen(cmd) == 0) {
        return;
    }
    
    if (strncmp(cmd, "start", 5) == 0) {
        start_readings();
    } else if (strncmp(cmd, "stop", 4) == 0) {
        stop_readings();
    } else if (strncmp(cmd, "periodo", 7) == 0) {
        char *period_str = cmd + 7;
        while (*period_str && (*period_str == ' ' || *period_str == ':')) {
            period_str++;
        }
        uint32_t period_s = atoi(period_str);
        set_period(period_s);
    } else if (strncmp(cmd, "help", 4) == 0 || strncmp(cmd, "?", 1) == 0) {
        printf("Comandos disponiveis:\n");
        printf("  start             - Inicia a leitura\n");
        printf("  stop              - Para a leitura\n");
        printf("  periodo <seg>     - Define periodo em segundos\n");
        printf("  help              - Mostra este texto\n");
    } else {
        printf("Comando desconhecido. Digite 'help' para ver opcoes\n");
    }
}

static void read_serial_input(void) {
    static char cmd_buffer[64];
    static int cmd_index = 0;
    
    int c = getchar_timeout_us(100);
    
    if (c != PICO_ERROR_TIMEOUT && c != '\n' && c != '\r') {
        if (cmd_index < (int)sizeof(cmd_buffer) - 1) {
            cmd_buffer[cmd_index++] = (char)c;
            printf("%c", c);
        }
    } else if (c == '\n' || c == '\r') {
        if (cmd_index > 0) {
            cmd_buffer[cmd_index] = '\0';
            printf("\n");
            process_command(cmd_buffer);
            cmd_index = 0;
        }
        printf("> ");
    }
}

static void print_pending_readings(void) {
    if (new_reading_available) {
        if (last_reading.is_error) {
            printf("%us - Falha\n", last_reading.elapsed_s);
        } else {
            printf("%us - %.1f cm\n", last_reading.elapsed_s, last_reading.distance_cm);
        }
        new_reading_available = false;
    }
}

int main() {
    stdio_init_all();
    
    init_sensor_gpio();
    
    printf("\n=== HC-SR04 Sensor via Timer ===\n");
    printf("Digite 'help' para ver comandos disponiveis\n");
    printf("> ");
    
    while (true) {
        read_serial_input();
        print_pending_readings();
        sleep_us(100);
    }
    
    return 0;
}

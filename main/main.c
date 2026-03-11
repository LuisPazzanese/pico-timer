#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"

#define TRIGGER_PIN 14
#define ECHO_PIN 15
#define BUFFER_SIZE 32

/// System state for IRQ communication
struct {
    volatile uint64_t echo_rise, echo_fall;
    volatile alarm_id_t alarm_id;
    volatile bool reading_in_progress;
    volatile uint32_t elapsed_time_s;
    volatile uint32_t reading_period_ms; ///< read in periodic_alarm_callback (IRQ)
    volatile alarm_id_t periodic_alarm_id, timer_alarm_id;
    volatile bool system_active;          ///< read in periodic_alarm_callback (IRQ)
    volatile bool should_read;
    volatile int last_distance;
    volatile bool last_read_failed;
} sys = {.reading_period_ms = 3000};

#define ECHO_RISE sys.echo_rise
#define ECHO_FALL sys.echo_fall
#define alarm_id sys.alarm_id
#define reading_in_progress sys.reading_in_progress
#define elapsed_time_s sys.elapsed_time_s
#define reading_period_ms sys.reading_period_ms
#define periodic_alarm_id sys.periodic_alarm_id
#define timer_alarm_id sys.timer_alarm_id
#define system_active sys.system_active
#define should_read sys.should_read
#define last_distance sys.last_distance
#define last_read_failed sys.last_read_failed

void send_trigger_pulse();

int64_t timer_callback(alarm_id_t id, void *user_data) {
    elapsed_time_s++;
    timer_alarm_id = add_alarm_in_ms(1000, timer_callback, NULL, false);
    return 0;
}

int64_t alarm_callback(alarm_id_t id, void *user_data) {
    reading_in_progress = false;
    last_read_failed = true;
    alarm_id = 0;
    return 0;
}

int64_t periodic_alarm_callback(alarm_id_t id, void *user_data) {
    if (system_active) {
        should_read = true;
    }
    periodic_alarm_id = add_alarm_in_ms(reading_period_ms, periodic_alarm_callback, NULL, false);
    return 0;
}

void send_trigger_pulse(){
    reading_in_progress = true;
    alarm_id = add_alarm_in_ms(50, alarm_callback, NULL, false);
    gpio_put(TRIGGER_PIN, 0);
    sleep_us(2);
    gpio_put(TRIGGER_PIN, 1);
    sleep_us(10);
    gpio_put(TRIGGER_PIN, 0);
}

void gpio_callback(uint gpio, uint32_t events) {
    if (events & GPIO_IRQ_EDGE_RISE) {
        ECHO_RISE = time_us_64();
    }
    if (events & GPIO_IRQ_EDGE_FALL) {
        ECHO_FALL = time_us_64();
        uint64_t pulse = ECHO_FALL - ECHO_RISE;
        
        if (pulse > 30000) {
            return;
        }
        
        last_distance = pulse * 0.0343 / 2;
        
        if (alarm_id > 0) {
            cancel_alarm(alarm_id);
            alarm_id = 0;
        }
        
        last_read_failed = false;
    }
}

void process_serial_commands() {
    static char cmd_buffer[BUFFER_SIZE];
    static int cmd_index = 0;
    
    int ch = getchar_timeout_us(0);
    
    if (ch == PICO_ERROR_TIMEOUT) {
        return;
    }
    
    if (ch == '\r' || ch == '\n') {
        printf("\n");
        if (cmd_index > 0) {
            cmd_buffer[cmd_index] = '\0';
            
            if (strcmp(cmd_buffer, "start") == 0) {
                if (!system_active) {
                    system_active = true;
                    printf(">> Sistema iniciado. Primeira leitura em 3s...\n");
                    should_read = true;
                } else {
                    printf(">> Sistema já está ativo\n");
                }
            } else if (strcmp(cmd_buffer, "stop") == 0) {
                if (system_active) {
                    system_active = false;
                    printf(">> Sistema parado\n");
                } else {
                    printf(">> Sistema já está parado\n");
                }
            } else if (strncmp(cmd_buffer, "period ", 7) == 0) {
                uint32_t new_period = atoi(&cmd_buffer[7]);
                if (new_period > 0) {
                    reading_period_ms = new_period * 1000;
                    printf(">> Período configurado para %u segundos\n", new_period);
                } else {
                    printf(">> Período inválido (use: period <segundos>)\n");
                }
            } else {
                printf(">> Comando desconhecido: '%s'\n", cmd_buffer);
                printf(">> Comandos: start | stop | period <seg>\n");
            }
            
            cmd_index = 0;
        }
        return;
    } 
    
    if (ch >= 32 && ch <= 126 && cmd_index < BUFFER_SIZE - 1) {
        cmd_buffer[cmd_index++] = (char)ch;
        printf("%c", ch);
    }
}


int main() {
    stdio_init_all();

    gpio_init(TRIGGER_PIN);
    gpio_set_dir(TRIGGER_PIN, GPIO_OUT);
    gpio_init(ECHO_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);
    gpio_disable_pulls(ECHO_PIN);
    gpio_set_irq_enabled_with_callback(ECHO_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, gpio_callback);
    
    periodic_alarm_id = add_alarm_in_ms(reading_period_ms, periodic_alarm_callback, NULL, false);
    timer_alarm_id = add_alarm_in_ms(1000, timer_callback, NULL, false);
    
    printf("Sistema de leitura de distância iniciado\n");
    printf("Digite 'start' para iniciar ou 'stop' para parar\n");
    printf("Use 'period <segundos>' para configurar o período\n");
    
    bool last_displayed = false;
    
    while (true) {
        process_serial_commands();
        
        if (should_read) {
            should_read = false;
            last_displayed = false;
            send_trigger_pulse();
        }
        
        if (last_read_failed && !last_displayed) {
            last_displayed = true;
            printf("%us - Falha\n", elapsed_time_s);
        }
        
        if (!last_read_failed && !last_displayed && !reading_in_progress) {
            last_displayed = true;
            printf("%us - %d cm\n", elapsed_time_s, last_distance);
        }
        
        sleep_ms(10);
    }
}

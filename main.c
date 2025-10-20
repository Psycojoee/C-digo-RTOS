/*
 * Checkpoint 2 - Sistema de Dados Robusto
 * Aluno: THIAGO MARQUES
 * RM: 88049
 */

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_task_wdt.h"

// --- Definições do Projeto ---
#define TAMANHO_FILA 5
#define TIMEOUT_NIVEL_1 5
#define TIMEOUT_NIVEL_2 10
#define TIMEOUT_NIVEL_3 15 // Nível de falha para reset total

// Bits (flags) para o Event Group
#define TASK_GERADOR_OK_BIT (1 << 0)
#define TASK_CONSUMIDOR_OK_BIT (1 << 1)

// --- Handles Globais ---
QueueHandle_t filaDeDados = NULL;
EventGroupHandle_t flagsDeStatus = NULL;

// --- Constantes de Delay ---
const TickType_t xDelay1000ms = pdMS_TO_TICKS(1000);
const TickType_t xDelay2000ms = pdMS_TO_TICKS(2000);


// MÓDULO 1: Geração de Dados
void task_gerador (void *pvParameters)
{
    // Adiciona esta tarefa ao WDT
    esp_task_wdt_add(NULL); 

    int valor_atual = 0; 

    for(;;)
    {
        // 1. Gera o dado
        valor_atual++;

        // 2. Tenta enviar para a fila
        if(xQueueSend(filaDeDados, &valor_atual, 0) != pdTRUE)
        {
            // Falha: Fila cheia
            printf("{THIAGO MARQUES-RM:88049} [ERRO_FILA] Fila cheia, dado %d descartado.\n", valor_atual);
        }
        else
        {
            // Sucesso: Imprime a linha única (gerado e enviado)
            printf("{THIAGO MARQUES-RM:88049} [GERACAO] Dado %d gerado e enviado.\n", valor_atual);
            xEventGroupSetBits(flagsDeStatus, TASK_GERADOR_OK_BIT);
        }

        // 3. Alimenta o Watchdog
        esp_task_wdt_reset();
        vTaskDelay(xDelay1000ms);
    }
}

// MÓDULO 2: Recepção de Dados
void task_consumidor(void *pvParameters)
{
    // Adiciona esta tarefa ao WDT
    esp_task_wdt_add(NULL); 

    int dadoRecebido = 0;
    int timeouts_consecutivos = 0;

    for(;;)
    {
        // 1. Tenta receber da fila
        if(xQueueReceive(filaDeDados, &dadoRecebido, 0) == pdTRUE)
        {
            // 2. Aloca memória (como pedido no PDF)
            int *ptrDadoTemp = (int *) malloc(sizeof(int));

            // 3. Verifica falha do malloc
            if(ptrDadoTemp == NULL)
            {
                printf("{THIAGO MARQUES-RM:88049} [ERRO_MEM] Falha ao alocar memoria (malloc) no RECEPTOR.\n");
                vTaskDelay(xDelay1000ms);
                continue; 
            }

            // 4. Armazena, transmite (printa) e libera
            *ptrDadoTemp = dadoRecebido;
            printf("{THIAGO MARQUES-RM:88049} [CONSUMO] Dado %d lido da fila.\n", *ptrDadoTemp);
            
            timeouts_consecutivos = 0; // Zera o contador de falhas
            xEventGroupSetBits(flagsDeStatus, TASK_CONSUMIDOR_OK_BIT);
            
            free(ptrDadoTemp);
            ptrDadoTemp = NULL;
        }
        else
        {
            // 5. Fila vazia, incrementa o timeout
            timeouts_consecutivos++;
            printf("{THIAGO MARQUES-RM:88049} [CONSUMO_WARN] Fila vazia (timeout: %d)\n", timeouts_consecutivos);
        }

        // 6. Reação escalonada
        if(timeouts_consecutivos == TIMEOUT_NIVEL_1) // Nível 1: Aviso
        {
            printf("{THIAGO MARQUES-RM:88049} [TIMEOUT_LV1] %ds sem dados. Apenas aviso.\n", TIMEOUT_NIVEL_1);
        }
        else if(timeouts_consecutivos == TIMEOUT_NIVEL_2) // Nível 2: Recuperação
        {
            printf("{THIAGO MARQUES-RM:88049} [TIMEOUT_LV2] %ds sem dados. Resetando fila.\n", TIMEOUT_NIVEL_2);
            xQueueReset(filaDeDados); // Limpa a fila
        }
        else if (timeouts_consecutivos >= TIMEOUT_NIVEL_3) // Nível 3: Reset Total
        {
            // Não é mais granular, agora reinicia o ESP
            printf("{THIAGO MARQUES-RM:88049} [TIMEOUT_LV3] %ds sem dados. Falha persistente. Reiniciando sistema...\n", TIMEOUT_NIVEL_3);
            vTaskDelay(pdMS_TO_TICKS(1000)); // Delay para o print
            esp_restart(); // Reinicia o ESP
        }

        // 7. Alimenta o WDT (Correção do bug de crash)
        // Alimenta o WDT todo ciclo, mesmo se a fila estiver vazia.
        esp_task_wdt_reset();

        vTaskDelay(xDelay2000ms);
    } 
}

// MÓDULO 3: Supervisão
void task_monitor (void *pvParameters)
{
    // Adiciona esta tarefa ao WDT
    esp_task_wdt_add(NULL); 

    for(;;)
    {
        // O Bloco de "Ressurreição" foi removido

        // 1. Aguarda (sem bloquear) pelos bits de status
        EventBits_t bitsLidos = xEventGroupWaitBits(
            flagsDeStatus,
            TASK_GERADOR_OK_BIT | TASK_CONSUMIDOR_OK_BIT,
            pdTRUE,  // Limpa os bits após a leitura
            pdFALSE, // Modo OR
            0        // Não bloqueia
        );

        // 2. Reporta o status
        if((bitsLidos & TASK_GERADOR_OK_BIT) && (bitsLidos & TASK_CONSUMIDOR_OK_BIT))
        {
            printf("{THIAGO MARQUES-RM:88049} [MONITOR] Status: Sistema OK (Ambas Tasks ativas)\n");
        }
        else if(bitsLidos & TASK_GERADOR_OK_BIT)
        {
            printf("{THIAGO MARQUES-RM:88049} [MONITOR] Status: Falha (Apenas Task Geradora ativa)\n");
        }
        else if(bitsLidos & TASK_CONSUMIDOR_OK_BIT)
        {
            printf("{THIAGO MARQUES-RM:88049} [MONITOR] Status: Falha (Apenas Task Consumidora ativa)\n");
        }
        else
        {
            printf("{THIAGO MARQUES-RM:88049} [MONITOR] Status: Falha Critica (Nenhuma task ativa)\n");
        }

        // 3. Alimenta o Watchdog
        esp_task_wdt_reset();
        
        vTaskDelay(xDelay2000ms);
    }
}

void app_main(void)
{
    // 1. Configuração do WDT
    esp_task_wdt_config_t configWDT = {
        .timeout_ms = 5000, 
        .idle_core_mask = (1 << 0) | (1 << 1),
        .trigger_panic = true
    };
    esp_task_wdt_init(&configWDT);

    // 2. Criação da Fila e Event Group
    filaDeDados = xQueueCreate(TAMANHO_FILA, sizeof(int));
    flagsDeStatus = xEventGroupCreate();

    if(filaDeDados == NULL || flagsDeStatus == NULL)
    {
        printf("{THIAGO MARQUES-RM:88049} [ERRO_CRITICO] Falha ao criar fila ou event group. Reiniciando.\n");
        vTaskDelay(xDelay1000ms);
        esp_restart();
    }

    // 3. Criação das Tasks
    // Não precisamos mais dos handles globais
    xTaskCreate(task_gerador, "TaskGeradora", 4096, NULL, 5, NULL);
    xTaskCreate(task_consumidor, "TaskConsumidora", 4096, NULL, 5, NULL);
    xTaskCreate(task_monitor, "TaskSupervisora", 4096, NULL, 5, NULL);

    // 4. Adiciona as tasks ao WDT
    // (Não é mais necessário, pois as tarefas se adicionam sozinhas)
}
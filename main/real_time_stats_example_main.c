/* FreeRTOS Real Time Stats Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <stdlib.h>
#include "string.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
//#include "TinyGsmClient.h"
#include "driver/uart.h"

#define NUM_OF_SPIN_TASKS   6
#define SPIN_ITER           500000  //Actual CPU cycles used will depend on compiler optimization
#define SPIN_TASK_PRIO      2
#define STATS_TASK_PRIO     3
#define STATS_TASK_PRIOO     1
#define STATS_TICKS         pdMS_TO_TICKS(1000)
#define ARRAY_SIZE_OFFSET   5   //Increase this if print_real_time_stats returns ESP_ERR_INVALID_SIZE
#define BLINK_GPIO          12
#define DTR_GPIO            25

//Define para SIM7070

#define TINY_GSM_MODEM_SIM7070
#define UART_BAUD           9600
#define PIN_TX              27
#define PIN_RX              26
#define BUF_SIZE (1024)
#define ROT_BUF_SIZE (1024)
#define EX_UART_NUM UART_NUM_2
#define PATTERN_CHR_NUM    (3) /*!< Set the number of consecutive and identical characters received by receiver which defines a UART pattern*/

static char task_names[NUM_OF_SPIN_TASKS][configMAX_TASK_NAME_LEN];
static SemaphoreHandle_t sync_spin_task;
static SemaphoreHandle_t sync_stats_task;
QueueHandle_t xQueueCaboGPS;
static QueueHandle_t uart0_queue;

uart_config_t uart_config = {
    .baud_rate = UART_BAUD,
    .data_bits = UART_DATA_8_BITS,
    .parity    = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .source_clk = UART_SCLK_APB,
};

/**
 * @brief   Function to print the CPU usage of tasks over a given duration.
 *
 * This function will measure and print the CPU usage of tasks over a specified
 * number of ticks (i.e. real time stats). This is implemented by simply calling
 * uxTaskGetSystemState() twice separated by a delay, then calculating the
 * differences of task run times before and after the delay.
 *
 * @note    If any tasks are added or removed during the delay, the stats of
 *          those tasks will not be printed.
 * @note    This function should be called from a high priority task to minimize
 *          inaccuracies with delays.
 * @note    When running in dual core mode, each core will correspond to 50% of
 *          the run time.
 *
 * @param   xTicksToWait    Period of stats measurement
 *
 * @return
 *  - ESP_OK                Success
 *  - ESP_ERR_NO_MEM        Insufficient memory to allocated internal arrays
 *  - ESP_ERR_INVALID_SIZE  Insufficient array size for uxTaskGetSystemState. Trying increasing ARRAY_SIZE_OFFSET
 *  - ESP_ERR_INVALID_STATE Delay duration too short
 */

// Basic Code
static esp_err_t print_real_time_stats(TickType_t xTicksToWait)
{
    TaskStatus_t *start_array = NULL, *end_array = NULL;
    UBaseType_t start_array_size, end_array_size;
    uint32_t start_run_time, end_run_time;
    esp_err_t ret;

    //Allocate array to store current task states
    start_array_size = uxTaskGetNumberOfTasks() + ARRAY_SIZE_OFFSET;
    start_array = malloc(sizeof(TaskStatus_t) * start_array_size);
    if (start_array == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto exit;
    }
    //Get current task states
    start_array_size = uxTaskGetSystemState(start_array, start_array_size, &start_run_time);
    if (start_array_size == 0) {
        ret = ESP_ERR_INVALID_SIZE;
        goto exit;
    }

    vTaskDelay(xTicksToWait);

    //Allocate array to store tasks states post delay
    end_array_size = uxTaskGetNumberOfTasks() + ARRAY_SIZE_OFFSET;
    end_array = malloc(sizeof(TaskStatus_t) * end_array_size);
    if (end_array == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto exit;
    }
    //Get post delay task states
    end_array_size = uxTaskGetSystemState(end_array, end_array_size, &end_run_time);
    if (end_array_size == 0) {
        ret = ESP_ERR_INVALID_SIZE;
        goto exit;
    }

    //Calculate total_elapsed_time in units of run time stats clock period.
    uint32_t total_elapsed_time = (end_run_time - start_run_time);
    if (total_elapsed_time == 0) {
        ret = ESP_ERR_INVALID_STATE;
        goto exit;
    }

    printf("| Task | Run Time | Percentage\n");
    //Match each task in start_array to those in the end_array
    for (int i = 0; i < start_array_size; i++) {
        int k = -1;
        for (int j = 0; j < end_array_size; j++) {
            if (start_array[i].xHandle == end_array[j].xHandle) {
                k = j;
                //Mark that task have been matched by overwriting their handles
                start_array[i].xHandle = NULL;
                end_array[j].xHandle = NULL;
                break;
            }
        }
        //Check if matching task found
        if (k >= 0) {
            uint32_t task_elapsed_time = end_array[k].ulRunTimeCounter - start_array[i].ulRunTimeCounter;
            uint32_t percentage_time = (task_elapsed_time * 100UL) / (total_elapsed_time * portNUM_PROCESSORS);
            printf("| %s | %d | %d%%\n", start_array[i].pcTaskName, task_elapsed_time, percentage_time);
        }
    }

    //Print unmatched tasks
    for (int i = 0; i < start_array_size; i++) {
        if (start_array[i].xHandle != NULL) {
            printf("| %s | Deleted\n", start_array[i].pcTaskName);
        }
    }
    for (int i = 0; i < end_array_size; i++) {
        if (end_array[i].xHandle != NULL) {
            printf("| %s | Created\n", end_array[i].pcTaskName);
        }
    }
    ret = ESP_OK;

exit:    //Common return path
    free(start_array);
    free(end_array);
    return ret;
}

// Basic Code
static void spin_task(void *arg)
{
    xSemaphoreTake(sync_spin_task, portMAX_DELAY);
    while (1) {
        //Consume CPU cycles
        for (int i = 0; i < SPIN_ITER; i++) {
            __asm__ __volatile__("NOP");
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Basic Code
static void stats_task(void *arg)
{
    xSemaphoreTake(sync_stats_task, portMAX_DELAY);

    //Start all the spin tasks
    for (int i = 0; i < NUM_OF_SPIN_TASKS; i++) {
        xSemaphoreGive(sync_spin_task);
    }

    //Print real time stats periodically
    while (1) {
        printf("\n\nGetting real time stats over %d ticks\n", STATS_TICKS);
        if (print_real_time_stats(STATS_TICKS) == ESP_OK) {
            printf("Real time stats obtained\n");
        } else {
            printf("Error getting real time stats\n");
        }
        xSemaphoreGive(sync_stats_task);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Task Blink para teste (OMM)
static void blink_tsk(void *arg)
{
    xSemaphoreTake(sync_stats_task, portMAX_DELAY);

    //Inicio das tasks tipo spin
    for (int i = 0; i< NUM_OF_SPIN_TASKS; i++){
        xSemaphoreGive(sync_spin_task);
    }

    //Blink de LED
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_DEF_OUTPUT);
    while (1)
    {
        /* code */
        //printf("Turning off the LED\n");
        gpio_set_level(BLINK_GPIO,0);
        xSemaphoreGive(sync_stats_task);
        vTaskDelay(pdMS_TO_TICKS(500));
        //printf("Turning on the LED\n");
        gpio_set_level(BLINK_GPIO,1);
        xSemaphoreGive(sync_stats_task);
        vTaskDelay(pdMS_TO_TICKS(500));
        
    }
}

// Definições para Task sendReceive
typedef struct GPS_Inf
{
 char UTCdt[18];
 char latit[10];
 char longi[11];
 char status[1024];
} GPSDados;

typedef enum{
    COMPARE_NONE=0,
    COMPARE_EQUAL,
    COMPARE_RETURN,
    COMPARE_CONTAINS,
}COMPARE;
char recBuff[512];
#define sendReceiveBuff() (char *)&recBuff[0]

// Task sendReceive
int sendReceive(char * sendCmd, char * waitResp, int trys, COMPARE bCompare)
{
    int len;
    int idx = 0; 
    int trysTmp=0;
    char *recStr=0;
    
    //Queue formato: GPSDados
    //GPSDados *pvEnvio = malloc(sizeof(GPSDados));
    len = strlen(sendCmd);
    if (len > 256) {
        return -1;
    }
    if (len == 0) {
        return -2;
    }

    len = strlen(waitResp);
    if (len > 256) {
        return -3;
    }

    if (bCompare > COMPARE_CONTAINS)
        return -4;
    
    // Envia
    //uart_flush(UART_NUM_2);
    printf("%s\n", sendCmd);
    uart_write_bytes(UART_NUM_2, sendCmd, strlen(sendCmd));
    //uart_write_bytes(EX_UART_NUM, sendCmd, UART_DATA);
    uart_wait_tx_done(UART_NUM_2, pdMS_TO_TICKS(100));
    xSemaphoreGive(sync_stats_task);
    vTaskDelay(pdMS_TO_TICKS(1777));
    /*
    do {

        trysTmp++;

        // Pega byte a byte e trata
        while (uart_read_bytes(UART_NUM_2, (char *)&recBuff[idx], 1, pdMS_TO_TICKS(50)) > 0) {
            if ((recBuff[idx]=='\r') || (recBuff[idx]=='\n') || (recBuff[idx]=='\0')) {
                if (idx>1) {
                    recBuff[idx] = '\0';
                    //printf("\t%s\n", recBuff);
                    strcpy(pvEnvio->status, recBuff);
                    xQueueSend(xQueueCaboGPS, pvEnvio, 1000);

                    // Cai fora quandoi receber qualquer coisa e não queira esperar algo
                    if ((bCompare == COMPARE_NONE) || (strlen(waitResp) == 0)) {
                        return 0;
                    }
                    
                    // Compara o recebido com o esperado
                    else if (bCompare == COMPARE_EQUAL) {
                        if (strcmp(recBuff, waitResp) == 0) {
                            return (int)strlen(recBuff);
                        }
                    }

                    // Compara o recebido com o esperado
                    else if (bCompare == COMPARE_CONTAINS) {
                        recStr = strstr(recBuff, waitResp);
                        if (recStr != 0) {
                            return (int)strlen(recBuff);
                        }
                    }

                    //Retorna a resposta para ser tratada.
                    else if (bCompare == COMPARE_RETURN){
                        //strcpy(pvEnvio->status, recBuff);
                        printf("\t%s\n", recBuff);
                        //xQueueSend(xQueueCaboGPS, pvEnvio, 1000);                        
                        return (int)strlen(recBuff);
                    }
                }
                // Recebeu o que não queria, reinicia
                idx=0;
                // Incrementa byte a string de recebimento
            } 
            else {
                idx++;
            }
        }

        // Aguarda e faz timeout
        xSemaphoreGive(sync_stats_task);
        vTaskDelay(pdMS_TO_TICKS(100));
        if (trysTmp >= trys) {
            return -5;
        } else {
            printf("\t(Tentativa %d)\n", trysTmp);
        }
    }while(1);
    */
    return -99;
}

// Função Reset do módulo GSM
void GSM_Reset(int tock)
{
    if(tock == 2)
    {
        gpio_set_level(4,1);
        xSemaphoreGive(sync_stats_task);
        vTaskDelay(pdMS_TO_TICKS(1500));
        gpio_set_level(4,0);
        printf("\rReset Modem GSM\n");
        xSemaphoreGive(sync_stats_task);
        vTaskDelay(pdMS_TO_TICKS(5000));
        gpio_set_level(4,1);
        xSemaphoreGive(sync_stats_task);
        vTaskDelay(pdMS_TO_TICKS(1500));
        gpio_set_level(4,0);
        xSemaphoreGive(sync_stats_task);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    else if (tock == 1)
    {
        printf("\rTurn ON/OFF Modem GSM\n");
        gpio_set_level(4,1);
        xSemaphoreGive(sync_stats_task);
        vTaskDelay(pdMS_TO_TICKS(1500));
        gpio_set_level(4,0);
        xSemaphoreGive(sync_stats_task);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    else
    {
        printf("\rInvalid Reset\n");
    }
}
/*
void Berb (void *arg)
{
    while(endLine)
    {
        rocStr = strstr(dtmp, "\nOK");
        //Não localizou
        bzero(temp2, ROT_BUF_SIZE);
        if (rocStr == 0) 
        {
            strcpy(temp2,dtmp);
            strcat(pvEnvio->status, temp2);
            context ++;
        }
        else if(context == 5)
        {
            endLine = false;
        }
        else
        {
            endLine = false;
        }
    }
}*/

//char rotBuff[1024];
char dtmp[ROT_BUF_SIZE];

// Task leitura automatica serial
static void UART_SIM(void *arg)
{
    //Inicio das tasks tipo spin
    for (int i = 0; i< NUM_OF_SPIN_TASKS; i++){
        xSemaphoreGive(sync_spin_task);
    }
    int UARTSt = 0;
    bool endLine = true;
    int posReadUART = 0;    

    //GSM_Reset(2);
    //Queue formato: GPSDados
    GPSDados *pvEnvio = malloc(sizeof(GPSDados));

    uart_event_t event;
    size_t buffered_size;
    //uint8_t* dtmp = (uint8_t*) malloc(ROT_BUF_SIZE);
    while (1)
    {
        //printf(".U.\n");
        /* code */
        if(xQueueReceive(uart0_queue, (void * )&event, (TickType_t)portMAX_DELAY)) {
            bzero(dtmp, ROT_BUF_SIZE);
            bzero(pvEnvio->status, 1024);            
            switch(event.type) {
                //Event of UART receving data
                /*We'd better handler data event fast, there would be much more data events than
                other types of events. If we take too much time on data event, the queue might
                be full.*/
                case UART_DATA:
                    //printf("NewUART\n");
                    uart_read_bytes(EX_UART_NUM,(char *)&dtmp, event.size, portMAX_DELAY);                    
                    strcpy(pvEnvio->status, dtmp);
                    if(xQueueReceive(uart0_queue, (void * )&event, (TickType_t)portMAX_DELAY))
                    {
                        uart_read_bytes(EX_UART_NUM,(char *)&dtmp, event.size, portMAX_DELAY);
                        strcat(pvEnvio->status, dtmp);
                    }                        
                    //xQueueSend(xQueueCaboGPS, pvEnvio, 1000);
                    xQueueOverwrite(xQueueCaboGPS,pvEnvio);
                    printf("prepos: %s\n end\n", pvEnvio->status);                    
                    //uart_write_bytes(EX_UART_NUM, (const char*) dtmp, event.size);
                    break;
                //Event of HW FIFO overflow detected
                case UART_FIFO_OVF:
                    //ESP_LOGI(TAG, "hw fifo overflow");
                    printf("hw fifo overflow\n");
                    // If fifo overflow happened, you should consider adding flow control for your application.
                    // The ISR has already reset the rx FIFO,
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(EX_UART_NUM);
                    xQueueReset(uart0_queue);
                    break;
                //Event of UART ring buffer full
                case UART_BUFFER_FULL:
                    //ESP_LOGI(TAG, "ring buffer full");
                    printf("ring buffer full\n");
                    // If buffer full happened, you should consider encreasing your buffer size
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(EX_UART_NUM);
                    xQueueReset(uart0_queue);
                    break;
                //Event of UART RX break detected
                case UART_BREAK:
                    //ESP_LOGI(TAG, "uart rx break");
                    printf("uart rx break\n");
                    break;
                //Event of UART parity check error
                case UART_PARITY_ERR:
                    //ESP_LOGI(TAG, "uart parity error");
                    printf("uart parity error\n");
                    break;
                //Event of UART frame error
                case UART_FRAME_ERR:
                    //ESP_LOGI(TAG, "uart frame error");
                    printf("uart frame error\n");
                    break;
                //UART_PATTERN_DET
                case UART_PATTERN_DET:
                    uart_get_buffered_data_len(EX_UART_NUM, &buffered_size);
                    int pos = uart_pattern_pop_pos(EX_UART_NUM);
                    //ESP_LOGI(TAG, "[UART PATTERN DETECTED] pos: %d, buffered size: %d", pos, buffered_size);
                    printf("[UART PATTERN DETECTED] pos: %d, buffered size: %d\n", pos, buffered_size);
                    if (pos == -1) {
                        // There used to be a UART_PATTERN_DET event, but the pattern position queue is full so that it can not
                        // record the position. We should set a larger queue size.
                        // As an example, we directly flush the rx buffer here.
                        uart_flush_input(EX_UART_NUM);
                    } else {
                        uart_read_bytes(EX_UART_NUM, dtmp, pos, 100 / portTICK_PERIOD_MS);
                        uint8_t pat[PATTERN_CHR_NUM + 1];
                        memset(pat, 0, sizeof(pat));
                        uart_read_bytes(EX_UART_NUM, pat, PATTERN_CHR_NUM, 100 / portTICK_PERIOD_MS);
                        //ESP_LOGI(TAG, "read data: %s", dtmp);
                        //ESP_LOGI(TAG, "read pat : %s", pat);
                        printf("read data: %s\n", dtmp);
                        printf("read pat : %s\n", pat);
                    }
                    break;
                //Others
                default:
                    //ESP_LOGI(TAG, "uart event type: %d", event.type);
                    printf("uart event type: %d\n", event.type);
                    break;
            }
        }
        //xSemaphoreGive(sync_stats_task);
        //vTaskDelay(pdMS_TO_TICKS(250));
    }
}

static void GSM_C(void *arg)
{
    xSemaphoreTake(sync_stats_task, portMAX_DELAY);
    int16_t *datap = (int16_t *) malloc(BUF_SIZE);
    //Inicio das tasks tipo spin
    for (int i = 0; i< NUM_OF_SPIN_TASKS; i++){
        xSemaphoreGive(sync_spin_task);
    }
    printf("p1\n");
    int errc = 0;
    /*
    // Set serial ESP32 e SIM7070G       
    uart_param_config(UART_NUM_2, &uart_config);
    uart_set_pin(UART_NUM_2, PIN_TX, PIN_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM_2, BUF_SIZE * 2, 0, 0, NULL, 0);
    */
    //Reset Modem GSM
    //gpio_reset_pin(4);
    //gpio_set_direction(4, GPIO_MODE_DEF_OUTPUT);    

    GSM_Reset(2);
    xSemaphoreGive(sync_stats_task);
    vTaskDelay(pdMS_TO_TICKS(5000));

    printf("p2\n");
    int uaband = UART_BAUD;
    int len = 0;
    uint8_t redeb = 0;
    char mensagem[256];
    char msgtotal [256];
    sprintf(mensagem, "AT+IPR=%i\r", uaband);
    len = uart_read_bytes(UART_NUM_2, datap, BUF_SIZE, pdMS_TO_TICKS(100));

    //Set Baud rate
    errc = 0;
    printf("Set auto-baud rate\n");
    while (redeb == 0)
    {
        uart_write_bytes(UART_NUM_2, (char *) mensagem, strlen(mensagem));        
        len = uart_read_bytes(UART_NUM_2, datap, BUF_SIZE, pdMS_TO_TICKS(100));
        if (len > 0)
        {
            printf("Leitura: %d\n", len);      
            printf("%ls \n", datap);            
            printf("Set auto-baud rate\n");
            bzero(datap,1024);
            len = uart_read_bytes(UART_NUM_2, datap, BUF_SIZE, pdMS_TO_TICKS(100));
            printf("Leitura: %d\n", len);
            redeb = 1;
        }
        else if(errc == 50)
            GSM_Reset(1);
        else
        {
            printf(" .");
            errc++;
        }        
        xSemaphoreGive(sync_stats_task);
        vTaskDelay(pdMS_TO_TICKS(2500));
    }
    printf("Baud rate configurado.\n");
    xSemaphoreGive(sync_stats_task);
    vTaskDelay(pdMS_TO_TICKS(50));
    //uart_write_bytes(UART_NUM_2, (const char *) "AT+IPR=115200\n", 12);
    
    // Desativar ECHO (eco)
    sprintf(mensagem, "ATE0\r");
    printf("Escrita ECHO");
    errc = 0;
    redeb = 0;
    len = 0;
    while (redeb == 0)
    {
        uart_write_bytes(UART_NUM_2, (char *) mensagem, strlen(mensagem));
        uart_wait_tx_done(UART_NUM_2, pdMS_TO_TICKS(500));
        if (len > 0)
        {
            printf("Leitura: %d\n", len);      
            printf("%ls", datap);            
            bzero(datap,1024);
            len = uart_read_bytes(UART_NUM_2, datap, BUF_SIZE, pdMS_TO_TICKS(100));
            //printf("Leitura: %d\n", len);
            redeb = 1;
        }
        else if( errc == 15)
            GSM_Reset(1);
        else
        {
            printf(" .");
            errc++;
        }
        len = uart_read_bytes(UART_NUM_2, datap, BUF_SIZE, pdMS_TO_TICKS(100));
        xSemaphoreGive(sync_stats_task);
        vTaskDelay(pdMS_TO_TICKS(2000));                 
    }
    xSemaphoreGive(sync_stats_task);
    vTaskDelay(pdMS_TO_TICKS(500));
    // Inicio principais funções
    errc = 0;
    bool checking = true;
    int ack=0;
    int state=0;    
    GPSDados *caboGPS = malloc(sizeof(GPSDados));
    char *verif = 0;    
    int col = 0;
    int bg = 0;
    int vtst = 0;
    int ret = 0;
    char data[25];
    char netp[25];
    char bands[50];
    char ver[25];     
    struct datasimpl
    {
        char latitu[25];
        char longitu[25];
        char ano[5];
        char mes[3];
        char dia[3];
        char horap[6];
    };
    struct datasimpl GPSuser;
    printf("p3\n");
    state = 3;
    while (1)
    {
        xSemaphoreGive(sync_stats_task);
        vTaskDelay(pdMS_TO_TICKS(1500));        
        switch (state)
        {
        case 0:
            //printf("p4\n");
            ack = sendReceive("AT+CGNSPWR?\r", "",3, COMPARE_RETURN);
            //printf("p5\n");
            //ack = sendReceive("AT+CPSI?\r", "", 3, COMPARE_NONE);
            xQueueReceive(xQueueCaboGPS, caboGPS, 300);
            printf("Status GPS:\n%s\n", caboGPS->status);
            verif = strstr(caboGPS->status, "0");
            if(verif != 0)
            {
                ack = sendReceive("AT+CGNSPWR=1\r", "",3, COMPARE_NONE);
                verif = 0;
            }
            else
                state = 1;
            break;
        case 1:
            ack = sendReceive("AT+CGNSINF\r", "",3, COMPARE_RETURN);
            xQueueReceive(xQueueCaboGPS, caboGPS, 300);
            //printf("Status GPS:\n%s\n", caboGPS->status);
            //sprintf(mensagem, "ATE0\r");
            //
            //
            state = 2;
            break;
        case 2:
            sprintf(mensagem, caboGPS->status);
            
            if(mensagem[6] == 'N')
            {
                col = 0;
                bg = 0;
                // Tratamento baseado na mesagem padrão enviada pelo módulo, segue exemplo modificado abaixo:
                // +CGNSINF: 1,1,20220212223745.000,-00.000000,-00.000000,591.395,0.00,,0,,1.0,1.4,0.9,,10,,3.6,4.0
                for(int i = 0; mensagem[i] != '\0'; i++ )
                {
                    if(mensagem[i] == ',')
                    {                        
                        col++;
                        bg = i + 1;                        
                    }
                    else if (col == 2)
                    {
                        data[i - bg] = mensagem[i];
                        if(mensagem[i+1] == ',')
                        {                        
                            data[i + 1 - bg] = '\0';                   
                        }                
                    }
                    else if (col == 3)
                    {
                        GPSuser.latitu[i - bg] = mensagem[i];
                        if(mensagem[i+1] == ',')
                        {                        
                            GPSuser.latitu[i + 1 - bg] = '\0';                   
                        } 
                    }
                    else if (col == 4)
                    {
                        GPSuser.longitu[i - bg] = mensagem[i];
                        if(mensagem[i+1] == ',')
                        {                        
                            GPSuser.longitu[i + 1 - bg] = '\0';                   
                        } 
                    }
                }
                if(strlen(data) != 18)
                {
                    printf("Sincronizando GPS...\n");
                }
                else
                {
                    for(int i = 0; i < 14; i++)
                    {
                        if(i <= 3)
                        {
                            GPSuser.ano[i] = data[i];
                            if(i + 1 == 4)
                            {
                                GPSuser.ano[i + 1] = '\0';
                            }
                        }
                        else if (i > 3 && i < 6)
                        {
                            GPSuser.mes[i-4] = data[i];
                            if(i + 1 == 6)
                            {
                                GPSuser.mes[i - 3] = '\0';
                            }
                        }
                        else if (i > 5 && i < 8)
                        {
                            GPSuser.dia[i-6] = data[i];
                            if(i + 1 == 8)
                            {
                                GPSuser.dia[i - 5] = '\0';
                            }
                        }
                        else if (i > 7)
                        {
                            GPSuser.horap[i-8] = data[i];
                            if(i + 1 == 14)
                            {
                                GPSuser.horap[i - 7] = '\0';
                            }
                        }
                    }
                    // Linhas de Teste
                    printf("Dados: \n");
                    //printf("Data: %s \n", data);
                    printf("Hora, Dia, Mes, Ano \n %s %s/%s/%s \n", GPSuser.horap, GPSuser.dia, GPSuser.mes, GPSuser.ano);
                    printf("Latitude: %s \n", GPSuser.latitu);
                    printf("Longitude: %s \n", GPSuser.longitu);
                    vtst++;
                    //     
                }
            }
            else
                printf("FAIL\n");
            if(vtst == 3)
            {
                state = 3;
                vtst = 0;
            }                
            else                      
                state = 1;
            break;
        case 3:
            // Desligamento do GPS para trabalhar com LTE.
            ack = sendReceive("AT+CGNSPWR?\r", "",3, COMPARE_RETURN);
            xSemaphoreGive(sync_stats_task);                        
            xQueueReceive(xQueueCaboGPS, caboGPS, 300);
            printf("Status LTE:\n%s\n", caboGPS->status);
            verif = strstr(caboGPS->status, "1");
            if(verif != 0)
            {
                ack = sendReceive("AT+CGNSPWR=0\r", "",3, COMPARE_NONE);
                verif = 0;
            }
            else
                state = 4;
            break;
        case 4:
            // Verificação LTE.
            ack = sendReceive("AT+CGNSPWR?\r", "",3, COMPARE_RETURN);            
            xQueueReceive(xQueueCaboGPS, caboGPS, 300);
            printf("Status GPS:\n%s\n", caboGPS->status);
            verif = strstr(caboGPS->status, "1");
            if(verif != 0)
            {
                ack = sendReceive("AT+CGNSPWR=0\r", "",3, COMPARE_NONE);
                xSemaphoreGive(sync_stats_task);
                vTaskDelay(pdMS_TO_TICKS(703));
                verif = 0;
            }
            else
                state = 5;
            break;
        case 5:
            col = 0;
            bg = 0;
            ack = sendReceive("AT+CPSI?\r", "",3, COMPARE_RETURN);
            xQueueReceive(xQueueCaboGPS, caboGPS, 300);
            printf("\n%s\n", caboGPS->status);
            sprintf(mensagem, caboGPS->status);
            for(int i = 0; mensagem[i] != '\0'; i++ )
            {
                if(mensagem[i] == ':')
                {
                    col++;
                    bg = i;                    
                }
                else if(mensagem[i+2] == ',')
                {
                    col++;
                }
                if(col == 1)
                {
                    netp[i - bg] = mensagem[i+2];
                    if(mensagem[i+3] == ',')
                    {                        
                        netp[i + 1 - bg] = '\0';                   
                    }
                }
                else if (col == 2)
                {                    
                    //state = 5;
                    ret = strcmp(netp,"NO SERVICE");
                    if(ret == 0)
                    {
                        state = 5;
                        printf("Msg: %s\n", netp);
                    }
                        
                    else
                    {
                        state = 6;
                        printf("No Compare %s\n", netp);
                    }
                        
                }
            }
            /*if(vtst == 3)
            {
                //state =5;
                vtst = 0;
            }                
            else
                vtst++;*/
            break;
        case 6:
            col = 0;
            bg = 0;
            checking = true;
            ack = sendReceive("AT+CBANDCFG?\r", "",3, COMPARE_RETURN);
            //vTaskDelay(pdMS_TO_TICKS(5883));            
            bzero(msgtotal, strlen(msgtotal));
            do
            {
                /* code */
                xQueueReceive(xQueueCaboGPS, caboGPS, 300);
                sprintf(mensagem, caboGPS->status);
                for(int i = 0; mensagem[i] != '\0'; i++ )
                {
                    if(mensagem[i] == '\n' && mensagem[i+1] == 'O' && mensagem[i+2] == 'K')
                    {
                        checking = false;
                    }
                }
                strcat(msgtotal, mensagem);
                vTaskDelay(pdMS_TO_TICKS(83));
            } while (checking);
            strcpy(mensagem, msgtotal);
            printf("Status BANDAS:\n%s\n", msgtotal);
            //xQueueReceive(xQueueCaboGPS, caboGPS, 300);
            //printf("Status BANDAS:\n%s\n", caboGPS->status);
            //sprintf(mensagem, caboGPS->status);
            for(int i = 0; mensagem[i] != '\0'; i++ )
            {
                if(mensagem[i] == ':')
                {
                    col++;
                    bg = i;                    
                }
                else if(mensagem[i+2] == ',' && col == 1)
                {
                    col++;
                }
                if(col == 1)
                {
                    ver[i - bg] = mensagem[i+2];
                    if(mensagem[i+3] == ',')
                    {                        
                        ver[i + 1 - bg] = '\0';                   
                    }
                }
                else if (col == 2)
                {
                    ret = 0;
                    //state = 5;
                    ret = strcmp(ver,"\"CAT-M\"");
                    if(ret == 0)
                    {
                        state = 5;
                        //printf("Msg: %s\n", ver);
                        
                    }
                        
                    else
                    {
                        state = 5;
                        //printf("No Compare %s\n", ver);
                    }
                        
                }
            }
            //state = 5;
            break;
        case 7:
            /*col = 0;
            bg = 0;
            ack = sendReceive("AT+CBANDCFG?\r", "",3, COMPARE_RETURN);
            xQueueReceive(xQueueCaboGPS, caboGPS, 300);
            sprintf(mensagem, caboGPS->status);
            printf("NetWork:\n%s\n", caboGPS->status);
             for(int i = 0; mensagem[i] != '\0'; i++ )
            {
                if(mensagem[i] == ',')
                {
                    bg = i;
                    netp[i - bg] = mensagem[i -1];
                    netp[i - bg + 1] = mensagem[i];
                    netp[i - bg + 2] = mensagem[i + 2];
                    netp[i - bg + 3] = '\0';
                        if(mensagem[i+1] == ',' || mensagem[i+2] == '\0')
                        {                        
                            data[i + 1 - bg] = '\0';                   
                        }
                }
            }*/
            
            ack = sendReceive("AT+CFUN=1,0\r", "",3, COMPARE_RETURN);
            vTaskDelay(pdMS_TO_TICKS(703));
            ack = sendReceive("AT+CGDCONT=1,\"IP\",\"java.claro.com.br\",\"0.0.0.0\"\r", "",3, COMPARE_RETURN);
            //ack = sendReceive("AT+CNACT=0,1\r", "",3, COMPARE_RETURN);
            //vTaskDelay(pdMS_TO_TICKS(703));
            ack = sendReceive("AT+CGPADDR\r", "",3, COMPARE_RETURN);
            //vTaskDelay(pdMS_TO_TICKS(703));
            ack = sendReceive("AT+CGDCONT?\r", "",3, COMPARE_RETURN);
            //vTaskDelay(pdMS_TO_TICKS(703));
            //ack = sendReceive("AT+CACID=0\r", "",3, COMPARE_RETURN);
            //vTaskDelay(pdMS_TO_TICKS(703));
            ack = sendReceive("AT+CNCFG=0,1,\"IoTLog\"\r", "",3, COMPARE_RETURN);
            //vTaskDelay(pdMS_TO_TICKS(1703));
            ack = sendReceive("AT+CGACT=1,1\r", "",3, COMPARE_RETURN);
            //vTaskDelay(pdMS_TO_TICKS(1703));
            ack = sendReceive("AT+CGACT?\r", "",3, COMPARE_RETURN);
            //vTaskDelay(pdMS_TO_TICKS(1703));
            ack = sendReceive("AT+CPSI?\r", "",3, COMPARE_RETURN);
            //vTaskDelay(pdMS_TO_TICKS(1703));
            ack = sendReceive("AT+CGPADDR\r", "",3, COMPARE_RETURN);
            //vTaskDelay(pdMS_TO_TICKS(1703));
            ack = sendReceive("AT+CNACT=0,1\r", "",3, COMPARE_RETURN);
            
            /*
            //ack = sendReceive("AT+CFUN=0\r", "",3, COMPARE_RETURN);
            //vTaskDelay(pdMS_TO_TICKS(2703));
            ack = sendReceive("AT+CGDCONT=1,\"IP\",\"IoTLog\" \r", "",3, COMPARE_RETURN);
            vTaskDelay(pdMS_TO_TICKS(1703));
            //ack = sendReceive("AT+CFUN=1,0\r", "",3, COMPARE_RETURN);
            //vTaskDelay(pdMS_TO_TICKS(2703));
            ack = sendReceive("AT+CGATT?\r", "",3, COMPARE_RETURN);
            vTaskDelay(pdMS_TO_TICKS(1503));
            ack = sendReceive("AT+CGNAPN\r", "",3, COMPARE_RETURN);
            vTaskDelay(pdMS_TO_TICKS(1703));
            ack = sendReceive("AT+CNCFG=0,1,\"IoTLog\"\r", "",3, COMPARE_RETURN);
            vTaskDelay(pdMS_TO_TICKS(1703));
            ack = sendReceive("AT+CGNAPN\r", "",3, COMPARE_RETURN);
            vTaskDelay(pdMS_TO_TICKS(1703));
            ack = sendReceive("AT+CNACT=0,1\r", "",3, COMPARE_RETURN);
            vTaskDelay(pdMS_TO_TICKS(1703));
            ack = sendReceive("AT+CNACT?\r", "",3, COMPARE_RETURN);
            */
            /*
            ack = sendReceive("AT+CPIN?\r", "READY",3, COMPARE_EQUAL);
            vTaskDelay(pdMS_TO_TICKS(1703));
            ack = sendReceive("AT+CSQ\r", "",3, COMPARE_RETURN);
            vTaskDelay(pdMS_TO_TICKS(1703));
            ack = sendReceive("AT+CGATT?\r", "",3, COMPARE_RETURN);
            vTaskDelay(pdMS_TO_TICKS(1703));
            ack = sendReceive("AT+COPS?\r", "",3, COMPARE_RETURN);
            vTaskDelay(pdMS_TO_TICKS(1703));
            ack = sendReceive("AT+CGNAPN\r", "",3, COMPARE_RETURN);
            vTaskDelay(pdMS_TO_TICKS(1703));
            ack = sendReceive("AT+CNCFG=0,1,\"IoTLog\"\r", "",3, COMPARE_RETURN);
            vTaskDelay(pdMS_TO_TICKS(1703));
            ack = sendReceive("AT+CNACT=0,1\r", "",3, COMPARE_RETURN);
            vTaskDelay(pdMS_TO_TICKS(1703));
            ack = sendReceive("AT+CNACT? \r", "",3, COMPARE_RETURN);
            */

            vTaskDelay(pdMS_TO_TICKS(1703));
            ack = sendReceive("AT+SNPDPID=0\r", "",3, COMPARE_RETURN);
            for(int i = 0; i<2;i++)
            {
                vTaskDelay(pdMS_TO_TICKS(1703));
                ack = sendReceive("AT+SNPING4=\"8.8.8.8\",5,1,20000\r", "",3, COMPARE_RETURN);
            }
            if(vtst >= 0)
            {
                state =8;
                vtst = 0;
            }                
            else
                vtst++;
            break;
        case 8:
            ack = sendReceive("AT+CGREG?\r", "",3, COMPARE_RETURN);
            if(vtst >= 0)
            {
                state =10;
                vtst = 0;
            }                
            else
                vtst++;
            break; 
        case 9:
            //ack = sendReceive("AT+SMCONF?\r", "",3, COMPARE_RETURN);
            //ack = sendReceive("AT+SMCONF=?\r", "",3, COMPARE_RETURN);
            vTaskDelay(pdMS_TO_TICKS(503));            
            if(vtst == 0)
            {
                ack = sendReceive("AT+SMCONF=\"URL\",\"mqtt3.thingspeak.com\",\"1883\"\r", "",3, COMPARE_RETURN);             
            }
            else if(vtst == 1)
            {
                ack = sendReceive("AT+SMCONF=\"KEEPTIME\",60\r", "",3, COMPARE_RETURN);             
            }
            else if(vtst == 2)
            {
                ack = sendReceive("AT+SMCONF=\"CLEANSS\",1\r", "",3, COMPARE_RETURN);                
            }
            else if(vtst == 3)
            {
                ack = sendReceive("AT+SMCONF=\"CLIENTID\",\"Exw1Ni8LOS8IKQsVCzAtNQY\"\r", "",3, COMPARE_RETURN);         
            }
            else if(vtst == 4)
            {
                ack = sendReceive("AT+SMCONF=\"QOS\",0\r", "",3, COMPARE_RETURN);             
            } 
            else if(vtst == 5)
            {
                ack = sendReceive("AT+SMCONF=\"TOPIC\",\"channels/1639540/publish\"\r", "",3, COMPARE_RETURN);                            
            }
            else if(vtst == 6)
            {
                ack = sendReceive("AT+SMCONF=\"USERNAME\",\"Exw1Ni8LOS8IKQsVCzAtNQY\"\r", "",3, COMPARE_RETURN);             
            }
            else if(vtst == 7)
            {
                ack = sendReceive("AT+SMCONF=\"PASSWORD\",\"WBaqO3TrzAwA5e75ScpKVL12\"\r", "",3, COMPARE_RETURN);            
            }
            else if(vtst == 8)
            {
                state = 10;
                vtst = 0;          
            }                   
            else
            {
                vtst = -1;
            }                
            vtst++;
            break;
         case 10:
            //ack = sendReceive("AT+CGNAPN\r", "",3, COMPARE_RETURN);
            ack = sendReceive("AT+CNACT?\r", "",3, COMPARE_RETURN);    
            vTaskDelay(pdMS_TO_TICKS(1703));
            //ack = sendReceive("AT+SMCONN\r", "",3, COMPARE_RETURN);
            ack = sendReceive("AT+CPSI?\r", "",3, COMPARE_RETURN);
            if(vtst >= 0)
            {
                state = 11;
                vtst = 0;
            }                
            else
                vtst++;
            break;
        case 11:
            //ack = sendReceive("AT+SMCONN\r", "",3, COMPARE_RETURN);
            if(vtst >= 0)
            {
                state =5;
                vtst = 0;
            }                
            else
                vtst++;
            break;
        default:
            state=0;
            continue;
        }
    }    
}
/*
void xTaskFunction (void * pvParameters)
{
    xSemaphoreTake(sync_stats_task, portMAX_DELAY);
    xSemaphoreGive(sync_stats_task);
    for(;;)
    {
        printf("Hello Wolrd\n");
        //xSemaphoreGive(sync_stats_task);
        vTaskDelay(pdMS_TO_TICKS(1803));
    }    
}
*/

void app_main(void)
{
    //Allow other core to finish initialization
    vTaskDelay(pdMS_TO_TICKS(100)); 

    // Set serial ESP32 e SIM7070G       
    uart_param_config(EX_UART_NUM, &uart_config);
    uart_set_pin(EX_UART_NUM, PIN_TX, PIN_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    //uart_driver_install(EX_UART_NUM, BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_driver_install(EX_UART_NUM, BUF_SIZE * 2, BUF_SIZE * 2, 20, &uart0_queue, 0);
    uart_flush(EX_UART_NUM);

    //Set uart pattern detect function.
    uart_enable_pattern_det_baud_intr(EX_UART_NUM, '+', PATTERN_CHR_NUM, 9, 0, 0);
    //Reset the pattern queue length to record at most 20 pattern positions.
    uart_pattern_queue_reset(EX_UART_NUM, 20);
    
    // Set GPIO
    gpio_reset_pin(4);
    gpio_set_direction(4, GPIO_MODE_DEF_OUTPUT); 

    //Create semaphores to synchronize
    sync_spin_task = xSemaphoreCreateCounting(NUM_OF_SPIN_TASKS, 0);
    sync_stats_task = xSemaphoreCreateBinary();

    //Create spin tasks
    for (int i = 0; i < NUM_OF_SPIN_TASKS; i++) {
        snprintf(task_names[i], configMAX_TASK_NAME_LEN, "spin%d", i);
        xTaskCreatePinnedToCore(spin_task, task_names[i], 1024, NULL, SPIN_TASK_PRIO, NULL, tskNO_AFFINITY);
    }

    // Criacão Queues    
    struct GPS_Inf *pxMessage;
    xQueueCaboGPS = xQueueCreate(1, 2*sizeof(struct GPS_Inf));
    if(xQueueCaboGPS == 0){
        for(;;){printf("\nERROR QUEUE CABOGPS CREATE\n");}   
    }
    printf("\nQUEUE PASS\n");
    
    // ********** Criacão de Tasks **********

    //xTaskCreate(xTaskFunction,"TaskTest", 16000, NULL, STATS_TASK_PRIO, NULL );    
    //Create and start stats task
    xTaskCreatePinnedToCore(blink_tsk, "blinkOMM1", 4096, NULL, STATS_TASK_PRIO, NULL, tskNO_AFFINITY);
    //xTaskCreatePinnedToCore(stats_task, "stats", 4096, NULL, STATS_TASK_PRIO, NULL, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(GSM_C, "GSM", 4096, NULL, STATS_TASK_PRIO, NULL, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(UART_SIM, "SERIAL", 4096, NULL, 11, NULL, tskNO_AFFINITY);
    //xTaskCreate(UART_SIM, "SERIAL", 4096, NULL, 2, NULL);
    printf("TASK CREATE PASS\n");
    
    xSemaphoreGive(sync_stats_task);
    //vTaskStartScheduler();
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}



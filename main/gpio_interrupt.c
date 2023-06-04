#include <stdio.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

#include <string.h>
#include <stdlib.h>
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_wpa2.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_smartconfig.h"

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
static const int CONNECTED_BIT = BIT0;
static const int ESPTOUCH_DONE_BIT = BIT1;
static const char *TAG = "smartconfig_example";

/* Is it the first time to Smartconfig ? */
bool firstConfig = true;

static void smartconfig_example_task(void * parm);

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        xTaskCreate(smartconfig_example_task, "smartconfig_example_task", 4096, NULL, 3, NULL);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE) {
        ESP_LOGI(TAG, "Scan done");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL) {
        ESP_LOGI(TAG, "Found channel");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD) {
        ESP_LOGI(TAG, "Got SSID and password");

        smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
        wifi_config_t wifi_config;
        uint8_t ssid[33] = { 0 };
        uint8_t password[65] = { 0 };
        uint8_t rvd_data[33] = { 0 };

        bzero(&wifi_config, sizeof(wifi_config_t));
        memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
        memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
        wifi_config.sta.bssid_set = evt->bssid_set;
        if (wifi_config.sta.bssid_set == true) {
            memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
        }

        memcpy(ssid, evt->ssid, sizeof(evt->ssid));
        memcpy(password, evt->password, sizeof(evt->password));
        ESP_LOGI(TAG, "SSID:%s", ssid);
        ESP_LOGI(TAG, "PASSWORD:%s", password);
        if (evt->type == SC_TYPE_ESPTOUCH_V2) {
            ESP_ERROR_CHECK( esp_smartconfig_get_rvd_data(rvd_data, sizeof(rvd_data)) );
            ESP_LOGI(TAG, "RVD_DATA:");
            for (int i=0; i<33; i++) {
                printf("%02x ", rvd_data[i]);
            }
            printf("\n");
        }

        ESP_ERROR_CHECK( esp_wifi_disconnect() );
        ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
        esp_wifi_connect();
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE) {
        xEventGroupSetBits(s_wifi_event_group, ESPTOUCH_DONE_BIT);
    }
}

static void initialise_wifi(void)
{
    /* If it's the first time to Smartconfig, config anything related to esp wifi */
    if(firstConfig){
        ESP_ERROR_CHECK(esp_netif_init());
        s_wifi_event_group = xEventGroupCreate();
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
        assert(sta_netif);

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK( esp_wifi_init(&cfg) );

        ESP_ERROR_CHECK( esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );
        ESP_ERROR_CHECK( esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL) );
        ESP_ERROR_CHECK( esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );

        firstConfig = false;
    }
    /* If not, then just stop the last wifi and restart it */
    else {
        esp_wifi_stop();
    }

    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_start() );
}

static void smartconfig_example_task(void * parm)
{
    EventBits_t uxBits;
    ESP_ERROR_CHECK( esp_smartconfig_set_type(SC_TYPE_ESPTOUCH) );
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_smartconfig_start(&cfg) );
    while (1) {
        uxBits = xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT, true, false, portMAX_DELAY);
        if(uxBits & CONNECTED_BIT) {
            ESP_LOGI(TAG, "WiFi Connected to ap");
        }
        if(uxBits & ESPTOUCH_DONE_BIT) {
            ESP_LOGI(TAG, "smartconfig over");
            esp_smartconfig_stop();
            vTaskDelete(NULL);
        }
    }
}

#define ESP_INTR_FLAG_DEFAULT 0
 
#define BLINK_LED 2
#define GPIO_INPUT_IO_0 34
volatile int buttonCount = 0;
int i = 0;

const char *TAG1 = "INT";
 
SemaphoreHandle_t xSemaphore = NULL;
 
TaskHandle_t printVariableTask = NULL;
 
void printVariable(void *pvParameter) {
 
    int a = (int) pvParameter;
    while (1) {
 
        printf("A is a: %d \n", a++);
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
}
// interrupt service routine, called when the button is pressed
void IRAM_ATTR button_isr_handler(void* arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    // notify the button task
    xSemaphoreGiveFromISR(xSemaphore, &xHigherPriorityTaskWoken );
    //ESP_LOGI(TAG, "ISR!");

    if (xHigherPriorityTaskWoken) {
    portYIELD_FROM_ISR();
  }
 
}
// task that will react to button clicks
void button_task(void* arg) {
 
    // infinite loop
    int64_t count_time = 0;
    for(;;) {
 
        // wait for the notification from the ISR
        if(xSemaphoreTake(xSemaphore,portMAX_DELAY) == pdTRUE) {
            //vTaskSuspendAll();
            if (buttonCount % 2 == 0) {
                count_time = esp_timer_get_time();
            }
            else {
                if (esp_timer_get_time() - count_time >= 3000000) {
                    ESP_LOGI(TAG1, "ISR!");
                    initialise_wifi();
                }
            }
            buttonCount++;
            
            //xTaskResumeAll();
        }
    }
}



void app_main()
{

    ESP_ERROR_CHECK( nvs_flash_init() );
    // create the binary semaphore
    xSemaphore = xSemaphoreCreateBinary();
 
    // configure button and led pins as GPIO pins
    
    gpio_pad_select_gpio(BLINK_LED);
 
    // set the correct direction
    
    gpio_set_direction(BLINK_LED, GPIO_MODE_OUTPUT);

    gpio_pad_select_gpio(GPIO_INPUT_IO_0);
    gpio_set_direction(GPIO_INPUT_IO_0, GPIO_MODE_INPUT);

    gpio_pulldown_en(GPIO_INPUT_IO_0);
    gpio_pullup_dis(GPIO_INPUT_IO_0);
 
    // enable interrupt on falling (1->0) edge for button pin
    gpio_set_intr_type(GPIO_INPUT_IO_0, GPIO_INTR_ANYEDGE);
 
    // start the task that will handle the button
    xTaskCreate(button_task, "button_task", 1024 * 3, NULL, 10, NULL);
 
    // install ISR service with default configuration
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
 
    // attach the interrupt service routine
    gpio_isr_handler_add(GPIO_INPUT_IO_0, button_isr_handler, NULL);
 
    int pass = 25;
    xTaskCreate(&printVariable, "printVariable", 2048, (void*) pass, 5, &printVariableTask);
 
}
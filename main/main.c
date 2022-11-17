#include 	<stdio.h>
#include	<string.h>
#include	"esp_event.h"	//	for usleep

#include	"neopixel.h"
#include	<esp_log.h>
#include <math.h>

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "freertos/task.h"
#include "protocol_examples_common.h"

#include <esp_http_server.h>

#define GPIO_INPUT_IO_0    0
#define ESP_INTR_FLAG_DEFAULT 0
//#define	NR_LED		3
#include "esp_log.h"
#include "esp_console.h"

#include "data.h"

static const char *TAG = "XMasStar";


static void IRAM_ATTR gpio_isr_handler(void* arg) {
	//printf("INT should go to xQueue\r\n");

  if (++mode==4)
    mode=0;
}

// 0 means "off" - nonzero means "on" - errors default to "off"
bool load_powerState() {
    esp_err_t err;
    unsigned char v;
    nvs_handle_t my_handle;
    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    ESP_ERROR_CHECK(err);
    err = nvs_get_u8(my_handle,"powerState",&v);
    if (err != ESP_OK)
      v=0;
    nvs_close(my_handle);
    return (v!=0);
}
short load_displayMode() {
    esp_err_t err;
    unsigned char v;
    nvs_handle_t my_handle;
    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    ESP_ERROR_CHECK(err);
    err = nvs_get_u8(my_handle,"displayMode",&v);
    if (err != ESP_OK)
      v=MODE_AUTOADVANCE;
    nvs_close(my_handle);
    return (v);
}

void save_powerState() {
    esp_err_t err;
    nvs_stats_t nvs_stats;
    nvs_handle_t my_handle;
    nvs_get_stats(NULL, &nvs_stats);
    printf("BEFORE Save Count: UsedEntries = (%d), FreeEntries = (%d), AllEntries = (%d)\n",
        nvs_stats.used_entries, nvs_stats.free_entries, nvs_stats.total_entries);
    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    ESP_ERROR_CHECK(err);
    err = nvs_set_u8(my_handle,"powerState",powerState? 1 : 0);
    ESP_ERROR_CHECK(nvs_commit(my_handle));
    nvs_close(my_handle);
    nvs_get_stats(NULL, &nvs_stats);
    printf("AFTER save Count: UsedEntries = (%d), FreeEntries = (%d), AllEntries = (%d)\n",
        nvs_stats.used_entries, nvs_stats.free_entries, nvs_stats.total_entries);
}
void save_displayMode() {
    esp_err_t err;
    nvs_stats_t nvs_stats;
    nvs_handle_t my_handle;
    nvs_get_stats(NULL, &nvs_stats);
    printf("BEFORE Save Count: UsedEntries = (%d), FreeEntries = (%d), AllEntries = (%d)\n",
        nvs_stats.used_entries, nvs_stats.free_entries, nvs_stats.total_entries);
    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    ESP_ERROR_CHECK(err);
    err = nvs_set_u8(my_handle,"displayMode",mode);
    ESP_ERROR_CHECK(nvs_commit(my_handle));
    nvs_close(my_handle);
    nvs_get_stats(NULL, &nvs_stats);
    printf("AFTER save Count: UsedEntries = (%d), FreeEntries = (%d), AllEntries = (%d)\n",
        nvs_stats.used_entries, nvs_stats.free_entries, nvs_stats.total_entries);
}
void save_preset(int slot) {
  nvs_stats_t nvs_stats;
  esp_err_t err;
  nvs_handle_t my_handle;
  char slotname[10];
  preset_t *p = malloc(sizeof(preset_t));
  memcpy(&p->rings,&rings,sizeof(rings));
  p->sparkle = sparkle;
  p->numflicker = numflicker;
  p->flicker_r = flicker_r;
  p->flicker_g = flicker_g;
  p->flicker_b = flicker_b;
  sprintf(slotname,"preset%d",slot);
  nvs_get_stats(NULL, &nvs_stats);
  printf("BEFORE Count: UsedEntries = (%d), FreeEntries = (%d), AllEntries = (%d)\n",
      nvs_stats.used_entries, nvs_stats.free_entries, nvs_stats.total_entries);
  err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
  ESP_ERROR_CHECK(err);
  err = nvs_set_blob(my_handle,slotname,p,sizeof(preset_t));
  ESP_ERROR_CHECK(nvs_commit(my_handle));
  nvs_close(my_handle);
  free(p);
  nvs_get_stats(NULL, &nvs_stats);
  printf("BEFORE Count: UsedEntries = (%d), FreeEntries = (%d), AllEntries = (%d)\n",
      nvs_stats.used_entries, nvs_stats.free_entries, nvs_stats.total_entries);
}
int load_preset(int slot) {
    size_t actualSize;
    esp_err_t err;
    nvs_handle_t my_handle;
    char slotname[10];
    preset_t *p = malloc(sizeof(preset_t));
    sprintf(slotname,"preset%d",slot);
    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    ESP_ERROR_CHECK(err);
    err = nvs_get_blob(my_handle,slotname,0L,&actualSize);
    if (err || actualSize != sizeof(preset_t)) {
      ESP_LOGE(TAG,"Error loading preset %d",slot);
      return -1;
    } else {
            err = nvs_get_blob(my_handle,slotname,p,&actualSize);
            memcpy(&rings,&p->rings,sizeof(rings));
            sparkle = p->sparkle;
            numflicker = p->numflicker;
            flicker_r = p->flicker_r;
            flicker_g = p->flicker_g;
            flicker_b = p->flicker_b;
            ESP_LOGI(TAG,"Preset %d loaded",slot);
    }
    free(p);
    nvs_close(my_handle);
    preset_running=slot;
    //time(&next_time);
    //next_time += 10; // Advance to next in 10 seconds
    return 0;
}


/*** WIFI AP  ****/

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}


#define EXAMPLE_ESP_WIFI_SSID "MakeIt Xmas Star"
// Remove this line or set your SSID!
int x[1/(sizeof(SSID_PASSWORD)-1)];

wifi_config_t wifi_config = {
    .ap = {
        .ssid = EXAMPLE_ESP_WIFI_SSID,
        .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
        .channel = 1,
        .password = SSID_PASSWORD,
        .max_connection = 4,
        .authmode = WIFI_AUTH_WPA_WPA2_PSK
    },
};


void SetupAP() {
esp_netif_create_default_wifi_ap();
ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                    ESP_EVENT_ANY_ID,
                                                    &wifi_event_handler,
                                                    NULL,
                                                    NULL));
wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
ESP_ERROR_CHECK(esp_wifi_init(&cfg));

if (strlen(SSID_PASSWORD) == 0) {
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;
}
ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

ESP_ERROR_CHECK(esp_wifi_start());
}

/*** WIFI AP  ****/
extern	void
app_main (void)
{
    static httpd_handle_t server = NULL;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    //ESP_ERROR_CHECK(example_connect());
    //
    SetupAP();
#ifdef CONFIG_EXAMPLE_CONNECT_WIFI
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server));
#endif // CONFIG_EXAMPLE_CONNECT_WIFI
#ifdef CONFIG_EXAMPLE_CONNECT_ETHERNET
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ETHERNET_EVENT_DISCONNECTED, &disconnect_handler, &server));
#endif // CONFIG_EXAMPLE_CONNECT_ETHERNET

		gpio_set_direction(GPIO_LED,GPIO_MODE_OUTPUT);
		gpio_set_level(GPIO_LED,1);

		gpio_set_direction(GPIO_STARTGAME,GPIO_MODE_INPUT);
		gpio_set_pull_mode(GPIO_STARTGAME,GPIO_PULLUP_ONLY);
		gpio_set_direction(GPIO_ENDGAME,GPIO_MODE_INPUT);
		gpio_set_pull_mode(GPIO_ENDGAME,GPIO_PULLUP_ONLY);
  		// Set Button handler 
    gpio_config_t io_conf;
		io_conf.intr_type = GPIO_PIN_INTR_NEGEDGE;
    io_conf.pin_bit_mask = 1<< GPIO_INPUT_IO_0;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);
    ;
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);

    gpio_isr_handler_add(GPIO_INPUT_IO_0, gpio_isr_handler, (void*) GPIO_INPUT_IO_0);
	//test_neopixel();
    
    /* Start the server for the first time */
    server = start_webserver();



  xTaskCreatePinnedToCore(&test_neopixel, "Neopixels", 8192, NULL, 55, NULL,1);
  xTaskCreatePinnedToCore(tcp_server_task, "tcp_server", 4096, (void*)0L, 5, NULL,0);
  xTaskCreatePinnedToCore(&console, "Console", 8192, NULL, 1, NULL,0);
}


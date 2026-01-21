// SPDX-License-Identifier: MIT
// Super-duper-clock.

#include "display.h"
#include "cJSON.h"

//#include <bme280.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_sntp.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/queue.h>
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_event.h"
//#include <i2c_bus.h>
#include <nvs_flash.h>
#include "mqtt_client.h"
#include "memory.h"
#include "resources.h"

#define INDEX_CARHEATER     0
#define INDEX_DOOR          1
#define INDEX_FLOOD         2
#define INDEX_OILBURNER     3
#define INDEX_STOCKHEATER   4
#define INDEX_SOLHEATER     5

#define FLOODFLAG_TRASH     1
#define FLOODFLAG_LATTIA    2
#define FLOODFLAG_TISKIKONE 4

#define DOORFLAG_STORE     1
#define DOORFLAG_BOILER    2
#define DOORFLAG_BALKONG   4
#define DOORFLAG_FRONT     8


// Log tag
static const char* log_tag = "monitor";
// BME280 sensor hadle
//static bme280_handle_t bme280;
// Current info to display
//static struct info info;
static struct commState commInfo;
static QueueHandle_t evt_queue = NULL;



static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(log_tag, "Last error %s: 0x%x", message, error_code);
    }
}


static char *getJsonStr(cJSON *js, char *name)
{
    cJSON *item = cJSON_GetObjectItem(js, name);
    if (item != NULL)
    {
        if (cJSON_IsString(item))
        {
            return item->valuestring;
        }
        else ESP_LOGI(log_tag, "%s is not a string", name);
    }
    else ESP_LOGI(log_tag,"%s not found from json", name);
    return "\0";
}

static bool getJsonState(cJSON *js, char *name)
{
    cJSON *item = cJSON_GetObjectItem(js, name);
    if (item != NULL)
    {
        if (item->type == 2)
        //if (cJSON_IsTrue(item)) bug in library
        {
            return true;
        }
    }
    else ESP_LOGI(log_tag,"%s not found from json", name);
    return false;
}


static bool getJsonInt(cJSON *js, char *name, int *val)
{
    bool ret = false;

    cJSON *item = cJSON_GetObjectItem(js, name);
    if (item != NULL)
    {
        if (cJSON_IsNumber(item))
        {
            if (item->valueint != *val)
            {
                ret = true;
                *val = item->valueint;
            }
        }
        else ESP_LOGI(log_tag,"%s is not a number", name);
    }
    else ESP_LOGI(log_tag,"%s not found from json", name);
    return ret;
}

static bool getJsonFloat(cJSON *js, char *name, float *val)
{
    bool ret = false;

    cJSON *item = cJSON_GetObjectItem(js, name);
    if (item != NULL)
    {
        if (cJSON_IsNumber(item))
        {
            if (item->valuedouble != *val)
            {
                ret = true;
                *val = item->valuedouble;
            }
        }
        else ESP_LOGI(log_tag,"%s is not a number", name);
    }
    else ESP_LOGI(log_tag,"%s not found from json", name);
    return ret;
}

static void dispComm(struct commState *state)
{
    struct measurement meas;
    meas.id = COMM;
    meas.data.comm.wifi = state->wifi;
    meas.data.comm.ntp = state->ntp;
    meas.data.comm.mqtt = state->mqtt;
    xQueueSend(evt_queue, &meas, 0);
}


static void dispTime(struct tm *now_local)
{
    struct measurement meas;
    meas.id = TIME;
    meas.data.time.hours   = now_local->tm_hour;
    meas.data.time.minutes = now_local->tm_min;
    meas.data.time.seconds = now_local->tm_sec;
    xQueueSend(evt_queue, &meas, 0);
}


// Timer callback: called once per 5 second
static void on_clock_tick(void* arg)
{
    time_t now_utc;
    struct tm now_local;

    // current time
    time(&now_utc);
    localtime_r(&now_utc, &now_local);
    dispTime(&now_local);
}


// NTP callback: sync complete
static void on_time_sync(struct timeval* tv)
{
    ESP_LOGI(log_tag, "NTP sync completed");
    commInfo.ntp = true;
    dispComm(&commInfo);
}

// Initialize Network Time Protocol client
static void ntp_init(void)
{
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    sntp_set_time_sync_notification_cb(on_time_sync);
    esp_sntp_init();
}

// Event handler for network info notifications
static void on_net_event(void* arg, esp_event_base_t event_base,
                         int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START ||
            event_id == WIFI_EVENT_STA_DISCONNECTED) {
            ESP_LOGW(log_tag, "Reconnect WiFi");
            commInfo.wifi = false;
            commInfo.ntp = false;
            dispComm(&commInfo);
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(log_tag, "WiFi connected");
        commInfo.wifi = true;
        dispComm(&commInfo);
        ntp_init();
    }
}

// Initialize WiFi
static void wifi_init(void)
{
    esp_event_handler_instance_t event;
    const wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();

    wifi_config_t wifi_cfg = {
        .sta.threshold.authmode = WIFI_AUTH_WPA2_PSK,
    };
    strcpy((char*)wifi_cfg.sta.ssid, WIFI_SSID);
    strcpy((char*)wifi_cfg.sta.password, WIFI_PASSWORD);

    // initialize wifi
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    // set callbacks for network stat events
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &on_net_event, NULL, &event));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &on_net_event, NULL, &event));

    // setup wifi
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    // start wifi client
    ESP_LOGI(log_tag, "Start WiFi connection with %s", WIFI_SSID);
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void dispPrice(float price, int level)
{
    struct measurement meas;

    meas.id = PRICE;
    meas.data.price.euros = price;
    meas.data.price.level = level;
    xQueueSend(evt_queue, &meas, 0);
}

static void dispAvgPrice(float price)
{
    struct measurement meas;

    meas.id = AVGPRICE;
    meas.data.price.euros = price;
    meas.data.price.level = normal;
    xQueueSend(evt_queue, &meas, 0);
}

static void dispTemperature(float temperature)
{
    struct measurement meas;

    meas.id = TEMPERATURE;
    meas.data.heater.temperature = temperature;
    xQueueSend(evt_queue, &meas, 0);
}

static void dispLevel(int level)
{
    struct measurement meas;

    meas.id = LEVEL;
    meas.data.heater.level = level;
    xQueueSend(evt_queue, &meas, 0);
}

static void dispState(enum indicator state, enum meastype id)
{
    struct measurement meas;

    meas.id = id;
    meas.data.indic = state;
    xQueueSend(evt_queue, &meas, 0);
}

char const * const hometopic   = "home/kallio";
const char * const zigbeetopic = "zigbee2mqtt";


struct messageId {
    char const * const baseTopic;
    const char * const subTopic;
    const char *id;
    const int num;
};

struct messageId messageIds[] = {
    {hometopic,      NULL,               "thermostat",       0},
    {hometopic,      NULL,               "temperature",      1},
    {hometopic,      NULL,               "relay",            2},
    {hometopic,      NULL,               "elprice",          9},    
    {hometopic,      NULL,               "daystats",         11},
    {zigbeetopic,   "store_door",        NULL,               3},
    {zigbeetopic,   "boiler_door",       NULL,               4},
    {zigbeetopic,   "balkong_door",      NULL,               5},
    {zigbeetopic,   "front_door",        NULL,               10},
    {zigbeetopic,   "kitchen_trash",     NULL,               6},
    {zigbeetopic,   "lattia",            NULL,               7},
    {zigbeetopic,   "tiskikone",         NULL,               8},
    {NULL,NULL,NULL,-1}
};


static int resolveWhichMessage(esp_mqtt_event_handle_t event, cJSON *cjson)
{
    if (event->topic == NULL) return -1;

    int len;
    char id[20];
    char zigbee[30];
    
    for (int i=0; messageIds[i].baseTopic != NULL; i++)
    {
        if (messageIds[i].subTopic == NULL)
        {
            if (!memcmp(event->topic,messageIds[i].baseTopic,strlen(messageIds[i].baseTopic)))
            {
                strcpy(id,getJsonStr(cjson,"id"));
                if (!strcmp(id,messageIds[i].id))
                {
                    return messageIds[i].num;
                }
            }
        }
        else
        {
            len = sprintf(zigbee,"%s/%s",messageIds[i].baseTopic, messageIds[i].subTopic);
            if (!memcmp(event->topic, zigbee, len))
            {
                ESP_LOGI(log_tag, "%s changed", zigbee);
                return messageIds[i].num;
            }
        }
    }
    return -1;
}

static int todayNum(void)
{
    time_t now_utc;
    struct tm now_local;

    time(&now_utc);
    localtime_r(&now_utc, &now_local);
    return now_local.tm_wday;
}

static uint16_t handleJson(esp_mqtt_event_handle_t event, uint8_t *chipid)
{
    cJSON *root = cJSON_Parse(event->data);
    time_t now;
    bool flagsChanged=false;
    static float avgDayPrice = -10;
    static int floodFlag = 0x0;
    static int doorFlag  = 0x0;

    time(&now);
    if (root != NULL)
    {
        switch (resolveWhichMessage(event, root))
        {
            case 0:
                {
                    int val=-1;
                    if (getJsonInt(root,"value",&val))
                    {
                        dispLevel(val);
                    }
                }
                break;

            case 1:
                if (!strcmp(getJsonStr(root,"sensor"),"ntc"))
                {
                    float val=0;
                    if (getJsonFloat(root,"value",&val))
                    {
                        ESP_LOGI(log_tag,"got some temperature %.2f", val);
                        dispTemperature(val);
                    }
                }
                break;

            case 2:
                {
                    char device[20];
                    float power=-1.0;
                    bool state = getJsonState(root,"state");
                    int contact = -1;

                    getJsonInt(root,"contact", &contact);
                    strcpy(device,getJsonStr(root,"device"));
                    if (!strcmp(device, "shellyplus1pm"))
                    {
                        if (!state)
                        {
                            dispState(INDICATOR_OFF, CARHEATER);
                        }
                        else
                        {
                            if (getJsonFloat(root,"power",&power))
                            {
                                ESP_LOGI(log_tag,"got power %.2f", power);
                                if (power > 10.0)
                                {
                                    dispState(INDICATOR_CONNECTED, CARHEATER);
                                }
                                else
                                {
                                    dispState(INDICATOR_ON, CARHEATER);
                                }
                            }
                        }
                    }
                    if (!strcmp(device,"shelly1"))
                    {
                        switch (contact)
                        {
                            case 0: // solarheater
                                if (!state) dispState(INDICATOR_OFF, SOLHEAT);
                                else dispState(INDICATOR_CONNECTED, SOLHEAT);
                            break;

                            case 1:
                            break;

                            case 2: // stockheater
                                if (!state) dispState(INDICATOR_OFF, STOCKHEAT);
                                else dispState(INDICATOR_CONNECTED, STOCKHEAT);

                            break;

                            case 3:
                                if (!state)  dispState(INDICATOR_OFF, OILBURNER);
                                else dispState(INDICATOR_CONNECTED, OILBURNER);
                            break;
                        }
                    }
                }
                break;


            case 3:
                flagsChanged = true;
                if (!getJsonState(root,"contact"))
                    doorFlag |= DOORFLAG_STORE;
                else
                    doorFlag &= ~DOORFLAG_STORE;
                break;

            case 4:
                flagsChanged = true;
                if (!getJsonState(root,"contact"))
                    doorFlag |= DOORFLAG_BOILER;
                else
                    doorFlag &= ~DOORFLAG_BOILER;
                break;

            case 5:
                flagsChanged = true;
                if (!getJsonState(root,"contact"))
                    doorFlag |= DOORFLAG_BALKONG;
                else
                    doorFlag &= ~DOORFLAG_BALKONG;
                break;

            case 6:
                flagsChanged = true;
                if (getJsonState(root,"water_leak"))
                    floodFlag |= FLOODFLAG_TRASH;
                else
                    floodFlag &= ~FLOODFLAG_TRASH;
                break;

            case 7:
                flagsChanged = true;
                if (getJsonState(root,"water_leak"))
                    floodFlag |= FLOODFLAG_LATTIA;
                else
                    floodFlag &= ~FLOODFLAG_LATTIA;

                break;

            case 8:
                flagsChanged = true;
                if (getJsonState(root,"water_leak"))
                    floodFlag |= FLOODFLAG_TISKIKONE;
                else
                    floodFlag &= ~FLOODFLAG_TISKIKONE;

                break;

            case 9:
                {
                    float val = 0.0;
                    enum pricelevel level;
                    char stateStr[10];

                    strcpy(stateStr,getJsonStr(root,"pricestate"));
                    if (!strcmp(stateStr,"low")) level = low;
                    else if (!strcmp(stateStr,"high")) level = high;
                    else level = normal;
                    if (getJsonFloat(root,"price",&val))
                    {
                        dispPrice(val,level);
                    }
                }
                break;

            case 10:
                flagsChanged = true;
                if (!getJsonState(root,"contact"))
                    doorFlag |= DOORFLAG_FRONT;
                else
                    doorFlag &= ~DOORFLAG_FRONT;
                break;

            case 11:
                {
                    int today = -1;
                    if (getJsonInt(root, "weekday",&today) && today == todayNum())
                    {
                        if (getJsonFloat(root,"avg",&avgDayPrice))
                        {
                            ESP_LOGI(log_tag, "--> Electricity daystats for day %d, avg %.2f", today, avgDayPrice);
                            dispAvgPrice(avgDayPrice);
                        }
                    }
                }
                break;

            default:
                break;
        }
        if (flagsChanged)
        {
            if (floodFlag) dispState(INDICATOR_ON, FLOOD);
            else dispState(INDICATOR_OFF, FLOOD);

            if (doorFlag) dispState(INDICATOR_ON, DOOR);
            else dispState(INDICATOR_OFF, DOOR);
        }
        cJSON_Delete(root);
    }
    return 0;
}



int subscribeTopic(esp_mqtt_client_handle_t client, const char *prefix, char *topic)
{
    char name[80];

    sprintf(name,"%s/%s", prefix, topic);
    return esp_mqtt_client_subscribe(client, name , 0);
}

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */


static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(log_tag, "Event dispatched from event loop base=%s, event_id=%ld", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
            ESP_LOGI(log_tag, "MQTT_EVENT_CONNECTED");
            subscribeTopic(client, hometopic, "thermostat/+/parameters/#");
            subscribeTopic(client, hometopic, "relay/0/shellyplus1pm/state");
            subscribeTopic(client, hometopic, "relay/+/shelly1/state");
            subscribeTopic(client, hometopic, "elprice/currentquart");
            subscribeTopic(client, hometopic, "elprice/daystats/#");
            subscribeTopic(client, zigbeetopic, "#");
            commInfo.mqtt = true;
            dispComm(&commInfo);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(log_tag, "MQTT_EVENT_DISCONNECTED");
        commInfo.mqtt = false; // TODO: mqtt does not yet have any indicator.
        dispComm(&commInfo);
        break;

    case MQTT_EVENT_SUBSCRIBED:
        break;

    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(log_tag, "MQTT_EVENT_UNSUBSCRIBED");
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(log_tag, "MQTT_EVENT_PUBLISHED");
        break;

    case MQTT_EVENT_DATA:
        {
            uint16_t flags = handleJson(event, handler_args);
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGI(log_tag, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(log_tag, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));

        }
        break;
    default:
        ESP_LOGI(log_tag, "Other event id");
        break;
    }
}



static esp_mqtt_client_handle_t mqtt_app_start(uint8_t *chipid)
{
    char client_id[128];
    char uri[64];
    
    sprintf(client_id,"client_id=%x%x%x",chipid[3],chipid[4],chipid[5]);
    sprintf(uri,"mqtt://%s:%s","192.168.101.231", "1883");

    ESP_LOGI(log_tag,"built client id=[%s]",client_id);
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = uri,
        .credentials.client_id = client_id
        //.session.last_will.topic = device_topic(comminfo->mqtt_prefix, deviceTopic, chipid),
        //.session.last_will.msg = device_data(jsondata, chipid, appname, 0),
        //.session.last_will.msg_len = strlen(jsondata),
        //.session.last_will.qos = 0,
        //.session.last_will.retain = 1
    };
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);

    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, chipid);
    esp_mqtt_client_start(client);
    return client;
}

// Entry point
void app_main(void)
{
    uint8_t chipid[8];

    const esp_timer_create_args_t ptimer_args = {
        .callback = &on_clock_tick,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "time",
        .skip_unhandled_events = true,
    };
    esp_timer_handle_t ptimer_handle;
    esp_efuse_mac_get_default(chipid);

    ESP_LOGI(log_tag, "Initialization started");

    // set timezone
    setenv("TZ", "GMT-2", 1);
    tzset();

    //bme280_init();
    display_init();
    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_init();

    evt_queue = xQueueCreate(15, sizeof(struct measurement));

    display_static_elements();
    display_indicatoramount(6);
    dispLevel(0);
    dispTemperature(0);
    dispPrice(0,normal);
    dispState(INDICATOR_ON, CARHEATER);
    dispState(INDICATOR_ON, OILBURNER);
    dispState(INDICATOR_ON, DOOR);
    dispState(INDICATOR_ON, STOCKHEAT);
    dispState(INDICATOR_ON, SOLHEAT);
    dispState(INDICATOR_ON, FLOOD);

    on_clock_tick(chipid); // chipid is not used.

    esp_mqtt_client_handle_t client = mqtt_app_start(chipid);
    // register periodic timer
    ESP_ERROR_CHECK(esp_timer_create(&ptimer_args, &ptimer_handle));
    ESP_ERROR_CHECK(esp_timer_start_periodic(ptimer_handle, 5000000)); // 5 sec

    ESP_LOGI(log_tag, "Initialization completed");

    while (1)
    {
        struct measurement meas;

        if (xQueueReceive(evt_queue, &meas, 10000 / portTICK_PERIOD_MS))
        {
            switch (meas.id) {
                case COMM:
                    display_comm(&meas.data.comm);
                break;

                case TEMPERATURE:
                    display_temperature(meas.data.heater.temperature);
                break;

                case LEVEL:
                    display_level(meas.data.heater.level * 20);
                break;

                case CARHEATER:
                    display_icon(meas.data.indic, image_car, INDEX_CARHEATER);
                break;

                case OILBURNER:
                    display_icon(meas.data.indic ? INDICATOR_ON : INDICATOR_OFF, image_burner, INDEX_OILBURNER);
                break;

                case STOCKHEAT:
                    display_icon(meas.data.indic, image_heater, INDEX_STOCKHEATER);
                break;

                case SOLHEAT:
                    display_icon(meas.data.indic, image_solar, INDEX_SOLHEATER);
                break;

                case DOOR:
                    ESP_LOGI(log_tag, "Received door indicator %d", meas.data.indic);
                    display_icon(meas.data.indic ? INDICATOR_ON : INDICATOR_OFF, image_door, INDEX_DOOR);
                break;

                case FLOOD:
                    ESP_LOGI(log_tag, "Received flooding indicator %d", meas.data.indic);
                    display_icon(meas.data.indic ? INDICATOR_ON : INDICATOR_OFF, image_flood, INDEX_FLOOD);
                break;

                case TIME:
                    display_time(&meas.data.time);
                break;

                case PRICE:
                    display_price(&meas.data.price, 10, 230 );
                break;

                case AVGPRICE:
                    display_price(&meas.data.price, 160, 230 );

            }    
        }
        else
        { 
            ESP_LOGI(log_tag,"timeout");
        }
    }
}

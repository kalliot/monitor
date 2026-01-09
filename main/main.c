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
#include <i2c_bus.h>
#include <nvs_flash.h>
#include "mqtt_client.h"

// Log tag
static const char* log_tag = "monitor";
// BME280 sensor hadle
//static bme280_handle_t bme280;
// Current info to display
static struct info info;
QueueHandle_t evt_queue = NULL;

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
        ESP_LOGI(log_tag,"%s found from json, type=%d", name, item->type);
        if (item->type == 2)
        //if (cJSON_IsTrue(item))
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
            else ESP_LOGI(log_tag,"%s is not changed", name);
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
                ESP_LOGI(log_tag,"received variable %s:%2.2f", name, item->valuedouble);
            }
            ESP_LOGI(log_tag,"%s is not changed", name);
        }
        else ESP_LOGI(log_tag,"%s is not a number", name);
    }
    else ESP_LOGI(log_tag,"%s not found from json", name);
    return ret;
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


// Timer callback: called once per second
static void on_clock_tick(void* arg)
{
    float sensor;
    time_t now_utc;
    struct tm now_local;

    // current time
    time(&now_utc);
    localtime_r(&now_utc, &now_local);
    dispTime(&now_local);

    info.hours = now_local.tm_hour;
    info.minutes = now_local.tm_min;
    info.seconds = now_local.tm_sec;


    // BME280 sensor data
    //info.temperature =
    //bme280_read_temperature(bme280, &sensor) == ESP_OK ? sensor : 0;
    //info.humidity =
    //bme280_read_humidity(bme280, &sensor) == ESP_OK ? sensor : 0;
    //info.pressure =
    //bme280_read_pressure(bme280, &sensor) == ESP_OK ? sensor : 0;

    ESP_LOGD(log_tag, "%02d:%02d:%02d t=%.1f h=%.1f p=%.1f", info.hours,
             info.minutes, info.seconds, info.temperature, info.humidity,
             info.pressure);

    display_redraw(&info);
}

// Initialize BME280 sensor
/*
static void bme280_init(void)
{
    const i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = GPIO_NUM_18,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = GPIO_NUM_19,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    const i2c_bus_handle_t bus = i2c_bus_create(I2C_NUM_0, &conf);

    bme280 = bme280_create(bus, BME280_I2C_ADDRESS_DEFAULT);
    bme280_default_init(bme280);
}
*/

// NTP callback: sync complete
static void on_time_sync(struct timeval* tv)
{
    ESP_LOGI(log_tag, "NTP sync completed");
    info.ntp = true;
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
            info.wifi = false;
            info.ntp = false;
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(log_tag, "WiFi connected");
        info.wifi = true;
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

static void dispState(enum indicator state)
{
    struct measurement meas;
    meas.id = CARHEATER;
    meas.data.indic = state;
    xQueueSend(evt_queue, &meas, 0);
}


static uint16_t handleJson(esp_mqtt_event_handle_t event, uint8_t *chipid)
{
    cJSON *root = cJSON_Parse(event->data);
    char id[20];
    time_t now;

    time(&now);
    if (root != NULL)
    {
        strcpy(id,getJsonStr(root,"id"));

        if (!strcmp(id,"temperature"))
        {
            if (!strcmp(getJsonStr(root,"sensor"),"ntc"))
            {
                float val=0;
                if (getJsonFloat(root,"value",&val))
                {
                    ESP_LOGI(log_tag,"got some temperature %.2f", val);
                    dispTemperature(val);
                    info.temperature = val;
                    int tmp = (int) val;

                    info.humidity = 100 * (val - tmp);
                    ESP_LOGI(log_tag,"info.temperature %.2f", info.temperature);
                    ESP_LOGI(log_tag,"info.humidity %.2f", info.humidity);
                }
            }    
        }
        if (!strcmp(id,"thermostat"))
        {
            int val=0;
            if (getJsonInt(root,"value",&val))
            {
                dispLevel(val);
                ESP_LOGI(log_tag,"got level %d", val);
                info.pressure = (float) val;
                ESP_LOGI(log_tag,"info.pressure %.1f", info.pressure);
            }    
        }

        if (!strcmp(id,"relay"))
        {
            float power=-1.0;
            bool state = getJsonState(root,"state");

            if (!state)
            {
                dispState(INDICATOR_OFF);
                //display_indicator(INDICATOR_OFF);
            }
            else
            {
                if (getJsonFloat(root,"power",&power))
                {
                    ESP_LOGI(log_tag,"got power %.2f", power);
                    if (power > 10.0)
                    {
                        //display_indicator(INDICATOR_CONNECTED);
                        dispState(INDICATOR_CONNECTED);
                    }
                    else
                    {
                        //display_indicator(INDICATOR_ON);
                        dispState(INDICATOR_ON);
                    }
                }
            }
            
        }
        cJSON_Delete(root);
    }
    return 0;
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

    int msg_id;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
            ESP_LOGI(log_tag, "MQTT_EVENT_CONNECTED");
            ESP_LOGI(log_tag,"subscribing topics");

            msg_id = esp_mqtt_client_subscribe(client, "home/kallio/thermostat/+/parameters/#" , 0);
            ESP_LOGI(log_tag, "sent subscribe thermostat succesful, msg_id=%d", msg_id);

            msg_id = esp_mqtt_client_subscribe(client, "home/kallio/relay/0/shellyplus1pm/state" , 0);
            ESP_LOGI(log_tag, "sent subscribe relay succesful, msg_id=%d", msg_id);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(log_tag, "MQTT_EVENT_DISCONNECTED");
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
            uint16_t flags = handleJson(event, (uint8_t *) handler_args);
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
    evt_queue = xQueueCreate(10, sizeof(struct measurement));
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
                break;

                case TEMPERATURE:
                    ESP_LOGI(log_tag, "Received temperature from queue %.2f", meas.data.heater.temperature);
                    display_temperature(meas.data.heater.temperature);
                break;

                case LEVEL:
                    ESP_LOGI(log_tag, "Received level from queue %d", meas.data.heater.level);
                    display_level(meas.data.heater.level);
                break;

                case CARHEATER:
                    ESP_LOGI(log_tag, "Received indicator from queue %d", meas.data.indic);
                    display_indicator(meas.data.indic);
                break;
    
                case TIME:
                    ESP_LOGI(log_tag, "Received time from queue %d %d %d", meas.data.time.hours, meas.data.time.minutes, meas.data.time.seconds);
                    display_time(&meas.data.time);
                break;
            }    
        }
        else
        { 
            ESP_LOGI(log_tag,"timeout");
        }
    }
}

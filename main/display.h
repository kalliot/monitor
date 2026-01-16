// SPDX-License-Identifier: MIT
// Display interface.

#pragma once

#include <stdbool.h>
#include <stdint.h>

// Display size
#define DISPLAY_WIDTH  480
#define DISPLAY_HEIGHT 320


enum indicator {
    INDICATOR_OFF,
    INDICATOR_ON,
    INDICATOR_CONNECTED
};

enum pricelevel {
    low,
    normal,
    high
};

enum meastype
{
    COMM,
    TEMPERATURE,
    LEVEL,
    CARHEATER,
    OILBURNER,
    STOCKHEAT,
    SOLHEAT,
    DOOR,
    FLOOD,
    TIME,
    PRICE
};

struct commState {
    bool wifi;
    bool ntp;
    bool mqtt;
};

struct Price {
    enum pricelevel level; 
    float euros; // euros with tax
};

struct ntpTime {
    uint8_t hours;
    uint8_t minutes;
    uint8_t seconds;
};

struct Heater {
    float temperature;
    int level;
};


struct measurement {
    enum meastype id;
    union {
        struct commState comm;
        struct Heater heater;
        struct ntpTime time;
        struct Price price;
        enum indicator indic;
    } data;
};

/**
 * Info to display.
 */
struct info {
    bool wifi;
    bool ntp;

    uint8_t hours;
    uint8_t minutes;
    uint8_t seconds;

    float temperature;
    float humidity;
    float pressure;
};

/**
 * Initialize LCD display.
 */
void display_init(void);

/**
 * Redraw display.
 * @param info values to display
 */
void display_redraw(const struct info* info);
void display_indicator(enum indicator state, int index);
void display_temperature(float temperature);
void display_price(struct Price *price);
void display_level(unsigned long level);
void display_time(struct ntpTime *time);
void display_comm(struct commState *state);
void display_static_elements(void);
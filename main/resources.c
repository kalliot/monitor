// SPDX-License-Identifier: MIT
// Images and fonts resources.

#include "resources.h"

//#include "img/celsius.xbm"
#include "img/font100.xbm"
#include "img/font28.xbm"
#include "img/font60.xbm"
#include "img/door32.xbm"
#include "img/ntp.xbm"
#include "img/heater32.xbm"
#include "img/burner32.xbm"
#include "img/solar32.xbm"
#include "img/car32.xbm"
#include "img/flood32.xbm"
#include "img/wifi.xbm"
#include "img/mqtt.xbm"


#define XBM_STRIDE(w) (((w + 7) / 8) * 8)

#define FONT_SYMBOLS    10
#define FONT_SPACING(w) (w / FONT_SYMBOLS / 20)

static const struct font fonts[] = {
    [font28] = {
        .width = font28_width / FONT_SYMBOLS,
        .height = font28_height,
        .spacing = FONT_SPACING(font28_width),
        .stride = XBM_STRIDE(font28_width),
        .mask = font28_bits,
    },
    [font60] = {
        .width = font60_width / FONT_SYMBOLS,
        .height = font60_height,
        .spacing = FONT_SPACING(font60_height),
        .stride = XBM_STRIDE(font60_width),
        .mask = font60_bits,
    },
    [font100] = {
        .width = font100_width / FONT_SYMBOLS,
        .height = font100_height,
        .spacing = FONT_SPACING(font100_height),
        .stride = XBM_STRIDE(font100_width),
        .mask = font100_bits,
    },
};

static const struct image images[] = {
    [image_wifi] = {
        .width = wifi_width,
        .height = wifi_height,
        .stride = XBM_STRIDE(wifi_width),
        .mask = wifi_bits,
    },
    [image_ntp] = {
        .width = ntp_width,
        .height = ntp_height,
        .stride = XBM_STRIDE(wifi_width),
        .mask = ntp_bits,
    },
    [image_mqtt] = {
        .width = mqtt_width,
        .height = mqtt_height,
        .stride = XBM_STRIDE(mqtt_width),
        .mask = mqtt_bits,
    },
    [image_car] = {
        .width = car_width,
        .height = car_height,
        .stride = XBM_STRIDE(car_width),
        .mask = car_bits,
    },
    [image_burner] = {
        .width = burner_width,
        .height = burner_height,
        .stride = XBM_STRIDE(burner_width),
        .mask = burner_bits,
    },
    [image_heater] = {
        .width = heater_width,
        .height = heater_height,
        .stride = XBM_STRIDE(heater_width),
        .mask = heater_bits,
    },
    [image_solar] = {
        .width = solar_width,
        .height = solar_height,
        .stride = XBM_STRIDE(solar_width),
        .mask = solar_bits,
    },
    [image_door] = {
        .width = door_width,
        .height = door_height,
        .stride = XBM_STRIDE(door_width),
        .mask = door_bits,
    },
    [image_flood] = {
        .width = flood_width,
        .height = flood_height,
        .stride = XBM_STRIDE(flood_width),
        .mask = flood_bits,
    },

};

const struct image* get_image(enum image_type type)
{
    return &images[type];
}

const struct font* get_font(enum font_size size)
{
    return &fonts[size];
}

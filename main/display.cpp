// SPDX-License-Identifier: MIT
// Display interface.

extern "C" {
#include "display.h"

#include "resources.h"
}

#define LGFX_WT32_SC01
#define HEIGHT_INDICATOR 33

#include <LGFX_AUTODETECT.hpp>

// LCD handle
static LGFX lcd;
static int ind_spacing = 10;

/**
 * Draw masked image.
 * @param img pointer to the image instance to use
 * @param x,y coordinates of the left top corner
 * @param color output color
 */
static void draw_image(const struct image* img, size_t x, size_t y,
                       uint32_t color)
{
    for (size_t dy = 0; dy < img->height; ++dy) {
        const size_t disp_y = dy + y;
        for (size_t dx = 0; dx < img->width; ++dx) {
            const size_t disp_x = dx + x;
            const uint32_t pixel = color * image_bit(img, dx, dy);
            lcd.writePixel(disp_x, disp_y, pixel);
        }
    }
}

/**
 * Draw masked image from the font.
 * @param font pointer to the font instance to use
 * @param index index of the font symbol
 * @param x,y coordinates of the left top corner
 * @param color output color
 */
static void draw_font(const struct font* font, size_t index, size_t x, size_t y,
                      uint32_t color)
{
    for (size_t dy = 0; dy < font->height; ++dy) {
        const size_t disp_y = dy + y;
        for (size_t dx = 0; dx < font->width; ++dx) {
            const size_t disp_x = dx + x;
            const uint32_t pixel = color * font_bit(font, index, dx, dy);
            lcd.writePixel(disp_x, disp_y, pixel);
        }
    }
}

/**
 * Draw number.
 * @param font pointer to the font instance to use
 * @param x,y coordinates of the left top corner
 * @param color output color
 * @param value number to draw
 * @param min_digits minimal number of digits to draw
 */
static void draw_number(const struct font* font, size_t x, size_t y,
                        uint32_t color, size_t value, size_t min_digits)
{
    size_t bcd = 0;
    size_t digits = 0;
    while (value > 0) {
        bcd |= (value % 10) << (digits * 4);
        value /= 10;
        ++digits;
    }
    if (digits < min_digits) {
        digits = min_digits;
    }

    for (size_t i = 0; i < digits; ++i) {
        const size_t start_bit = (digits - i - 1) * 4;
        const uint8_t digit = (bcd >> start_bit) & 0xf;
        const size_t x_offset = x + (font->width + font->spacing) * i;
        draw_font(font, digit, x_offset, y, color);
    }
}

/**
 * Fill rectangle with specified color.
 * @param x,y coordinates of the left top corner
 * @param width,height size of the rectangle
 * @param color output color
 */
static void fill(size_t x, size_t y, size_t width, size_t height,
                 uint32_t color)
{
    const size_t max_x = x + width;
    const size_t max_y = y + height;
    for (; y < max_y; ++y) {
        for (size_t dx = x; dx < max_x; ++dx) {
            lcd.writePixel(dx, y, color);
        }
    }
}

extern "C" void display_init(void)
{
    lcd.init();
    lcd.setRotation(1);
    lcd.setColorDepth(lgfx::rgb888_3Byte);
    lcd.setBrightness(20);
}


extern "C" void display_static_elements(void)
{
    const uint32_t main_color = lcd.color888(100, 219, 255);
    const uint32_t clr = lcd.color888(0xa0, 0x00, 0x00);

    lcd.startWrite();
    //draw_image(get_image(image_celsius), 50, 250, clr);
    //draw_image(get_image(image_percent), 155, 250, clr);
    //draw_image(get_image(image_mm), 240, 250, clr);

    fill(DISPLAY_WIDTH / 2 - 10, 50, 20, 20, main_color);
    fill(DISPLAY_WIDTH / 2 - 10, 100, 20, 20, main_color);
    fill(70, 210, 5, 5, main_color);   // dot between temperature full and remain
    //fill(70, 260, 5, 5, main_color);  // price full and remain.
    lcd.endWrite();
}
// x  = 10, y = 230

extern "C" void display_price(struct Price *price, int x, int y)
{
    uint32_t color;
    unsigned long whole = (unsigned long) price->euros;
    unsigned long fract = 100 * (price->euros - whole);

    switch (price->level)
    {
        case low:
            color = lcd.color888(40, 255, 40);
            break;

        case normal:
            color = lcd.color888(100, 219, 255);
            break;

        case high:
            color = lcd.color888(255, 50, 50);
            break;

        default:
            color = lcd.color888(100, 219, 255);
            break;
    }

    lcd.startWrite();
    draw_number(get_font(font28), x, y, color, whole, 2);
    draw_number(get_font(font28), x + 70, y, color, fract, 2);
    fill(x+60, y+30, 5, 5, color);
    lcd.endWrite();
}

extern "C" void display_temperature(float temperature)
{
    const uint32_t main_color = lcd.color888(100, 219, 255);
    unsigned long whole = (unsigned long) temperature;
    unsigned long fract = 100 * (temperature - whole);

    lcd.startWrite();
    draw_number(get_font(font28), 10, 170, main_color, whole, 2);
    draw_number(get_font(font28), 80, 170, main_color, fract, 2);
    lcd.endWrite();
}   

extern "C" void display_level(unsigned long level)
{
    const uint32_t main_color = lcd.color888(100, 219, 255);

    lcd.startWrite();
    draw_number(get_font(font28), 160, 170, main_color, level, 3);
    lcd.endWrite();
}   

extern "C" void display_time(struct ntpTime *time)
{
    const uint32_t main_color = lcd.color888(100, 219, 255);

    lcd.startWrite();
    draw_number(get_font(font100), 10, 20, main_color, time->hours, 2);
    draw_number(get_font(font100), 270, 20, main_color, time->minutes, 2);
    //draw_number(get_font(font60), 350, 190, main_color, time->seconds, 2);
    lcd.endWrite();
}

extern "C" void display_comm(struct commState *state)
{
    const struct image* iWifi = get_image(image_wifi);
    const struct image* iMqtt = get_image(image_mqtt);
    const struct image* iNtp = get_image(image_ntp);
    const uint32_t on_color = lcd.color888(50, 255, 50);
    const uint32_t off_color = lcd.color888(255, 50, 50);
    uint32_t wificolor, ntpcolor, mqttcolor;

    if (state->wifi)
        wificolor = on_color;
    else
        wificolor = off_color;

    if (state->ntp)
        ntpcolor = on_color;
    else
        ntpcolor = off_color;

    if (state->mqtt)
        mqttcolor = on_color;
    else
        mqttcolor = off_color;

    lcd.startWrite();
    draw_image(iWifi, DISPLAY_WIDTH / 2 - 30, 0, wificolor);
    draw_image(iNtp, DISPLAY_WIDTH / 2 - 5, 0, ntpcolor);
    draw_image(iMqtt, DISPLAY_WIDTH / 2 + 20, 0, mqttcolor);
    lcd.endWrite();
}


extern "C" void display_indicatoramount(int amount)
{
    ind_spacing = DISPLAY_WIDTH / amount;
}

extern "C" void display_icon(enum indicator state, enum image_type itype, int index)
{
    uint32_t color = lcd.color888(0, 0, 0);

    switch (state)
    {
        case INDICATOR_OFF:
            color = lcd.color888(0, 0, 0);
            break;

        case INDICATOR_ON:
            color = lcd.color888(255, 255, 50);
            break;

        case INDICATOR_CONNECTED:
            color = lcd.color888(255, 50, 50);
            break;
    }
    const struct image* iImage = get_image(itype);
    lcd.startWrite();
    draw_image(iImage, index * ind_spacing + 3, DISPLAY_HEIGHT - HEIGHT_INDICATOR, color);
    lcd.endWrite();
}

extern "C" void display_indicator(enum indicator state, int index)
{
    const uint32_t connected_color  = lcd.color888(255, 50, 50);
    const uint32_t off_color = lcd.color888(0, 0, 0);
    const uint32_t on_color = lcd.color888(0xff, 0xff, 0x0b);
    uint32_t color = off_color;
    
    switch (state)
    {
        case INDICATOR_OFF:
            color = off_color;
            break;

        case INDICATOR_ON:
            color = on_color;
            break;

        case INDICATOR_CONNECTED:
            color = connected_color;
            break;
    }
    lcd.startWrite();
    fill(index * ind_spacing, DISPLAY_HEIGHT - HEIGHT_INDICATOR, ind_spacing, HEIGHT_INDICATOR, color);
    lcd.endWrite();
}
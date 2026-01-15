// SPDX-License-Identifier: MIT
// Display interface.

extern "C" {
#include "display.h"

#include "resources.h"
}

#define LGFX_WT32_SC01
#define SIZE_INDICATOR 20

#include <LGFX_AUTODETECT.hpp>

// LCD handle
static LGFX lcd;

/**
 * Draw masked image.
 * @param font pointer to the image instance to use
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
    const uint32_t main_color = lcd.color888(0xff, 0, 0);
    const uint32_t clr = lcd.color888(0xa0, 0x00, 0x00);

    lcd.startWrite();
    draw_image(get_image(image_celsius), 50, 250, clr);
    //draw_image(get_image(image_percent), 155, 250, clr);
    draw_image(get_image(image_mm), 240, 250, clr);

    fill(DISPLAY_WIDTH / 2 - 10, 50, 20, 20, main_color);
    fill(DISPLAY_WIDTH / 2 - 10, 100, 20, 20, main_color);
    fill(100, 230, 5, 5, main_color);  // dot between temperature and humidity
    lcd.endWrite();
}

extern "C" void display_temperature(float temperature)
{
    const uint32_t main_color = lcd.color888(0xff, 0, 0);
    unsigned long whole = (unsigned long) temperature;
    unsigned long fract = 100 * (temperature - whole);

    lcd.startWrite();
    draw_number(get_font(font28), 40, 200, main_color, whole, 2);
    draw_number(get_font(font28), 110, 200, main_color, fract, 2);
    lcd.endWrite();
}   

extern "C" void display_level(unsigned long level)
{
    const uint32_t main_color = lcd.color888(0xff, 0, 0);

    lcd.startWrite();
    draw_number(get_font(font28), 180, 200, main_color, level, 3);
    lcd.endWrite();
}   

extern "C" void display_time(struct ntpTime *time)
{
    const uint32_t main_color = lcd.color888(0xff, 0, 0);

    lcd.startWrite();
    draw_number(get_font(font100), 10, 10, main_color, time->hours, 2);
    draw_number(get_font(font100), 270, 10, main_color, time->minutes, 2);
    draw_number(get_font(font60), 350, 190, main_color, time->seconds, 2);
    lcd.endWrite();
}

extern "C" void display_comm(struct commState *state)
{
    const struct image* img = NULL;

    lcd.startWrite();
    if (!state->wifi) {
        img = get_image(image_wifi);
    } else if (!state->ntp) {
        img = get_image(image_ntp);
    }
    if (img) {
        draw_image(img, DISPLAY_WIDTH / 2 - 10, 0,
                    lcd.color888(0xff, 0xff, 0));
    } else {
        fill(DISPLAY_WIDTH / 2 - 10, 0, 20, 20, lcd.color888(0, 0, 0));
    }
    lcd.endWrite();
}

extern "C" void display_indicator(enum indicator state, int index)
{
    const uint32_t connected_color  = lcd.color888(0xff, 0x0b, 0x0b);
    const uint32_t off_color = lcd.color888(0x0b, 0x0b, 0xff);
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
    fill(index * SIZE_INDICATOR,DISPLAY_HEIGHT - SIZE_INDICATOR, SIZE_INDICATOR, SIZE_INDICATOR, color);
    lcd.endWrite();
}
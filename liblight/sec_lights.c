/*
 * Copyright (C) 2026 prototype74
 * Not a Contribution.
 *
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (C) 2014 The  Linux Foundation. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cutils/log.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>

#include <sys/types.h>

#include <hardware/lights.h>

/******************************************************************************/

static pthread_once_t g_init_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_lcd_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_buttons_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_keyboard_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_led_mutex = PTHREAD_MUTEX_INITIALIZER;

/******************************************************************************/
/* Path arrays — first accessible path wins per device                        */
/******************************************************************************/

enum {
    LIGHT_LCD = 0,
    LIGHT_BUTTONS,
    LIGHT_KEYBOARD,
    LIGHT_SEC_LED_PATTERN,
    LIGHT_SEC_LED_BLINK,
    LIGHT_DEV_COUNT,
};

static const char *const g_light_paths[LIGHT_DEV_COUNT][5] = {
    [LIGHT_LCD] = {
        "/sys/class/leds/lcd-backlight/brightness",
        "/sys/class/backlight/panel/brightness",
        "/sys/class/backlight/panel0-backlight/brightness",
        "/sys/class/backlight/lcd-bl/brightness",
        NULL,
    },
    [LIGHT_BUTTONS] = {
        "/sys/class/sec/sec_touchkey/brightness",
        "/sys/class/leds/button-backlight/brightness",
        "/sys/class/backlight/touchkey-led/brightness",
        NULL,
        NULL,
    },
    [LIGHT_KEYBOARD] = {
        "/sys/class/sec/sec_keypad/brightness",
        "/sys/class/leds/keyboard-backlight/brightness",
        NULL,
        NULL,
        NULL,
    },
    [LIGHT_SEC_LED_PATTERN] = {
        "/sys/class/sec/led/led_pattern",
        NULL,
        NULL,
        NULL,
        NULL,
    },
    [LIGHT_SEC_LED_BLINK] = {
        "/sys/class/sec/led/led_blink",
        NULL,
        NULL,
        NULL,
        NULL,
    },
};

// GED (Generic) LED fallback paths
static const char *const GED_LED_BRIGHTNESS_PATHS[3] = {
    "/sys/class/leds/led_r/brightness",
    "/sys/class/leds/led_g/brightness",
    "/sys/class/leds/led_b/brightness",
};

static const char *const GED_LED_TRIGGER_PATHS[3] = {
    "/sys/class/leds/led_r/trigger",
    "/sys/class/leds/led_g/trigger",
    "/sys/class/leds/led_b/trigger",
};

static const char *const GED_LED_DELAY_ON_PATHS[3] = {
    "/sys/class/leds/led_r/delay_on",
    "/sys/class/leds/led_g/delay_on",
    "/sys/class/leds/led_b/delay_on",
};

static const char *const GED_LED_DELAY_OFF_PATHS[3] = {
    "/sys/class/leds/led_r/delay_off",
    "/sys/class/leds/led_g/delay_off",
    "/sys/class/leds/led_b/delay_off",
};

// Resolved file descriptors
static int g_light_fds[LIGHT_DEV_COUNT] = { -1, -1, -1, -1, -1 };
static int g_ged_brightness_fds[3] = { -1, -1, -1 };
static int g_ged_trigger_fds[3] = { -1, -1, -1 };
static int g_retry_count = 0;

/******************************************************************************/

static const char *const g_light_dev_names[LIGHT_DEV_COUNT] = {
    "lcd", "buttons", "keyboard", "led_pattern", "led_blink",
};

static void initialize_fds(void)
{
    for (int i = 0; i < LIGHT_DEV_COUNT; i++) {
        for (int j = 0; g_light_paths[i][j] != NULL; j++) {
            int fd = open(g_light_paths[i][j], O_RDWR);
            if (fd >= 0) {
                g_light_fds[i] = fd;
                ALOGI("%s: %s using %s", __FUNCTION__,
                      g_light_dev_names[i], g_light_paths[i][j]);
                break;
            }
            g_light_fds[i] = -errno;
        }
        if (g_light_fds[i] < 0)
            ALOGW("%s: %s not available", __FUNCTION__, g_light_dev_names[i]);
    }

    // If sec_led not available, open GED LED fallback paths
    if (g_light_fds[LIGHT_SEC_LED_BLINK] < 0) {
        for (int i = 0; i < 3; i++) {
            g_ged_brightness_fds[i] = open(GED_LED_BRIGHTNESS_PATHS[i], O_RDWR);
            g_ged_trigger_fds[i] = open(GED_LED_TRIGGER_PATHS[i], O_RDWR);
        }
        for (int i = 0; i < 3; i++) {
            if (g_ged_brightness_fds[i] < 0)
                ALOGE("%s: GED led_%c brightness not available",
                      __FUNCTION__, "rgb"[i]);
            if (g_ged_trigger_fds[i] < 0)
                ALOGE("%s: GED led_%c trigger not available",
                      __FUNCTION__, "rgb"[i]);
        }
    }
}

static void reinitialize_fd(int index)
{
    if (g_light_fds[index] >= 0)
        close(g_light_fds[index]);

    for (int j = 0; g_light_paths[index][j] != NULL; j++) {
        int fd = open(g_light_paths[index][j], O_RDWR);
        if (fd >= 0) {
            g_light_fds[index] = fd;
            ALOGW("%s: %s reinitialized via %s", __FUNCTION__,
                  g_light_dev_names[index], g_light_paths[index][j]);
            return;
        }
        g_light_fds[index] = -errno;
    }
    ALOGE("%s: %s reinitialize failed (errno=%d)", __FUNCTION__,
          g_light_dev_names[index], errno);
}

static void init_globals(void)
{
    pthread_mutex_init(&g_lcd_mutex, NULL);
    pthread_mutex_init(&g_buttons_mutex, NULL);
    pthread_mutex_init(&g_keyboard_mutex, NULL);
    pthread_mutex_init(&g_led_mutex, NULL);
    initialize_fds();
}

/******************************************************************************/

static int write_int(int fd, int value)
{
    if (fd < 0)
        return -EBADF;

    char buf[20];
    int len = snprintf(buf, sizeof(buf), "%d\n", value);
    ssize_t ret = write(fd, buf, len);
    return ret == -1 ? -errno : 0;
}

static int write_led_info(int fd, const char *fmt, ...)
{
    if (fd < 0)
        return -EBADF;

    char buf[32];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    ssize_t ret = write(fd, buf, len);
    return ret == -1 ? -errno : 0;
}

static int
rgb_to_brightness(struct light_state_t const *state)
{
    int color = state->color & 0x00ffffff;
    return ((77 * ((color >> 16) & 0xFF))
            + (150 * ((color >> 8) & 0xFF))
            + (29 * (color & 0xFF))) >> 8;
}

static int check_led_open_success(void)
{
    return (g_light_fds[LIGHT_SEC_LED_PATTERN] >= 0 ||
            g_light_fds[LIGHT_SEC_LED_BLINK] >= 0);
}

static int check_led_permission_err(void)
{
    return (g_light_fds[LIGHT_SEC_LED_PATTERN] == -EACCES &&
            g_light_fds[LIGHT_SEC_LED_BLINK] == -EACCES);
}

/******************************************************************************/

static int
set_light_backlight(struct light_device_t *dev,
        struct light_state_t const *state)
{
    (void)dev;
    int brightness;

    if (state->flashMode == 2)
        brightness = state->color;  // raw pass-through
    else
        brightness = rgb_to_brightness(state);

    pthread_mutex_lock(&g_lcd_mutex);
    write_int(g_light_fds[LIGHT_LCD], brightness);
    pthread_mutex_unlock(&g_lcd_mutex);
    return 0;
}

static int
set_light_buttons(struct light_device_t *dev,
        struct light_state_t const *state)
{
    (void)dev;
    int val = (state->color & 0x00ffffff) ? 1 : 0;

    pthread_mutex_lock(&g_buttons_mutex);
    write_int(g_light_fds[LIGHT_BUTTONS], val);
    pthread_mutex_unlock(&g_buttons_mutex);
    return 0;
}

static int
set_light_keyboard(struct light_device_t *dev,
        struct light_state_t const *state)
{
    (void)dev;
    int val = (state->color & 0x00ffffff) ? 1 : 0;

    pthread_mutex_lock(&g_keyboard_mutex);
    write_int(g_light_fds[LIGHT_KEYBOARD], val);
    pthread_mutex_unlock(&g_keyboard_mutex);
    return 0;
}

/******************************************************************************/
/* LED notification handling                                                   */
/******************************************************************************/

static void
write_blink_ged(unsigned int color, int onMS, int offMS)
{
    int red   = (color >> 16) & 0xFF;
    int green = (color >> 8) & 0xFF;
    int blue  = color & 0xFF;

    if (color == 0) {
        red = green = blue = 0;
        onMS = offMS = 0;
    }

    // Reset brightness
    write_led_info(g_ged_brightness_fds[0], "%d", 0);
    write_led_info(g_ged_brightness_fds[1], "%d", 0);
    write_led_info(g_ged_brightness_fds[2], "%d", 0);

    if (offMS > 0 && color != 0) {
        // Blink mode: set trigger to timer
        write_led_info(g_ged_trigger_fds[0], red   ? "timer" : "none");
        write_led_info(g_ged_trigger_fds[1], green ? "timer" : "none");
        write_led_info(g_ged_trigger_fds[2], blue  ? "timer" : "none");

        // Open delay FDs, write, close
        for (int i = 0; i < 3; i++) {
            int fd_off = open(GED_LED_DELAY_OFF_PATHS[i], O_RDWR);
            int fd_on  = open(GED_LED_DELAY_ON_PATHS[i], O_RDWR);

            if (fd_off >= 0) {
                write_led_info(fd_off, "%d", offMS);
                close(fd_off);
            } else {
                ALOGE("%s: GED led_%c delay_off open failed", __FUNCTION__, "rgb"[i]);
            }

            if (fd_on >= 0) {
                write_led_info(fd_on, "%d", onMS);
                close(fd_on);
            } else {
                ALOGE("%s: GED led_%c delay_on open failed", __FUNCTION__, "rgb"[i]);
            }
        }
    } else {
        // Solid or off: trigger none, set brightness
        write_led_info(g_ged_trigger_fds[0], "none");
        write_led_info(g_ged_trigger_fds[1], "none");
        write_led_info(g_ged_trigger_fds[2], "none");
        write_led_info(g_ged_brightness_fds[0], "%d", red);
        write_led_info(g_ged_brightness_fds[1], "%d", green);
        write_led_info(g_ged_brightness_fds[2], "%d", blue);
    }
}

static int
set_light_led(struct light_state_t const *state)
{
    int flashMode = state->flashMode;
    int onMS = state->flashOnMS;
    int offMS = state->flashOffMS;

    pthread_mutex_lock(&g_led_mutex);

    // Retry logic for permission errors (sysfs not yet accessible at boot)
    if (check_led_permission_err() && g_retry_count < 3) {
        reinitialize_fd(LIGHT_SEC_LED_PATTERN);
        reinitialize_fd(LIGHT_SEC_LED_BLINK);
        g_retry_count++;
    }

    switch (flashMode) {
    case 0:  // LIGHT_FLASH_NONE
        if (g_light_fds[LIGHT_SEC_LED_PATTERN] >= 0)
            write_int(g_light_fds[LIGHT_SEC_LED_PATTERN], 0);
        else
            write_blink_ged(0, 0, 0);
        break;

    case 1:  // LIGHT_FLASH_TIMED
    case 2:  // LIGHT_FLASH_HARDWARE
    {
        unsigned int color = state->color;
        if (g_light_fds[LIGHT_SEC_LED_BLINK] >= 0) {
            // SEC LED: write "0xCOLOR onMS offMS\n"
            char buf[32];
            int len = snprintf(buf, sizeof(buf), "0x%x %d %d\n", color, onMS, offMS);
            write(g_light_fds[LIGHT_SEC_LED_BLINK], buf, len);
        } else {
            write_blink_ged(color, onMS, offMS);
        }
        break;
    }

    // Samsung LED pattern modes (ledservice)
    case 10: write_int(g_light_fds[LIGHT_SEC_LED_PATTERN], 1); break;
    case 11: write_int(g_light_fds[LIGHT_SEC_LED_PATTERN], 2); break;
    case 12: write_int(g_light_fds[LIGHT_SEC_LED_PATTERN], 3); break;
    case 13: write_int(g_light_fds[LIGHT_SEC_LED_PATTERN], 4); break;
    case 14: write_int(g_light_fds[LIGHT_SEC_LED_PATTERN], 5); break;
    case 15: write_int(g_light_fds[LIGHT_SEC_LED_PATTERN], 6); break;

    default:
        break;
    }

    pthread_mutex_unlock(&g_led_mutex);
    return 0;
}

static int
set_light_battery(struct light_device_t *dev,
        struct light_state_t const *state)
{
    (void)dev;
    return set_light_led(state);
}

static int
set_light_notification(struct light_device_t *dev,
        struct light_state_t const *state)
{
    (void)dev;
    return set_light_led(state);
}

static int
set_light_attention(struct light_device_t *dev,
        struct light_state_t const *state)
{
    (void)dev;
    return set_light_led(state);
}

static int
set_light_led_service(struct light_device_t *dev,
        struct light_state_t const *state)
{
    (void)dev;
    return set_light_led(state);
}

/******************************************************************************/

static int
close_lights(struct light_device_t *dev)
{
    if (dev)
        free(dev);
    return 0;
}

static int open_lights(const struct hw_module_t *module, const char *name,
        struct hw_device_t **device)
{
    int (*set_light)(struct light_device_t *dev,
            struct light_state_t const *state);

    pthread_once(&g_init_once, init_globals);

    if (0 == strcmp(LIGHT_ID_BACKLIGHT, name)) {
        if (g_light_fds[LIGHT_LCD] < 0)
            return -ENOSYS;
        set_light = set_light_backlight;
    } else if (0 == strcmp(LIGHT_ID_BUTTONS, name)) {
        if (g_light_fds[LIGHT_BUTTONS] < 0)
            return -ENOSYS;
        set_light = set_light_buttons;
    } else if (0 == strcmp(LIGHT_ID_KEYBOARD, name)) {
        if (g_light_fds[LIGHT_KEYBOARD] < 0)
            return -ENOSYS;
        set_light = set_light_keyboard;
    } else if (0 == strcmp(LIGHT_ID_BATTERY, name)) {
        if (!check_led_open_success() && !check_led_permission_err())
            return -ENOSYS;
        set_light = set_light_battery;
    } else if (0 == strcmp(LIGHT_ID_NOTIFICATIONS, name)) {
        if (!check_led_open_success() && !check_led_permission_err())
            return -ENOSYS;
        set_light = set_light_notification;
    } else if (0 == strcmp(LIGHT_ID_ATTENTION, name)) {
        if (!check_led_open_success() && !check_led_permission_err())
            return -ENOSYS;
        set_light = set_light_attention;
    } else if (0 == strcmp("ledservice", name)) {
        if (!check_led_open_success() && !check_led_permission_err())
            return -ENOSYS;
        set_light = set_light_led_service;
    } else {
        ALOGW("%s: unknown light id '%s'", __FUNCTION__, name);
        return -EINVAL;
    }

    struct light_device_t *dev = malloc(sizeof(struct light_device_t));
    if (!dev) {
        ALOGE("%s: failed to allocate device for '%s'", __FUNCTION__, name);
        return -ENOMEM;
    }

    memset(dev, 0, sizeof(*dev));
    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = 0;
    dev->common.module = (struct hw_module_t *)module;
    dev->common.close = (int (*)(struct hw_device_t *))close_lights;
    dev->set_light = set_light;

    *device = (struct hw_device_t *)dev;
    return 0;
}

static struct hw_module_methods_t lights_module_methods = {
    .open = open_lights,
};

struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .version_major = 1,
    .version_minor = 0,
    .id = LIGHTS_HARDWARE_MODULE_ID,
    .name = "Samsung MSM8916 Lights HAL",
    .author = "prototype74",
    .methods = &lights_module_methods,
};

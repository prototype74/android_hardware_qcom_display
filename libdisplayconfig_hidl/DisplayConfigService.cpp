/*
 * Copyright (c) 2017, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define LOG_TAG "DisplayConfigService"

#include <hidl/HidlTransportSupport.h>
#include <vendor/display/config/1.1/IDisplayConfig.h>
#include <log/log.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <cstdio>

using android::hardware::Return;
using android::hardware::Void;
using android::hardware::hidl_vec;
using android::sp;
using vendor::display::config::V1_1::IDisplayConfig;

namespace {

struct DisplayInfo {
    uint32_t xres;
    uint32_t yres;
    uint32_t vsync_period_ns;
    float xdpi;
    float ydpi;
};

static bool getDisplayInfo(DisplayInfo *info) {
    int fd = open("/dev/graphics/fb0", O_RDONLY);
    if (fd < 0) {
        ALOGE("Failed to open /dev/graphics/fb0");
        return false;
    }

    struct fb_var_screeninfo vinfo;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        ALOGE("FBIOGET_VSCREENINFO failed");
        close(fd);
        return false;
    }
    close(fd);

    info->xres = vinfo.xres;
    info->yres = vinfo.yres;

    // pixclock is in KHz in msm8916 kernel (clk_rate / 1000)
    // vsync_period = total_pixels / pixclock
    uint32_t htotal = vinfo.xres + vinfo.left_margin + vinfo.right_margin + vinfo.hsync_len;
    uint32_t vtotal = vinfo.yres + vinfo.upper_margin + vinfo.lower_margin + vinfo.vsync_len;
    uint64_t pixclock_hz = (uint64_t)vinfo.pixclock * 1000;

    if (pixclock_hz > 0 && htotal > 0 && vtotal > 0) {
        uint64_t frame_time_ns = ((uint64_t)htotal * vtotal * 1000000000ULL) / pixclock_hz;
        info->vsync_period_ns = (uint32_t)frame_time_ns;
    } else {
        // Fallback: 60Hz
        info->vsync_period_ns = 16666667;
    }

    // DPI from physical size (mm -> inches)
    if (vinfo.width > 0 && vinfo.height > 0) {
        info->xdpi = (float)(vinfo.xres * 25.4f) / (float)vinfo.width;
        info->ydpi = (float)(vinfo.yres * 25.4f) / (float)vinfo.height;
    } else {
        info->xdpi = 294.0f;
        info->ydpi = 294.0f;
    }

    return true;
}

class DisplayConfigService : public IDisplayConfig {
public:
    // V1_0 methods
    Return<void> isDisplayConnected(
            ::vendor::display::config::V1_0::IDisplayConfig::DisplayType dpy,
            isDisplayConnected_cb _hidl_cb) override {
        // Primary is always connected
        bool connected = (dpy == ::vendor::display::config::V1_0::IDisplayConfig::DisplayType::DISPLAY_PRIMARY);
        _hidl_cb(0, connected);
        return Void();
    }

    // TODO: typo might need a fix (depends on vendor/qcom/opensource)
    Return<int32_t> setSecondayDisplayStatus(
            ::vendor::display::config::V1_0::IDisplayConfig::DisplayType /*dpy*/,
            ::vendor::display::config::V1_0::IDisplayConfig::DisplayExternalStatus /*status*/) override {
        return 0;
    }

    Return<int32_t> configureDynRefeshRate(
            ::vendor::display::config::V1_0::IDisplayConfig::DisplayDynRefreshRateOp /*op*/,
            uint32_t /*refreshRate*/) override {
        return 0;
    }

    Return<void> getConfigCount(
            ::vendor::display::config::V1_0::IDisplayConfig::DisplayType dpy,
            getConfigCount_cb _hidl_cb) override {
        if (dpy == ::vendor::display::config::V1_0::IDisplayConfig::DisplayType::DISPLAY_PRIMARY) {
            _hidl_cb(0, 1);
        } else {
            _hidl_cb(-1, 0);
        }
        return Void();
    }

    Return<void> getActiveConfig(
            ::vendor::display::config::V1_0::IDisplayConfig::DisplayType dpy,
            getActiveConfig_cb _hidl_cb) override {
        if (dpy == ::vendor::display::config::V1_0::IDisplayConfig::DisplayType::DISPLAY_PRIMARY) {
            _hidl_cb(0, 0);
        } else {
            _hidl_cb(-1, 0);
        }
        return Void();
    }

    Return<int32_t> setActiveConfig(
            ::vendor::display::config::V1_0::IDisplayConfig::DisplayType /*dpy*/,
            uint32_t /*config*/) override {
        return 0;
    }

    Return<void> getDisplayAttributes(uint32_t /*configIndex*/,
            ::vendor::display::config::V1_0::IDisplayConfig::DisplayType dpy,
            getDisplayAttributes_cb _hidl_cb) override {
        ::vendor::display::config::V1_0::IDisplayConfig::DisplayAttributes attrs = {};

        if (dpy == ::vendor::display::config::V1_0::IDisplayConfig::DisplayType::DISPLAY_PRIMARY) {
            DisplayInfo info;
            if (getDisplayInfo(&info)) {
                attrs.vsyncPeriod = info.vsync_period_ns;
                attrs.xRes = info.xres;
                attrs.yRes = info.yres;
                attrs.xDpi = info.xdpi;
                attrs.yDpi = info.ydpi;
                attrs.panelType = ::vendor::display::config::V1_0::IDisplayConfig::DisplayPortType::DISPLAY_PORT_DEFAULT;
                attrs.isYuv = false;
                _hidl_cb(0, attrs);
            } else {
                _hidl_cb(-1, attrs);
            }
        } else {
            _hidl_cb(-1, attrs);
        }
        return Void();
    }

    Return<int32_t> setPanelBrightness(uint32_t level) override {
        char path[] = "/sys/class/leds/lcd-backlight/brightness";
        FILE *f = fopen(path, "w");
        if (!f) return -1;
        fprintf(f, "%u", level);
        fclose(f);
        return 0;
    }

    Return<void> getPanelBrightness(getPanelBrightness_cb _hidl_cb) override {
        char path[] = "/sys/class/leds/lcd-backlight/brightness";
        FILE *f = fopen(path, "r");
        if (!f) {
            _hidl_cb(-1, 0);
            return Void();
        }
        uint32_t level = 0;
        fscanf(f, "%u", &level);
        fclose(f);
        _hidl_cb(0, level);
        return Void();
    }

    Return<int32_t> minHdcpEncryptionLevelChanged(
            ::vendor::display::config::V1_0::IDisplayConfig::DisplayType /*dpy*/,
            uint32_t /*min_enc_level*/) override {
        return 0;
    }

    Return<int32_t> refreshScreen() override {
        return 0;
    }

    Return<int32_t> controlPartialUpdate(
            ::vendor::display::config::V1_0::IDisplayConfig::DisplayType /*dpy*/,
            bool /*enable*/) override {
        return 0;
    }

    Return<int32_t> toggleScreenUpdate(bool /*on*/) override {
        return 0;
    }

    Return<int32_t> setIdleTimeout(uint32_t /*value*/) override {
        return 0;
    }

    Return<void> getHDRCapabilities(
            ::vendor::display::config::V1_0::IDisplayConfig::DisplayType /*dpy*/,
            getHDRCapabilities_cb _hidl_cb) override {
        ::vendor::display::config::V1_0::IDisplayConfig::DisplayHDRCapabilities caps = {};
        _hidl_cb(0, caps);
        return Void();
    }

    Return<int32_t> setCameraLaunchStatus(uint32_t /*on*/) override {
        return 0;
    }

    Return<void> displayBWTransactionPending(displayBWTransactionPending_cb _hidl_cb) override {
        _hidl_cb(0, false);
        return Void();
    }

    // V1_1 methods
    Return<int32_t> setDisplayAnimating(uint64_t /*display_id*/, bool /*animating*/) override {
        return 0;
    }
};

}  // anonymous namespace

int main() {
    android::hardware::configureRpcThreadpool(1, true);

    sp<IDisplayConfig> service = new DisplayConfigService();
    android::status_t status = service->registerAsService();

    if (status != android::OK) {
        ALOGE("Could not register IDisplayConfig service (%d)", status);
        return 1;
    }

    ALOGI("IDisplayConfig service registered");
    android::hardware::joinRpcThreadpool();
    return 0;
}

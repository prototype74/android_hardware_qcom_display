LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := vendor.display.config@1.1-service.msm8916
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_VENDOR_MODULE := true
LOCAL_MODULE_TAGS := optional
LOCAL_INIT_RC := vendor.display.config@1.1-service.rc

LOCAL_SRC_FILES := DisplayConfigService.cpp

LOCAL_SHARED_LIBRARIES := \
    libhidlbase \
    libhidltransport \
    libutils \
    liblog \
    vendor.display.config@1.0_vendor \
    vendor.display.config@1.1_vendor

LOCAL_CFLAGS := -Wall -Werror

include $(BUILD_EXECUTABLE)

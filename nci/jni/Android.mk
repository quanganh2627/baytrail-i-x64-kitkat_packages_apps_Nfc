VOB_COMPONENTS := external/libnfc-nci/src
NFA := $(VOB_COMPONENTS)/nfa
NFC := $(VOB_COMPONENTS)/nfc

PN547_EXT_PATH := device/intel/nfc/pn547/extns
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE := false

ifneq ($(NCI_VERSION),)
LOCAL_CFLAGS += -DNCI_VERSION=$(NCI_VERSION) -O0 -g
endif

define all-cpp-files-under
$(patsubst ./%,%, \
  $(shell cd $(LOCAL_PATH) ; \
          find $(1) -name "*.cpp" -and -not -name ".*") \
 )
endef

LOCAL_SRC_FILES:= $(call all-cpp-files-under, .)

LOCAL_C_INCLUDES += \
    bionic \
    bionic/libstdc++ \
    external/stlport/stlport \
    external/libxml2/include \
    external/icu4c/common \
    frameworks/native/include \
    $(NFA)/include \
    $(NFA)/brcm \
    $(NFC)/include \
    $(NFC)/brcm \
    $(NFC)/int \
    $(VOB_COMPONENTS)/hal/include \
    $(VOB_COMPONENTS)/hal/int \
    $(VOB_COMPONENTS)/include \
    $(VOB_COMPONENTS)/gki/ulinux \
    $(VOB_COMPONENTS)/gki/common

LOCAL_SHARED_LIBRARIES := \
    libicuuc \
    libnativehelper \
    libcutils \
    libutils \
    libnfc-nci \
    libstlport

ifeq ($(strip $(BOARD_HAVE_NXP_PN547)), true)
LOCAL_CFLAGS += -DNFCC_DEVICE_PN547C1 -DNXP_EXT
LOCAL_C_INCLUDES += \
    $(PN547_EXT_PATH)/inc \
    $(PN547_EXT_PATH)/osal \
    $(PN547_EXT_PATH)/src

LOCAL_SHARED_LIBRARIES += \
    libnfc_nci_extns
endif

LOCAL_STATIC_LIBRARIES := libxml2

LOCAL_MODULE := libnfc_nci_jni
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)


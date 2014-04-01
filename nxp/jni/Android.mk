LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
ifeq ($(strip $(BOARD_HAVE_NXP_PN544PC)), true)

LOCAL_SRC_FILES:= \
    com_android_nfc_NativeLlcpConnectionlessSocket_1_3.cpp \
    com_android_nfc_NativeLlcpServiceSocket_1_3.cpp \
    com_android_nfc_NativeLlcpSocket_1_3.cpp \
    com_android_nfc_NativeNfcManager_1_3.cpp \
    com_android_nfc_NativeNfcTag_1_3.cpp \
    com_android_nfc_NativeP2pDevice_1_3.cpp \
    com_android_nfc_NativeNfcSecureElement_1_3.cpp \
    com_android_nfc_list_1_3.cpp \
    com_android_nfc_1_3.cpp

LOCAL_C_INCLUDES += \
    $(JNI_H_INCLUDE) \
    external/libnfc-nxp-pn544pc/src \
    external/libnfc-nxp-pn544pc/inc \
	libcore/include

LOCAL_SHARED_LIBRARIES := \
    libnativehelper \
    libcutils \
    libutils \
	liblog \
    libnfc_pn544pc \
    libhardware

#LOCAL_CFLAGS += -O0 -g

LOCAL_CFLAGS += -DNXP_NFC_TGT_SPEED_CONFIG=0x01 -DNXP_FRI1_3

ifeq ($(TARGET_HAS_NFC_CUSTOM_CONFIG),true)
LOCAL_CFLAGS += -DNFC_CUSTOM_CONFIG_INCLUDE
LOCAL_CFLAGS += -I$(TARGET_OUT_HEADERS)/libnfc-nxp-pn544pc
endif

else

LOCAL_SRC_FILES:= \
    com_android_nfc_NativeLlcpConnectionlessSocket.cpp \
    com_android_nfc_NativeLlcpServiceSocket.cpp \
    com_android_nfc_NativeLlcpSocket.cpp \
    com_android_nfc_NativeNfcManager.cpp \
    com_android_nfc_NativeNfcTag.cpp \
    com_android_nfc_NativeP2pDevice.cpp \
    com_android_nfc_NativeNfcSecureElement.cpp \
    com_android_nfc_list.cpp \
    com_android_nfc.cpp

LOCAL_C_INCLUDES += \
    $(JNI_H_INCLUDE) \
    external/libnfc-nxp/src \
    external/libnfc-nxp/inc \
    libcore/include

LOCAL_SHARED_LIBRARIES := \
    libnativehelper \
    libcutils \
    libutils \
    liblog \
    libnfc \
    libhardware

#LOCAL_CFLAGS += -O0 -g

endif

LOCAL_MODULE := libnfc_jni
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

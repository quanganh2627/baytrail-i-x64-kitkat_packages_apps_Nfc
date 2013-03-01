/*
 * Copyright (C) 2012 The Android Open Source Project
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
#include "OverrideLog.h"
#include "SecureElement.h"
#include "JavaClassConstants.h"
#include "PowerSwitch.h"
#include "PeerToPeer.h"
#include "CEFromHost.h"

extern "C"
{
#include "nfa_api.h"
#include "nfa_ee_api.h"
#include "ce_api.h"
}

namespace android
{
extern void startRfDiscovery (bool isStart);
extern bool isDiscoveryStarted();
extern void nativeNfcTag_doTransceiveStatus (uint8_t * buf, uint32_t buflen);

extern SyncEvent sNfaSetConfigEvent;
}

#define NFCID_LEN   4
#define APP_DTA_LEN 4
typedef enum CEFH_Types{
    CEFHTypeA = 0,
    CEFHTResetypeA,
    CEFHTypeB,
    CEFHTResetypeB,
    CEFHInvalid = 0xFF
}CEFH_Types_t;

typedef enum Resource{
    eAllocate = 0,
    eRelease,
    eInvalid = 0xFF
}Resource_t;

typedef struct CEFH_TypeAParams{
    UINT8 sak;
    UINT8 la_platform_config[1];
    UINT8 *appdata;
}CEFH_TypeAParams_t;

typedef struct CEFH_TypeBParams{
    UINT8 SENSB_Info;
    UINT8 *nfcid0;
    UINT8 *appdata;
    UINT8 LB_SFGI;
    UINT8 LB_ADC_FO;
    UINT8 LI_FWI;
    UINT8 LI_BIT_RATE;
}CEFH_TypeBParams_t;

typedef struct CEFHConfigParams{
    CEFH_TypeAParams_t CEFH_TypeA;
    CEFH_TypeBParams_t CEFH_TypeB;
}CEFHConfigParams_t;

namespace android
{
static bool sRfEnabled;
static tNFA_STATUS NatvieNfc_SetCEFH_Config(JNIEnv *e, CEFH_Types_t type, CEFHConfigParams_t *CEFH_Info);
static void cefhEventsCallback(UINT8 event, tNFA_CONN_EVT_DATA *p_data);


UINT8 tech_mask = 0x00;

/*******************************************************************************
 **
 ** Function:        cefhEventsCallback
 **
 ** Description:     Receive cefh-related events from stack.
 **                  event: Event code.
 **                  p_data: Event data.
 **
 ** Returns:         None
 **
 *******************************************************************************/
static void cefhEventsCallback(UINT8 event, tNFA_CONN_EVT_DATA *p_data)
{
    ALOGD("%s: enter", __FUNCTION__);
    ALOGD("%s: enter - event %d", __FUNCTION__,event);

    uint8_t sw[] = {0x90, 0x00};

    switch (event) {
    case NFA_CE_DATA_EVT:
        ALOGD("%s: event - NFA_CE_DATA_EVT", __FUNCTION__);
        CEFromHost::getInstance().connectionEventHandler(event, p_data);
        break;
    case NFA_CE_ACTIVATED_EVT:
        ALOGD("%s: event - NFA_CE_ACTIVATED_EVT - status = 0x%0x", __FUNCTION__,p_data->status);
        CEFromHost::getInstance().notifyCEFHLinkState(true);

        break;

    case NFA_CE_DEACTIVATED_EVT:
        ALOGD("%s: event - NFA_CE_DEACTIVATED_EVT", __FUNCTION__);
        CEFromHost::getInstance().notifyCEFHLinkState(false);
        break;

    case NFA_CE_LOCAL_TAG_CONFIGURED_EVT:
        ALOGD("%s: event - NFA_CE_LOCAL_TAG_CONFIGURED_EVT", __FUNCTION__);
        break;

    case NFA_CE_NDEF_WRITE_START_EVT:
        ALOGD("%s: event - NFA_CE_NDEF_WRITE_START_EVT", __FUNCTION__);
        break;

    case NFA_CE_NDEF_WRITE_CPLT_EVT:
        ALOGD("%s: event - NFA_CE_NDEF_WRITE_CPLT_EVT", __FUNCTION__);
        break;

    case NFA_CE_UICC_LISTEN_CONFIGURED_EVT:
        ALOGD("%s: event - NFA_CE_UICC_LISTEN_CONFIGURED_EVT", __FUNCTION__);
        break;

    case NFA_CE_REGISTERED_EVT:
        ALOGD("%s: event - NFA_CE_REGISTERED_EVT", __FUNCTION__);
        CEFromHost::getInstance().connectionEventHandler(event, p_data);
        break;
    case NFA_CE_DEREGISTERED_EVT:
        ALOGD("%s: event - NFA_CE_DEREGISTERED_EVT", __FUNCTION__);
        CEFromHost::getInstance().connectionEventHandler(event, p_data);
        break;
    default:
        break;
    }
}


/*******************************************************************************
 **
 ** Function:        NativeNfcCEFromHost_dosetTypeA
 **
 ** Description:     Connect to the secure element.
 **                  e: JVM environment.
 **                  o: Java object.
 **
 ** Returns:         Handle of secure element.  0 is failure.
 **
 *******************************************************************************/
static jboolean NativeNfcCEFromHost_dosetTypeA(JNIEnv *e, jobject o,
        jbyte sak, jbyteArray atqa, jbyteArray appData)
{

    tNFA_STATUS status = NFA_STATUS_FAILED;
    tNFA_HANDLE eeHandle = NFA_EE_HANDLE_DH;
    CEFHConfigParams_t CEFH_Info;
    CEFH_Types_t type = CEFHTypeA;
    //    UINT8 protoMask = NFA_PROTOCOL_MASK_ISO_DEP;
    UINT8* jAppData = NULL;
    ALOGD("%s: enter", __FUNCTION__);

    PowerSwitch::getInstance ().setLevel (PowerSwitch::FULL_POWER);
    sRfEnabled = isDiscoveryStarted();
    if (sRfEnabled) {
        // Stop RF Discovery if we were polling
        startRfDiscovery (false);
    }

    // to enable CEFH required configurations
    CEFH_Info.CEFH_TypeA.sak = sak;
    jAppData = (UINT8*) e->GetByteArrayElements(appData,0);
    CEFH_Info.CEFH_TypeA.appdata = jAppData;
    CEFH_Info.CEFH_TypeA.la_platform_config[0] = 0x03;

    //    e->ReleaseByteArrayElements (appData, (jbyte *) jAppData, JNI_ABORT);

    status = NatvieNfc_SetCEFH_Config(e, type, &CEFH_Info);

    if (status == NFA_STATUS_OK) {
        ALOGD ("%s: Enabled CEfrom Host configurations", __FUNCTION__);
    } else {
        goto TheEnd;
        ALOGE ("%s: Failed to enabled CEfrom Host configurations", __FUNCTION__);
    }

    CEFromHost::getInstance().enableCEType(NFA_TECHNOLOGY_MASK_A);

    e->ReleaseByteArrayElements (appData, (jbyte *) jAppData, JNI_ABORT);

    //    A000 0000 0410 1000
    if(CEFromHost::getInstance().isEnabled() != true)
    {
        bool ret = CEFromHost::getInstance().registerCEEventsListener(cefhEventsCallback);
        if(ret == false)
            goto TheEnd;
    }

    CEFromHost::getInstance().setEnabled(CEFromHost::CE_TYPE_A, true);

    startRfDiscovery (true);

    TheEnd:
    ALOGD ("%s: exit", __FUNCTION__);
    status = (status == NFA_STATUS_OK)?TRUE:FALSE;

    return status;
}

static jboolean NativeNfcCEFromHost_dosetTypeB(JNIEnv *e, jobject o,
        jbyteArray atqb, jbyteArray hilayerresp, jint afi) {

    tNFA_STATUS status = NFA_STATUS_FAILED;

    UINT8 techMask = NFA_TECHNOLOGY_MASK_A | NFA_TECHNOLOGY_MASK_B;
    UINT8 nfcid[NFCID_LEN] = {0xE2, 0x87, 0xE0, 0xEF};
    UINT8 data[] = {0x00, 0x00, 0x00, 0x00};
    CEFH_Types_t type = CEFHTypeB;
    CEFHConfigParams_t CEFH_Info;
    ALOGD("%s: enter", __FUNCTION__);

    PowerSwitch::getInstance ().setLevel (PowerSwitch::FULL_POWER);
    sRfEnabled = isDiscoveryStarted();
    if (sRfEnabled) {
        // Stop RF Discovery if we were polling
        startRfDiscovery (false);
    }

    CEFH_Info.CEFH_TypeB.SENSB_Info = 0x01;
    CEFH_Info.CEFH_TypeB.nfcid0 = nfcid;
    CEFH_Info.CEFH_TypeB.appdata = data;
    CEFH_Info.CEFH_TypeB.LB_SFGI = 0x07;
    CEFH_Info.CEFH_TypeB.LB_ADC_FO = 0x70;
    CEFH_Info.CEFH_TypeB.LI_BIT_RATE = 0x03;
    CEFH_Info.CEFH_TypeB.LI_FWI = 0x07;

    // to enable CEFH required configurations
    status = NatvieNfc_SetCEFH_Config(e, type, &CEFH_Info);

    if (status == NFA_STATUS_OK) {
        ALOGD ("%s: Enabled CEfrom Host configurations", __FUNCTION__);
    } else {
        goto TheEnd;
        ALOGE ("%s: Failed to enabled CEfrom Host configurations", __FUNCTION__);
    }

    CEFromHost::getInstance().configLocalTag(data);

    CEFromHost::getInstance().enableCEType(NFA_TECHNOLOGY_MASK_B);

    if(CEFromHost::getInstance().isEnabled() != true)
    {
        bool ret = CEFromHost::getInstance().registerCEEventsListener(cefhEventsCallback);
        if(ret == false)
            goto TheEnd;
    }

    CEFromHost::getInstance().setEnabled(CEFromHost::CE_TYPE_B, true);


    //start discovery
    startRfDiscovery (true);

    TheEnd:
    ALOGD ("%s: exit", __FUNCTION__);
    status = (status == NFA_STATUS_OK)?TRUE:FALSE;

    return status;
}

static void NativeNfcCEFromHost_doresetType(JNIEnv *e, jobject o)
{
    tNFA_STATUS status = NFA_STATUS_FAILED;
    CEFHConfigParams_t CEFH_Info;
    ALOGD("%s: enter", __FUNCTION__);

    sRfEnabled = isDiscoveryStarted();
    if (sRfEnabled) {
        // Stop RF Discovery if we were polling
        startRfDiscovery (false);
    }

    if(CEFromHost::getInstance().isEnabled() == true)
    {
        status = CEFromHost::getInstance().deRegisterCEEventsListener();
    }

    if(CEFromHost::getInstance().isEnabled(CEFromHost::CE_TYPE_A) == true)
    {
        // to disable CEFH required configurations
        memset(&CEFH_Info, 0, sizeof(CEFHConfigParams_t));
        status = NatvieNfc_SetCEFH_Config(e, CEFHTResetypeA, &CEFH_Info);
        if (status == NFA_STATUS_OK) {
            ALOGD ("%s: Disable CEfrom Host configurations", __FUNCTION__);
        } else {
            goto TheEnd;
            ALOGE ("%s: Failed to disable CEfrom Host configurations", __FUNCTION__);
        }

        CEFromHost::getInstance().setEnabled(CEFromHost::CE_TYPE_A, false);

    }

    if(CEFromHost::getInstance().isEnabled(CEFromHost::CE_TYPE_B) == true)
    {
        memset(&CEFH_Info, 0, sizeof(CEFHConfigParams_t));
        CEFH_Info.CEFH_TypeB.SENSB_Info = 0x00;

        status = NatvieNfc_SetCEFH_Config(e, CEFHTResetypeB, &CEFH_Info);
        if (status == NFA_STATUS_OK) {
            ALOGD ("%s: Disable CEfrom Host configurations", __FUNCTION__);
        } else {
            goto TheEnd;
            ALOGE ("%s: Failed to disable CEfrom Host configurations", __FUNCTION__);
        }

        CEFromHost::getInstance().setEnabled(CEFromHost::CE_TYPE_B, false);
    }

    CEFromHost::getInstance().enableCEType(0);

    startRfDiscovery (true);

    TheEnd:
    ALOGD ("%s: exit", __FUNCTION__);
}


static jboolean NativeNfcCEFromHost_dosend(JNIEnv *e, jobject o, jbyteArray  data)
{
    bool status = false;
    uint8_t *buf, *tempbuf = NULL;
    uint32_t bufLen = 0;

    ALOGD("%s: enter", __FUNCTION__);
    buf = (uint8_t*)e->GetByteArrayElements(data, NULL);
    bufLen = (uint32_t)e->GetArrayLength(data);

    status =  CEFromHost::getInstance().send(buf, bufLen);

    ALOGD("%s: exit", __FUNCTION__);

    return status;
}


static jbyteArray NativeNfcCEFromHost_doreceive(JNIEnv *e, jobject o)
{
    ALOGD("%s: enter", __FUNCTION__);

    bool status = false;
    jbyteArray recvdata = NULL;

    uint8_t *buf = NULL;
    uint16_t bufLen = 0;

    status = CEFromHost::getInstance().receive(&buf, &bufLen);

    ALOGD("%s: dataLen = %d", __FUNCTION__, bufLen);

    if(status == true)
    {
        recvdata = e->NewByteArray (bufLen);
        e->SetByteArrayRegion (recvdata, 0, bufLen, (jbyte*) buf);
    }

    ALOGD("%s: exit", __FUNCTION__);

    return recvdata;
}

/*****************************************************************************
 **
 ** Description:     JNI functions
 **
 *****************************************************************************/
static JNINativeMethod gMethods[] =
{
        {"dosetNfcCEFromHostTypeA", "(B[B[B)Z", (void *)NativeNfcCEFromHost_dosetTypeA},
        {"dosetNfcCEFromHostTypeB", "([B[BI)Z", (void *)NativeNfcCEFromHost_dosetTypeB},
        {"doresetNfcCEFromHostType", "()V", (void *)NativeNfcCEFromHost_doresetType},
        {"dosendNfcCEFromHost", "([B)Z", (void *)NativeNfcCEFromHost_dosend},
        {"doreceiveNfcCEFromHost", "()[B", (void *)NativeNfcCEFromHost_doreceive},
};

/*******************************************************************************
 **
 ** Function:        register_com_android_nfc_NativeNfcCEFromHost
 **
 ** Description:     Regisgter JNI functions with Java Virtual Machine.
 **                  e: Environment of JVM.
 **
 ** Returns:         Status of registration.
 **
 *******************************************************************************/
int register_com_android_nfc_NativeNfcCEFromHost(JNIEnv *e)
{
    return jniRegisterNativeMethods(e, gNativeNfcCEFromHostClassName,
            gMethods, NELEM(gMethods));
}


static tNFA_STATUS NatvieNfc_SetCEFH_Config(JNIEnv *e, CEFH_Types_t type, CEFHConfigParams_t *CEFH_Info)
{
    tNFA_STATUS status = NFA_STATUS_FAILED;

    ALOGD("%s: enter", __FUNCTION__);
    if (CEFH_Info == NULL)
    {
        status = NFA_STATUS_INVALID_PARAM;
    }
    else
    {
        switch(type)
        {
        case CEFHTypeA:
            ALOGD("Set config for CEFHTypeA");
            {
                SyncEventGuard guard (android::sNfaSetConfigEvent);
                status = NFA_SetConfig(NCI_PARAM_ID_LA_SEL_INFO, sizeof(UINT8), &(CEFH_Info->CEFH_TypeA.sak));
                if (status == NFA_STATUS_OK)
                    sNfaSetConfigEvent.wait ();
            }

            {
                SyncEventGuard guard (android::sNfaSetConfigEvent);
                status |= NFA_SetConfig(NCI_PARAM_ID_LA_NFCID1, APP_DTA_LEN, CEFH_Info->CEFH_TypeA.appdata);
                if (status == NFA_STATUS_OK)
                    sNfaSetConfigEvent.wait ();

            }

            {
                SyncEventGuard guard (android::sNfaSetConfigEvent);
                status |= NFA_SetConfig(NCI_PARAM_ID_LA_PLATFORM_CONFIG, sizeof(UINT8), &CEFH_Info->CEFH_TypeA.la_platform_config[0]);
                if (status == NFA_STATUS_OK)
                    sNfaSetConfigEvent.wait ();

            }
            break;

        case CEFHTResetypeA:
            ALOGD("Set config for CEFHResetTypeA");
            {
                CEFH_Info->CEFH_TypeA.sak = 0x40;
                SyncEventGuard guard (android::sNfaSetConfigEvent);
                status = NFA_SetConfig(NCI_PARAM_ID_LA_SEL_INFO, sizeof(UINT8), &(CEFH_Info->CEFH_TypeA.sak));
                if (status == NFA_STATUS_OK)
                    sNfaSetConfigEvent.wait ();
            }

            {
                SyncEventGuard guard (android::sNfaSetConfigEvent);
                status |= NFA_SetConfig(NCI_PARAM_ID_LA_NFCID1, 0x00, NULL);
                if (status == NFA_STATUS_OK)
                    sNfaSetConfigEvent.wait ();

            }

            {
                SyncEventGuard guard (android::sNfaSetConfigEvent);
                status |= NFA_SetConfig(NCI_PARAM_ID_LA_PLATFORM_CONFIG, 0x00, NULL);
                if (status == NFA_STATUS_OK)
                    sNfaSetConfigEvent.wait ();

            }
            break;

        case CEFHTypeB:
            ALOGD("Set config for CEFHTypeB");
            {
                SyncEventGuard guard (android::sNfaSetConfigEvent);
                status = NFA_SetConfig(NCI_PARAM_ID_LB_SENSB_INFO, sizeof(UINT8), &(CEFH_Info->CEFH_TypeB.SENSB_Info));
                if (status == NFA_STATUS_OK)
                    sNfaSetConfigEvent.wait ();
            }

            {
                SyncEventGuard guard (sNfaSetConfigEvent);
                status |= NFA_SetConfig(NCI_PARAM_ID_LB_NFCID0, NFCID_LEN, CEFH_Info->CEFH_TypeB.nfcid0);
                if (status == NFA_STATUS_OK)
                    sNfaSetConfigEvent.wait ();
            }

            {
                SyncEventGuard guard (sNfaSetConfigEvent);
                status |= NFA_SetConfig(NCI_PARAM_ID_LB_APPDATA, APP_DTA_LEN, CEFH_Info->CEFH_TypeB.appdata);
                if (status == NFA_STATUS_OK)
                    sNfaSetConfigEvent.wait ();
            }

            {
                SyncEventGuard guard (sNfaSetConfigEvent);
                status |= NFA_SetConfig(NCI_PARAM_ID_LB_SFGI, sizeof(UINT8), &CEFH_Info->CEFH_TypeB.LB_SFGI);
                if (status == NFA_STATUS_OK)
                    sNfaSetConfigEvent.wait ();
            }

            {
                SyncEventGuard guard (sNfaSetConfigEvent);
                status |= NFA_SetConfig(NCI_PARAM_ID_LB_ADC_FO, sizeof(UINT8), &CEFH_Info->CEFH_TypeB.LB_ADC_FO);
                if (status == NFA_STATUS_OK)
                    sNfaSetConfigEvent.wait ();
            }

            {
                SyncEventGuard guard (sNfaSetConfigEvent);
                status |= NFA_SetConfig(NCI_PARAM_ID_LI_BIT_RATE, sizeof(UINT8), &CEFH_Info->CEFH_TypeB.LI_BIT_RATE);
                if (status == NFA_STATUS_OK)
                    sNfaSetConfigEvent.wait ();
            }

            {
                SyncEventGuard guard (sNfaSetConfigEvent);
                status |= NFA_SetConfig(NCI_PARAM_ID_FWI, sizeof(UINT8), &CEFH_Info->CEFH_TypeB.LI_FWI);
                if (status == NFA_STATUS_OK)
                    sNfaSetConfigEvent.wait ();
            }
            break;

        case CEFHTResetypeB:
            ALOGD("Set config for CEFHTypeB");
            {
                SyncEventGuard guard (android::sNfaSetConfigEvent);
                status = NFA_SetConfig(NCI_PARAM_ID_LB_SENSB_INFO, sizeof(UINT8), &CEFH_Info->CEFH_TypeB.SENSB_Info);
                if (status == NFA_STATUS_OK)
                    sNfaSetConfigEvent.wait ();
            }

            {
                SyncEventGuard guard (sNfaSetConfigEvent);
                status |= NFA_SetConfig(NCI_PARAM_ID_LB_NFCID0, 0x00, NULL);
                if (status == NFA_STATUS_OK)
                    sNfaSetConfigEvent.wait ();
            }

            {
                SyncEventGuard guard (sNfaSetConfigEvent);
                status |= NFA_SetConfig(NCI_PARAM_ID_LB_APPDATA, 0x00, NULL);
                if (status == NFA_STATUS_OK)
                    sNfaSetConfigEvent.wait ();
            }

            {
                SyncEventGuard guard (sNfaSetConfigEvent);
                status |= NFA_SetConfig(NCI_PARAM_ID_LB_SFGI, 0x00, NULL);
                if (status == NFA_STATUS_OK)
                    sNfaSetConfigEvent.wait ();
            }

            {
                SyncEventGuard guard (sNfaSetConfigEvent);
                status |= NFA_SetConfig(NCI_PARAM_ID_LB_ADC_FO, 0x00, NULL);
                if (status == NFA_STATUS_OK)
                    sNfaSetConfigEvent.wait ();
            }

            {
                SyncEventGuard guard (sNfaSetConfigEvent);
                status |= NFA_SetConfig(NCI_PARAM_ID_LI_BIT_RATE, 0x00, NULL);
                if (status == NFA_STATUS_OK)
                    sNfaSetConfigEvent.wait ();
            }

            {
                SyncEventGuard guard (sNfaSetConfigEvent);
                status |= NFA_SetConfig(NCI_PARAM_ID_FWI, 0x00, NULL);
                if (status == NFA_STATUS_OK)
                    sNfaSetConfigEvent.wait ();
            }
            break;

        default:
            status = NFA_STATUS_INVALID_PARAM;
            break;
        }
    }
    return status;
}

} /*namespace android*/

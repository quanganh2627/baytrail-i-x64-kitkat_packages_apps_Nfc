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
#include <semaphore.h>
#include <errno.h>
#include "OverrideLog.h"
#include "NfcJniUtil.h"
#include "SyncEvent.h"
#include "JavaClassConstants.h"
#include "config.h"

extern "C"
{
#include "nfa_api.h"
}

typedef struct nxp_feature_data
{
    SyncEvent    NxpFeatureConfigEvt;
    tNFA_STATUS  wstatus;
}Nxp_Feature_Data_t;

namespace android
{
static Nxp_Feature_Data_t gnxpfeature_conf;

}

void SetCbStatus(tNFA_STATUS status);
tNFA_STATUS GetCbStatus(void);
static void NxpResponse_Cb(UINT8 event, UINT16 param_len, UINT8 *p_param);
static void NxpResponse_SetDhlf_Cb(UINT8 event, UINT16 param_len, UINT8 *p_param);
void NxpResponse_SetVenConfig_Cb(UINT8 event, UINT16 param_len, UINT8 *p_param);


namespace android
{
void SetCbStatus(tNFA_STATUS status)
{
    gnxpfeature_conf.wstatus = status;
}

tNFA_STATUS GetCbStatus(void)
{
    return gnxpfeature_conf.wstatus;
}

static void NxpResponse_Cb(UINT8 event, UINT16 param_len, UINT8 *p_param)
{

    ALOGD("NxpResponse_Cb Received length data = 0x%x status = 0x%x", param_len, p_param[3]);

    if (p_param[3] == 0x00)
    {
        SetCbStatus(NFA_STATUS_OK);
    }
    else
    {
        SetCbStatus(NFA_STATUS_FAILED);
    }

    SyncEventGuard guard(gnxpfeature_conf.NxpFeatureConfigEvt);
    gnxpfeature_conf.NxpFeatureConfigEvt.notifyOne ();

}
static void NxpResponse_SetDhlf_Cb(UINT8 event, UINT16 param_len, UINT8 *p_param)
{

    ALOGD("NxpResponse_SetDhlf_Cb Received length data = 0x%x status = 0x%x", param_len, p_param[3]);

    if (p_param[3] == 0x00)
    {
        SetCbStatus(NFA_STATUS_OK);
    }
    else
    {
        SetCbStatus(NFA_STATUS_FAILED);
    }

    SyncEventGuard guard(gnxpfeature_conf.NxpFeatureConfigEvt);
    gnxpfeature_conf.NxpFeatureConfigEvt.notifyOne ();

}
void NxpResponse_SetVenConfig_Cb(UINT8 event, UINT16 param_len, UINT8 *p_param)
{
    ALOGD("NxpResponse_SetVenConfig_Cb Received length data = 0x%x status = 0x%x", param_len, p_param[3]);
    if (p_param[3] == 0x00)
    {
        SetCbStatus(NFA_STATUS_OK);
    }
    else
    {
        SetCbStatus(NFA_STATUS_FAILED);
    }
    SyncEventGuard guard(gnxpfeature_conf.NxpFeatureConfigEvt);
    gnxpfeature_conf.NxpFeatureConfigEvt.notifyOne ();
}

#if (NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
/*******************************************************************************
 **
 ** Function:        EmvCo_dosetPoll
 **
 ** Description:     Enable/disable Emv Co polling
 **
 ** Returns:         success/failure
 **
 *******************************************************************************/
tNFA_STATUS EmvCo_dosetPoll(jboolean enable)
{
    tNFA_STATUS status = NFA_STATUS_FAILED;
    uint8_t cmd_buf[] ={0x20, 0x02, 0x05, 0x01, 0xA0, 0x44, 0x01, 0x00};

    ALOGD("%s: enter", __FUNCTION__);

    SetCbStatus(NFA_STATUS_FAILED);
    SyncEventGuard guard (gnxpfeature_conf.NxpFeatureConfigEvt);
    if (enable)
    {
        ALOGD("EMV-CO polling profile");
        cmd_buf[7] = 0x01; /*EMV-CO Poll*/
    }
    else
    {
        ALOGD("NFC forum polling profile");
    }
    status = NFA_SendNxpNciCommand(sizeof(cmd_buf), cmd_buf, NxpResponse_Cb);
    if (status == NFA_STATUS_OK) {
        ALOGD ("%s: Success NFA_SendNxpNciCommand", __FUNCTION__);
        gnxpfeature_conf.NxpFeatureConfigEvt.wait(); /* wait for callback */
    } else {
        ALOGE ("%s: Failed NFA_SendNxpNciCommand", __FUNCTION__);
    }

    status = GetCbStatus();
    return status;
}

/*******************************************************************************
 **
 ** Function:        SetDHListenFilter
 **
 ** Description:     set/clear SetDHListenFilter
 **
 ** Returns:         success/failure
 **
 *******************************************************************************/
tNFA_STATUS SetDHListenFilter(jboolean enable)
{
    tNFA_STATUS status = NFA_STATUS_FAILED;
    uint8_t cmd_buf[] ={0x2F, 0x15, 0x01, 0x01};

    ALOGD("%s: enter", __FUNCTION__);

    SetCbStatus(NFA_STATUS_FAILED);
    SyncEventGuard guard (gnxpfeature_conf.NxpFeatureConfigEvt);
    if (enable==true)
    {
        ALOGD("Set SetDHListenFilter");
        cmd_buf[3] = 0x01;
    }
    else
    {
        ALOGD("Clear SetDHListenFilter");
        cmd_buf[3] = 0x00;
    }
    status = NFA_SendNxpNciCommand(sizeof(cmd_buf), cmd_buf, NxpResponse_SetDhlf_Cb);
    if (status == NFA_STATUS_OK) {
        ALOGD ("%s: Success NFA_SendNxpNciCommand", __FUNCTION__);
        gnxpfeature_conf.NxpFeatureConfigEvt.wait(); /* wait for callback */
    } else {
        ALOGE ("%s: Failed NFA_SendNxpNciCommand", __FUNCTION__);
    }

    status = GetCbStatus();
    return status;
}


/*******************************************************************************
 **
 ** Function:        SetVenConfigValue
 **
 ** Description:     setting the Ven Config Value
 **
 ** Returns:         success/failure
 **
 *******************************************************************************/
tNFA_STATUS SetVenConfigValue(jint venconfig)
{
    tNFA_STATUS status = NFA_STATUS_FAILED;
    uint8_t cmd_buf[] = {0x20, 0x02, 0x05, 0x01, 0xA0, 0x07, 0x01, 0x03};
    ALOGD("%s: enter", __FUNCTION__);
    if (venconfig == VEN_CFG_NFC_OFF_POWER_OFF)
    {
        ALOGD("Setting the VEN_CFG to 2, Disable ESE events");
        cmd_buf[7] = 0x02;
    }
    else if (venconfig == VEN_CFG_NFC_ON_POWER_ON)
    {
        ALOGD("Setting the VEN_CFG to 3, Make ");
        cmd_buf[7] = 0x03;
    }
    else
    {
        ALOGE("Wrong VEN_CFG Value");
        return status;
    }
    SetCbStatus(NFA_STATUS_FAILED);
    SyncEventGuard guard (gnxpfeature_conf.NxpFeatureConfigEvt);
    status = NFA_SendNxpNciCommand(sizeof(cmd_buf), cmd_buf, NxpResponse_SetVenConfig_Cb);
    if (status == NFA_STATUS_OK)
    {
        ALOGD ("%s: Success NFA_SendNxpNciCommand", __FUNCTION__);
        gnxpfeature_conf.NxpFeatureConfigEvt.wait(); /* wait for callback */
    }
    else
    {
        ALOGE ("%s: Failed NFA_SendNxpNciCommand", __FUNCTION__);
    }
    status = GetCbStatus();
    return status;
}
#endif
} /* namespace android */
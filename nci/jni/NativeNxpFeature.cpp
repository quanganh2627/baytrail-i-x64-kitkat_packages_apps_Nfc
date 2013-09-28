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

//namespace android
//{
static Nxp_Feature_Data_t gnxpfeature_conf;

//}

void SetCbStatus(tNFA_STATUS status);
tNFA_STATUS GetCbStatus(void);
static void NxpResponse_Cb(UINT8 event, UINT16 param_len, UINT8 *p_param);

//namespace android
//{
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

    if(p_param[3] == 0x00)
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
    if(enable)
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

//} /*namespace android*/

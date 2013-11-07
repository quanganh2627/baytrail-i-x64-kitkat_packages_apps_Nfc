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
/******************************************************************************
 *
 *  The original Work has been changed by NXP Semiconductors.
 *
 *  Copyright (C) 2013 NXP Semiconductors
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************/
#include "OverrideLog.h"
#include <semaphore.h>
#include <JcopOsDownload.h>
#include "JavaClassConstants.h"
#include "SecureElement.h"
#include <errno.h>


JcopOsDwnld JcopOsDwnld::sJcopDwnld;
INT32 gTransceiveTimeout = 120000;

tNFA_STATUS (*JcopOsDwnld::JcopOs_dwnld_seqhandler[])(
            JcopOs_ImageInfo_t* pContext, tNFA_STATUS status, JcopOs_TranscieveInfo_t* pInfo)={
       JcopOsDwnld::TriggerApdu,
       JcopOsDwnld::GetInfo,
       JcopOsDwnld::load_JcopOS_image,
       NULL
   };


tNFA_STATUS JcopOs_Dwnld_SeqHandler(JcopOs_ImageInfo_t* pContext, tNFA_STATUS status, JcopOs_TranscieveInfo_t* pInfo);

pJcopOs_Dwnld_Context_t gpJcopOs_Dwnld_Context = NULL;
/*******************************************************************************
**
** Function:        getInstance
**
** Description:     Get the JcopOsDwnld singleton object.
**
** Returns:         JcopOsDwnld object.
**
*******************************************************************************/
JcopOsDwnld& JcopOsDwnld::getInstance()
{
    return sJcopDwnld;
}

/*******************************************************************************
**
** Function:        initialize
**
** Description:     Initialize all member variables.
**                  native: Native data.
**
** Returns:         True if ok.
**
*******************************************************************************/
bool JcopOsDwnld::initialize (nfc_jni_native_data* native)
{
    static const char fn [] = "JcopOsDwnld::initialize";

    ALOGD ("%s: enter", fn);

    mNativeData     = native;

    gpJcopOs_Dwnld_Context = (pJcopOs_Dwnld_Context_t)malloc(sizeof(JcopOs_Dwnld_Context_t));
    if(gpJcopOs_Dwnld_Context != NULL)
    {
        memset((void *)gpJcopOs_Dwnld_Context, 0, (UINT32)sizeof(JcopOs_Dwnld_Context_t));
    }
    else
    {
        ALOGD("%s: Memory allocation failed", fn);
        return (false);
    }
    mIsInit = true;

    ALOGD ("%s: exit", fn);
    return (true);
}

/*******************************************************************************
**
** Function:        finalize
**
** Description:     Release all resources.
**
** Returns:         None
**
*******************************************************************************/
void JcopOsDwnld::finalize ()
{
    static const char fn [] = "JcopOsDwnld::finalize";
    ALOGD ("%s: enter", fn);
    mNativeData   = NULL;
    mIsInit       = false;
    free(gpJcopOs_Dwnld_Context);
    gpJcopOs_Dwnld_Context = NULL;

    ALOGD ("%s: exit", fn);
}
/*******************************************************************************
**
** Function:        IsWiredMode_Enable
**
** Description:     Provides the connection status of EE
**
** Returns:         True if ok.
**
*******************************************************************************/
bool JcopOsDwnld::IsWiredMode_Enable()
{
    static const char fn [] = "JcopOsDwnld::IsWiredMode_Enable";
    ALOGD ("%s: enter", fn);

    tNFA_STATUS stat = NFA_STATUS_FAILED;
    tNFA_EE_INFO mEeInfo [MAX_NUM_EE];
    mActualNumEe = MAX_NUM_EE;
    meSE = 0x4C0;

    if(mIsInit == false)
    {
        ALOGD ("%s: JcopOs Dwnld is not initialized", fn);
        goto TheEnd;
    }
    stat = NFA_EeGetInfo(&mActualNumEe, mEeInfo);
    if(stat == NFA_STATUS_OK)
    {
        for(int xx = 0; xx <  mActualNumEe; xx++)
        {
            if ((mEeInfo[xx].ee_handle == meSE) &&
                (mEeInfo[xx].ee_status == 0x00))
            {
                stat = NFA_STATUS_OK;
                ALOGD ("%s: status = 0x%x", fn, stat);
                break;
            }
            else
            {
                stat = NFA_STATUS_FAILED;
            }
        }
    }
TheEnd:
    ALOGD("%s: exit; status = 0x%X", fn, stat);
    if(stat == NFA_STATUS_OK)
        return true;
    else
        return false;
}

/*******************************************************************************
**
** Function:        JcopOs_Download
**
** Description:     Starts the OS download sequence
**
** Returns:         NFA_Success if ok.
**
*******************************************************************************/
tNFA_STATUS JcopOsDwnld::JcopOs_Download()
{
    static const char fn [] = "JcopOsDwnld::JcopOs_Download";
    tNFA_STATUS wstatus = NFA_STATUS_FAILED;
    JcopOs_TranscieveInfo_t pTranscv_Info;
    JcopOs_ImageInfo_t ImageInfo;
    SecureElement &se = SecureElement::getInstance();
    ALOGD("%s: enter:", fn);
    if(mIsInit == false)
    {
        //ALOGD("%s: JcopOs_Dwnld_Context is NULL");
        ALOGD ("%s: JcopOs Dwnld is not initialized", fn);
        wstatus = NFA_STATUS_FAILED;
    }
    else
    {
        wstatus = JcopOsDwnld::JcopOs_update_seq_handler(JcopOs_dwnld_seqhandler);
        if(wstatus != NFA_STATUS_OK)
        {
            ALOGE("%s: JCOP OS update failed; status=0x%X", fn, wstatus);
        }
        else
        {
            ALOGE("%s: JCOP OS update is success; status =0x%X", fn, wstatus);
        }
        JcopOsDwnld::doeSE_Reset();
    }
    ALOGD("%s: exit; status = 0x%x", fn, wstatus);
    return wstatus;
}

tNFA_STATUS JcopOsDwnld::JcopOs_update_seq_handler(tNFA_STATUS (*seq_handler[])(JcopOs_ImageInfo_t* pContext, tNFA_STATUS status, JcopOs_TranscieveInfo_t* pInfo))
{
    static const char fn[] = "JcopOsDwnld::JcopOs_update_seq_handler";
    int16_t seq_counter = 0;
    JcopOs_ImageInfo_t update_info = (JcopOs_ImageInfo_t )gpJcopOs_Dwnld_Context->Image_info;
    JcopOs_TranscieveInfo_t trans_info = (JcopOs_TranscieveInfo_t )gpJcopOs_Dwnld_Context->pJcopOs_TransInfo;
    tNFA_STATUS status = NFA_STATUS_FAILED;
    ALOGD("%s: enter", fn);

    while(seq_handler[seq_counter] != NULL )
    {
        status = NFA_STATUS_FAILED;
        status = (seq_handler[seq_counter])(&update_info, status, &trans_info );
        if(NFA_STATUS_OK != status)
        {
            ALOGE(" JcopOs_update_seq_handler : FAILED");
            break;
        }
        seq_counter++;
    }
    return status;
}

tNFA_STATUS JcopOsDwnld::TriggerApdu(JcopOs_ImageInfo_t* pVersionInfo, tNFA_STATUS status, JcopOs_TranscieveInfo_t* pTranscv_Info)
{
    static const char fn [] = "JcopOsDwnld::TriggerApdu";
    //tNFA_STATUS status = NFA_STATUS_FAILED;
    bool stat = false;
    INT32 recvBufferActualSize = 0;
    SecureElement &se = SecureElement::getInstance();

    ALOGD("%s: enter;", fn);

    if(pTranscv_Info == NULL ||
       pVersionInfo == NULL)
    {
        ALOGD("%s: Invalid parameter", fn);
        status = NFA_STATUS_FAILED;
    }
    else
    {
        pTranscv_Info->timeout = gTransceiveTimeout;
        pTranscv_Info->sSendlength = (INT32)sizeof(Trigger_APDU);
        pTranscv_Info->sRecvlength = 1024;//(INT32)sizeof(INT32);
        memcpy(pTranscv_Info->sSendData, Trigger_APDU, pTranscv_Info->sSendlength);

        ALOGD("%s: Calling Secure Element Transceive", fn);
        stat = se.transceive (pTranscv_Info->sSendData,
                                pTranscv_Info->sSendlength,
                                pTranscv_Info->sRecvData,
                                pTranscv_Info->sRecvlength,
                                recvBufferActualSize,
                                pTranscv_Info->timeout);
        if (stat != true)
        {
            status = NFA_STATUS_FAILED;
            ALOGE("%s: SE transceive failed status = 0x%X", fn, status);//Stop JcopOs Update
        }
        else if(((pTranscv_Info->sRecvData[recvBufferActualSize-2] == 0x68) &&
               (pTranscv_Info->sRecvData[recvBufferActualSize-1] == 0x81))||
               ((pTranscv_Info->sRecvData[recvBufferActualSize-2] == 0x6F) &&
               (pTranscv_Info->sRecvData[recvBufferActualSize-1] == 0x00)))
        {
            /*TODO:
             2. Reset */
            JcopOsDwnld::doeSE_Reset();
            status = NFA_STATUS_OK;
            ALOGD("%s: Trigger APDU Transceive status = 0x%X", fn, status);
        }
        else
        {
            /* status {90, 00} */
            status = NFA_STATUS_OK;
        }
    }
    ALOGD("%s: exit; status = 0x%X", fn, status);
    return status;
}

tNFA_STATUS JcopOsDwnld::GetInfo(JcopOs_ImageInfo_t* pVersionInfo, tNFA_STATUS status, JcopOs_TranscieveInfo_t* pTranscv_Info)
{
    static const char fn [] = "JcopOsDwnld::GetInfo";
    //tNFA_STATUS status = NFA_STATUS_FAILED;
    bool stat = false;
    INT32 recvBufferActualSize = 0;
    SecureElement &se = SecureElement::getInstance();

    ALOGD("%s: enter;", fn);

    if(pTranscv_Info == NULL ||
       pVersionInfo == NULL)
    {
        ALOGD("%s: Invalid parameter", fn);
        status = NFA_STATUS_FAILED;
    }
    else
    {
        memset(pTranscv_Info->sSendData, 0, 1024);
        pTranscv_Info->timeout = gTransceiveTimeout;
        pTranscv_Info->sSendlength = (UINT32)sizeof(GetInfo_APDU);
        pTranscv_Info->sRecvlength = 1024;//(UINT32)sizeof(UINT32);
        memcpy(pTranscv_Info->sSendData, GetInfo_APDU, pTranscv_Info->sSendlength);

        ALOGD("%s: Calling Secure Element Transceive", fn);
        stat = se.transceive (pTranscv_Info->sSendData,
                                pTranscv_Info->sSendlength,
                                pTranscv_Info->sRecvData,
                                pTranscv_Info->sRecvlength,
                                recvBufferActualSize,
                                pTranscv_Info->timeout);
        if (stat != true)
        {
            status = NFA_STATUS_FAILED;
            ALOGE("%s: SE transceive failed status = 0x%X", fn, status);//Stop JcopOs Update
        }
        else if((pTranscv_Info->sRecvData[recvBufferActualSize-2] == 0x90) &&
                (pTranscv_Info->sRecvData[recvBufferActualSize-1] == 0x00))
        {
            /*TODO:
             2. Reset */
            status = NFA_STATUS_OK;
            ALOGD("%s: GetInfo Transceive status = 0x%X", fn, status);
        }
    }
    ALOGD("%s: exit; status = 0x%X", fn, status);
    return status;
}

tNFA_STATUS JcopOsDwnld::load_JcopOS_image(JcopOs_ImageInfo_t *Os_info, tNFA_STATUS status, JcopOs_TranscieveInfo_t *pTranscv_Info)
{
    static const char fn [] = "JcopOsDwnld::load_JcopOS_image";
    static const char path [] = "/data/JcopOs_Update.apdu";
    bool stat = false;
    int wResult, size =0;
    INT32 wIndex,wCount=0;
    INT32 wLen;

    INT32 recvBufferActualSize = 0;
    SecureElement &se = SecureElement::getInstance();
    ALOGD("%s: enter", fn);
    if(Os_info == NULL ||
       pTranscv_Info == NULL)
    {
        ALOGE("%s: invalid parameter", fn);
        return status;
    }
    memcpy(Os_info->fls_path,path, sizeof(path));
    Os_info->fp = fopen(Os_info->fls_path, "r+");

    if (Os_info->fp == NULL) {
        ALOGE("Error opening OS image file <%s> for reading: %s",
                    Os_info->fls_path, strerror(errno));
        return status;
    }
    wResult = fseek(Os_info->fp, 0L, SEEK_END);
    if (wResult) {
        ALOGE("Error seeking end OS image file %s", strerror(errno));
        goto exit;
    }
    Os_info->fls_size = ftell(Os_info->fp);
    if (Os_info->fls_size < 0) {
        ALOGE("Error ftelling file %s", strerror(errno));
        goto exit;
    }
    wResult = fseek(Os_info->fp, 0L, SEEK_SET);
    if (wResult) {
        ALOGE("Error seeking start image file %s", strerror(errno));
        goto exit;
    }
    while(!feof(Os_info->fp))
    {
        ALOGE("%s; Start of line processing", fn);

        wIndex=0;
        wLen=0;
        wCount=0;
        memset(pTranscv_Info->sSendData,0x00,1024);
        pTranscv_Info->sSendlength=0;

        ALOGE("%s; wIndex = 0", fn);
        for(wCount =0; (wCount < 5 && !feof(Os_info->fp)); wCount++, wIndex++)
        {
            ALOGE("%s; Reading 1st 5bytes", fn);
            wResult = fscanf(Os_info->fp,"%2X",&pTranscv_Info->sSendData[wIndex]);
        }
        ALOGE("%s; Reading 1st 5bytes completed", fn);
        if(wResult != 0)
        {
            ALOGE("%s; Read 5byes success", fn);
            wLen = pTranscv_Info->sSendData[4];
            ALOGE("%s; Read 5byes success =%d", fn,wLen);
            if(wLen == 0x00)
            {
                ALOGE("%s: Extended APDU", fn);
                wResult = fscanf(Os_info->fp,"%2X",&pTranscv_Info->sSendData[wIndex++]);
                wResult = fscanf(Os_info->fp,"%2X",&pTranscv_Info->sSendData[wIndex++]);
                wLen = ((pTranscv_Info->sSendData[5] << 8) | (pTranscv_Info->sSendData[6]));
            }
            for(wCount =0; (wCount < wLen && !feof(Os_info->fp)); wCount++, wIndex++)
            {
                wResult = fscanf(Os_info->fp,"%2X",&pTranscv_Info->sSendData[wIndex]);
            }
            ALOGE("%s; Reading line of file len = %d, wIndex = %d", fn,wLen,wIndex);
            ALOGE("%s; Reading line of file = %x%x%x%x", fn,pTranscv_Info->sSendData[wIndex-wLen],
                    pTranscv_Info->sSendData[wIndex-wLen+1],
                    pTranscv_Info->sSendData[wIndex-wLen+2],
                    pTranscv_Info->sSendData[wIndex-wLen+3]);

        }
        else
        {
            ALOGE("%s: JcopOs image Read failed", fn);
            goto exit;
        }


        pTranscv_Info->sSendlength = wIndex;
        ALOGE("%s: start transceive for length %d", fn, pTranscv_Info->sSendlength);

        stat = se.transceive (pTranscv_Info->sSendData,
                                pTranscv_Info->sSendlength,
                                pTranscv_Info->sRecvData,
                                pTranscv_Info->sRecvlength,
                                recvBufferActualSize,
                                pTranscv_Info->timeout);
        if(stat != true)
        {
            ALOGE("%s: Transceive failed; status=0x%X", fn, stat);
            goto exit;
        }
        else if(recvBufferActualSize != 0 &&
                pTranscv_Info->sRecvData[recvBufferActualSize-2] == 0x90 &&
                pTranscv_Info->sRecvData[recvBufferActualSize-1] == 0x00)
        {
            ALOGE("%s: END transceive for length %d", fn, pTranscv_Info->sSendlength);
        }
        else if(pTranscv_Info->sRecvData[recvBufferActualSize-2] == 0x6F &&
                pTranscv_Info->sRecvData[recvBufferActualSize-1] == 0x00)
        {
            ALOGE("%s: JcopOs is already upto date-No update required exiting", fn);
            status = 0x01;
            goto exit;
        }
        else
        {
            ALOGE("%s: Invalid response", fn);
            goto exit;
        }

        ALOGE("%s: Going for next line", fn);

    }

    ALOGE("%s: End of Load os image", fn);
    wResult = fclose(Os_info->fp);
    return (stat == true)?NFA_STATUS_OK:NFA_STATUS_FAILED;

exit:
    status = (status == 0x01)?0x01: NFA_STATUS_FAILED;
    ALOGE("%s close fp and exit; status= 0x%X", fn,status);
    wResult = fclose(Os_info->fp);
    return status;
}

void JcopOsDwnld::doeSE_Reset(void)
{
    static const char fn [] = "JcopOsDwnld::doeSE_Reset";
    SecureElement &se = SecureElement::getInstance();
    ALOGD("%s: enter:", fn);

    ALOGD("1st mode set calling");
    se.SecEle_Modeset(0x00);
    usleep(100 * 1000);
    ALOGD("1st mode set called");
    ALOGD("2nd mode set calling");

    se.SecEle_Modeset(0x01);
    ALOGD("2nd mode set called");

    usleep(2000 * 1000);
}

bool JcopOsDwnld::open()
{
    bool stat = true;
    SecureElement &se = SecureElement::getInstance();

    ALOGE("JCOPOS_Dwnld: Sec Element open Enter");
    //if controller is not routing AND there is no pipe connected,
    //then turn on the sec elem
    if (! se.isBusy())
        stat = se.activate(0x01);

    if (stat)
    {
        //establish a pipe to sec elem
        stat = se.connectEE();
        if (!stat)
            se.deactivate (0);
    }

    return stat;
}

bool JcopOsDwnld::close()
{
    bool stat = false;
    SecureElement &se = SecureElement::getInstance();

    //stat = se.routeToDefault();
    if(stat == true)
    {
        ALOGE("JCOPOS_Dwnld: Route Sec Element to default is success");
    }
    stat = SecureElement::getInstance().disconnectEE (0x01);

    //if controller is not routing AND there is no pipe connected,
    //then turn off the sec elem
    if (! SecureElement::getInstance().isBusy())
        SecureElement::getInstance().deactivate (0x01);

     return stat;
}
#if 0
void *JcopOsDwnld::GetMemory(UINT32 size)
{
    void *pMem;
    static const char fn [] = "JcopOsDwnld::GetMemory";
    pMem = (void *)malloc(size);

    if(pMem != NULL)
    {
        memset(pMem, 0, size);
    }
    else
    {
        ALOGD("%s: memory allocation failed", fn);
    }
    return pMem;
}

void JcopOsDwnld::FreeMemory(void *pMem)
{
    if(pMem != NULL)
    {
        free(pMem);
        pMem = NULL;
    }
}

#endif

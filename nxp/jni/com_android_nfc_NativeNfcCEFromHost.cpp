/*

 */

#include <semaphore.h>
#include <errno.h>

#include "com_android_nfc_1_3.h"
#include "phNfcHalTypes.h"

static phLibNfc_Handle handle;

extern uint8_t device_connected_flag;

/* TODO temporary */
static phLibNfc_sEmulationCfg_t pConfigInfo_TypeA;
static phLibNfc_sEmulationCfg_t pConfigInfo_TypeB;

static phLibNfc_Handle mCeFromHostHandle = 0;

namespace android
{

extern void nfc_jni_reset_timeout_values();

static void com_android_nfc_jni_EnableHostCE_callback(void *pContext,
                                                      NFCSTATUS status);
static void com_android_nfc_jni_DisableHostCE_callback(void *pContext,
                                                       NFCSTATUS status);

void set_CEFromHost_mHandle(phLibNfc_Handle mHandle)
{
    mCeFromHostHandle = mHandle;
}

/*
 * Callbacks
 */

static void com_android_nfc_jni_EnableHostCE_callback(void *pContext,
                                                      NFCSTATUS status)
{
    struct nfc_jni_callback_data * pContextData =
            (struct nfc_jni_callback_data*) pContext;

    LOG_CALLBACK("com_android_nfc_jni_EnableHostCE_callback", status);

    pContextData->status = status;
    sem_post(&pContextData->sem);
}

static void com_android_nfc_jni_DisableHostCE_callback(void *pContext,
                                                       NFCSTATUS status)
{
    struct nfc_jni_callback_data * pContextData =
            (struct nfc_jni_callback_data*) pContext;

    LOG_CALLBACK("com_android_nfc_jni_DisableHostCE_callback", status);

    pContextData->status = status;
    sem_post(&pContextData->sem);
}

static void nfc_jni_cefh_receive_callback(void* pContext,
                                          phNfc_sData_t* psReceiveBuffer,
                                          NFCSTATUS status)
{
    struct nfc_jni_callback_data * pCallbackData =
            (struct nfc_jni_callback_data *) pContext;
    LOG_CALLBACK("nfc_jni_cefh_receive_callback", status);

    if (status == NFCSTATUS_SUCCESS)
    {
        TRACE("psReceiveBuffer length : %", psReceiveBuffer->length);
        /* Store receive information buffer
         * +4 for the length byte
         * Allocate Working buffer length */
        pCallbackData->pContext = (uint8_t*) malloc(
                psReceiveBuffer->length + 4);
        memcpy(pCallbackData->pContext, psReceiveBuffer,
                psReceiveBuffer->length + 4);
    }

    /* Report the callback status and wake up the caller */
    pCallbackData->status = status;
    sem_post(&pCallbackData->sem);
}

static void nfc_jni_cefh_send_callback(void *pContext, NFCSTATUS status)
{
    struct nfc_jni_callback_data * pCallbackData =
            (struct nfc_jni_callback_data *) pContext;
    LOG_CALLBACK("nfc_jni_cefh_send_callback", status);

    /* Report the callback status and wake up the caller */
    pCallbackData->status = status;
    sem_post(&pCallbackData->sem);
}

/* Functions */

static NFCSTATUS nfc_jni_start_hostcardemulationTypeA(
        JNIEnv *e, struct nfc_jni_native_data *nat, jbyte sak, jbyteArray atqa,
        jbyteArray appData)
{

    ALOGD("nfc_jni_start_hostcardemulationTypeA - start");

    NFCSTATUS ret = NFCSTATUS_FAILED;
    struct nfc_jni_callback_data cb_data;
    uint8_t *atqabuf;
    uint8_t uidbuf[] =
        { 0x04, 0x7c, 0x72, 0xc9, 0x92, 0x26, 0x80 };
    uint8_t uidlength = 0x07;

    /* Create the local semaphore */
    if (!nfc_cb_data_init(&cb_data, NULL))
    {
        goto clean_and_return;
    }

    /* Setting TypeA info */
    pConfigInfo_TypeA.hostType = phHal_eHostController;
    pConfigInfo_TypeA.emuType = NFC_HOST_CE_A_EMULATION;
    pConfigInfo_TypeA.config.hostEmuCfg_A.enableEmulation = TRUE;
    //pConfigInfo.config.hostEmuCfg_A.hostEmuCfgInfo.UidLength = e->GetArrayLength(uid);
    //uidbuf = (uint8_t *)e->GetByteArrayElements(uid, NULL);
    pConfigInfo_TypeA.config.hostEmuCfg_A.hostEmuCfgInfo.UidLength = uidlength;
    memcpy(&pConfigInfo_TypeA.config.hostEmuCfg_A.hostEmuCfgInfo.Uid, uidbuf,
            pConfigInfo_TypeA.config.hostEmuCfg_A.hostEmuCfgInfo.UidLength);
    pConfigInfo_TypeA.config.hostEmuCfg_A.hostEmuCfgInfo.Sak = 0x20;
    pConfigInfo_TypeA.config.hostEmuCfg_A.hostEmuCfgInfo.Fwi_Sfgt = 0x88;
    atqabuf = (uint8_t *) e->GetByteArrayElements(atqa, NULL);
    memcpy(&pConfigInfo_TypeA.config.hostEmuCfg_A.hostEmuCfgInfo.AtqA, atqabuf,
            PHHAL_ATQA_LENGTH);

    //CONCURRENCY_LOCK();
    REENTRANCE_LOCK();
    #ifdef  HOST_EMULATION
     ret = phLibNfc_Mgt_SetCE_ConfigParams(&pConfigInfo_TypeA,
            com_android_nfc_jni_EnableHostCE_callback, (void *) &cb_data);
    #else
     ret= NFCSTATUS_FEATURE_NOT_SUPPORTED;
    #endif
    REENTRANCE_UNLOCK();

    if (ret != NFCSTATUS_PENDING)
    {
        ALOGE("phLibNfc_Mgt_SetCE_ConfigParams() returned 0x%04x[%s]", ret,
                nfc_jni_get_status_name(ret));
        goto clean_and_return;
    }
    TRACE(
            "phLibNfc_Mgt_SetCE_ConfigParams() returned 0x%04x[%s]", ret, nfc_jni_get_status_name(ret));

    /* Wait for callback response */
    if (sem_wait(&cb_data.sem))
    {
        ALOGE("SetCECofig semaphore error");
        goto clean_and_return;
    }

    ret = cb_data.status;

    ALOGD("nfc_jni_start_hostcardemulationTypeA - end");
    clean_and_return: nfc_cb_data_deinit(&cb_data);
    return ret;

}

static NFCSTATUS nfc_jni_start_hostcardemulationTypeB(
        JNIEnv *e, struct nfc_jni_native_data *nat, jbyteArray atqb,
        jbyteArray hilayerresp, jint afi)
{
    ALOGD("nfc_jni_start_hostcardemulationTypeB - start");

    NFCSTATUS ret = NFCSTATUS_FAILED;
    struct nfc_jni_callback_data cb_data;
    uint8_t *atqbbuf, *hilayerrespbuf;

    /* Create the local semaphore */
    if (!nfc_cb_data_init(&cb_data, NULL))
    {
        goto clean_and_return;
    }

    /* Setting TypeB info */
    pConfigInfo_TypeB.hostType = phHal_eHostController;
    pConfigInfo_TypeB.emuType = NFC_HOST_CE_B_EMULATION;
    pConfigInfo_TypeB.config.hostEmuCfg_B.enableEmulation = TRUE;
    //pupibuf = (uint8_t *)e->GetByteArrayElements(pupi, NULL);
    //memcpy(&pConfigInfo.config.hostEmuCfg_B.hostEmuCfgInfo.AtqB.AtqResInfo.Pupi,nTypeB_Pupi,PHHAL_PUPI_LENGTH);
    atqbbuf = (uint8_t *) e->GetByteArrayElements(atqb, NULL);
    memcpy(&pConfigInfo_TypeB.config.hostEmuCfg_B.hostEmuCfgInfo.AtqB.AtqRes,
            atqbbuf, PHHAL_ATQB_LENGTH);
    hilayerrespbuf = (uint8_t *) e->GetByteArrayElements(hilayerresp, NULL);
    memcpy(&pConfigInfo_TypeB.config.hostEmuCfg_B.hostEmuCfgInfo.HiLayerResp,
            hilayerrespbuf, PHHAL_MAX_ATR_LENGTH);
    pConfigInfo_TypeB.config.hostEmuCfg_B.hostEmuCfgInfo.HiLayerRespLength =
            e->GetArrayLength(hilayerresp);
    pConfigInfo_TypeB.config.hostEmuCfg_B.hostEmuCfgInfo.AtqB.AtqResInfo.ProtInfo[2] =
            0xE4;
    pConfigInfo_TypeB.config.hostEmuCfg_B.hostEmuCfgInfo.Afi = 0x00;

    //CONCURRENCY_LOCK();
    REENTRANCE_LOCK();
    #ifdef  HOST_EMULATION
    ret = phLibNfc_Mgt_SetCE_ConfigParams(&pConfigInfo_TypeB,
            com_android_nfc_jni_EnableHostCE_callback, (void *) &cb_data);
    #else
    ret= NFCSTATUS_FEATURE_NOT_SUPPORTED;
    #endif
    REENTRANCE_UNLOCK();

    if (ret != NFCSTATUS_PENDING)
    {
        ALOGE("phLibNfc_Mgt_SetCE_ConfigParams() returned 0x%04x[%s]", ret,
                nfc_jni_get_status_name(ret));
        goto clean_and_return;
    }
    TRACE(
            "phLibNfc_Mgt_SetCE_ConfigParams() returned 0x%04x[%s]", ret, nfc_jni_get_status_name(ret));

    /* Wait for callback response */
    if (sem_wait(&cb_data.sem))
    {
        ALOGE("SetCECofig semaphore error");
        goto clean_and_return;
    }

    ret = cb_data.status;
    ALOGD("nfc_jni_start_hostcardemulationTypeB - end");
    clean_and_return: nfc_cb_data_deinit(&cb_data);
    return ret;

}

static NFCSTATUS nfc_jni_stop_hostcardemulation(struct nfc_jni_native_data *nat)
{
    ALOGD("nfc_jni_stop_hostcardemulation - start");
    NFCSTATUS ret;
    struct nfc_jni_callback_data cb_data;

    /* Create the local semaphore */
    if (!nfc_cb_data_init(&cb_data, NULL))
    {
        goto clean_and_return;
    }
#if 0
    REENTRANCE_LOCK();
    ret = phLibNfc_CardEmulation_NtfUnregister();
    REENTRANCE_UNLOCK();
    if(ret != NFCSTATUS_SUCCESS)
    {
        ALOGD("pphLibNfc_CardEmulation_NtfUnregister returned 0x%02x",ret);
        goto clean_and_return;
    }
#endif
   ALOGD("nfc_jni_stop_hostcardemulation - #1");
   if ( TRUE ==  pConfigInfo_TypeA.config.hostEmuCfg_A.enableEmulation )
   {
        pConfigInfo_TypeA.config.hostEmuCfg_A.enableEmulation = FALSE;
        ALOGD("nfc_jni_stop_hostcardemulation - #2");
        //CONCURRENCY_LOCK();
        REENTRANCE_LOCK();
        #ifdef  HOST_EMULATION
        ret = phLibNfc_Mgt_SetCE_ConfigParams(&pConfigInfo_TypeA,
                com_android_nfc_jni_DisableHostCE_callback, (void *) &cb_data);
        #else
        ret=NFCSTATUS_FEATURE_NOT_SUPPORTED;
        #endif
        REENTRANCE_UNLOCK();

        if (ret != NFCSTATUS_PENDING)
        {
            ALOGE("phLibNfc_Mgt_SetCE_ConfigParams() returned 0x%04x[%s]", ret,
                    nfc_jni_get_status_name(ret));
            goto clean_and_return;
        }
        TRACE(
                "phLibNfc_Mgt_SetCE_ConfigParams() returned 0x%04x[%s]", ret, nfc_jni_get_status_name(ret));

        /* Wait for callback response */
        if (sem_wait(&cb_data.sem))
        {
            ALOGE("SetCECofig semaphore error");
            goto clean_and_return;
        }
    }

   if ( TRUE == pConfigInfo_TypeB.config.hostEmuCfg_B.enableEmulation )
   {
        pConfigInfo_TypeB.config.hostEmuCfg_B.enableEmulation = FALSE;
        ALOGE("nfc_jni_stop_hostcardemulation - #3");
        //CONCURRENCY_LOCK();
        REENTRANCE_LOCK();

        #ifdef  HOST_EMULATION
        ret = phLibNfc_Mgt_SetCE_ConfigParams(&pConfigInfo_TypeB,
                com_android_nfc_jni_DisableHostCE_callback, (void *) &cb_data);
        #else
        ret=NFCSTATUS_FEATURE_NOT_SUPPORTED;
        #endif
        REENTRANCE_UNLOCK();

        if (ret != NFCSTATUS_PENDING)
        {
            ALOGE("phLibNfc_Mgt_SetCE_ConfigParams() returned 0x%04x[%s]", ret,
                    nfc_jni_get_status_name(ret));
            goto clean_and_return;
        }
        TRACE(
                "phLibNfc_Mgt_SetCE_ConfigParams() returned 0x%04x[%s]", ret, nfc_jni_get_status_name(ret));

        /* Wait for callback response */
        if (sem_wait(&cb_data.sem))
        {
            ALOGE("SetCECofig semaphore error");
            goto clean_and_return;
        }

    }

    ALOGE("nfc_jni_stop_hostcardemulation - end");
    ret = cb_data.status;

    clean_and_return: nfc_cb_data_deinit(&cb_data);
    return ret;

}

static jboolean com_android_nfc_NativeNfcCEFromHost_dosetTypeA(
        JNIEnv *e, jobject o, jbyte sak, jbyteArray atqa, jbyteArray appData)
{

    struct nfc_jni_native_data *nat;
    bool result = true;
    NFCSTATUS status = NFCSTATUS_SUCCESS;

    CONCURRENCY_LOCK();
    //nat = nfc_jni_get_nat(e, o);
    status = nfc_jni_start_hostcardemulationTypeA(e, nat, sak, atqa, appData);
    CONCURRENCY_UNLOCK();

    if (status != NFCSTATUS_SUCCESS)
    {
        result = false;
    }

    return result;

}

static jboolean com_android_nfc_NativeNfcCEFromHost_dosetTypeB(
        JNIEnv *e, jobject o, jbyteArray atqb, jbyteArray hilayerresp, jint afi)
{

    struct nfc_jni_native_data *nat;
    bool result = true;
    NFCSTATUS status = NFCSTATUS_SUCCESS;

    CONCURRENCY_LOCK();
    //nat = nfc_jni_get_nat(e, o);
    status = nfc_jni_start_hostcardemulationTypeB(e, nat, atqb, hilayerresp,
            afi);
    CONCURRENCY_UNLOCK();

    if (status != NFCSTATUS_SUCCESS)
    {
        result = false;
    }

    return result;
}

static void com_android_nfc_NativeNfcCEFromHost_doresetType(JNIEnv *e,
                                                            jobject o)
{

    struct nfc_jni_native_data *nat;

    CONCURRENCY_LOCK();

    //nat = nfc_jni_get_nat(e, o);
    nfc_jni_stop_hostcardemulation(nat);

    CONCURRENCY_UNLOCK();
}

static jboolean com_android_nfc_NativeNfcCEFromHost_dosend(JNIEnv *e, jobject o,
                                                           jbyteArray data)
{
    NFCSTATUS status;
    phLibNfc_Handle hRemoteDevice = 0;
    phNfc_sData_t sSendBuffer =
        { NULL, 0 };
    jboolean result = JNI_FALSE;
    struct nfc_jni_callback_data cb_data;

    TRACE("com_android_nfc_NativeNfcCEFH_doSend()");

    CONCURRENCY_LOCK();

    /* Retrieve handles */
    //hRemoteDevice = nfc_jni_get_connected_handle(e, o);
    /* Create the local semaphore */
    if (!nfc_cb_data_init(&cb_data, NULL))
    {
        goto clean_and_return;
    }

    sSendBuffer.buffer = (uint8_t*) e->GetByteArrayElements(data, NULL);
    sSendBuffer.length = (uint32_t) e->GetArrayLength(data);

    TRACE("phLibNfc_RemoteDev_Send()");
    REENTRANCE_LOCK();
    #ifdef  HOST_EMULATION
    status = phLibNfc_RemoteDev_Send(mCeFromHostHandle, &sSendBuffer,
            nfc_jni_cefh_send_callback, (void *) &cb_data);
    #else
     status=NFCSTATUS_FEATURE_NOT_SUPPORTED;
    #endif
    REENTRANCE_UNLOCK();

    if (status != NFCSTATUS_PENDING)
    {
        ALOGE("phLibNfc_RemoteDev_Send() returned 0x%04x[%s]", status,
                nfc_jni_get_status_name(status));
        goto clean_and_return;
    }
    TRACE(
            "phLibNfc_RemoteDev_Send() returned 0x%04x[%s]", status, nfc_jni_get_status_name(status));

    /* Wait for callback response */
    if (sem_wait(&cb_data.sem))
    {
        ALOGE("Failed to wait for semaphore (errno=0x%08x)", errno);
        goto clean_and_return;
    }

    if (cb_data.status == NFCSTATUS_SUCCESS)
    {
        result = JNI_TRUE;
    }

    clean_and_return: if (sSendBuffer.buffer != NULL)
    {
        e->ReleaseByteArrayElements(data, (jbyte*) sSendBuffer.buffer, 0);
    }
    nfc_cb_data_deinit(&cb_data);
    CONCURRENCY_UNLOCK();
    return result;
}

static jbyteArray com_android_nfc_NativeNfcCEFromHost_doreceive(JNIEnv *e,
                                                                jobject o)
{
    phLibNfc_Handle handle = 0;
    NFCSTATUS status;
    uint8_t i;
    jbyteArray apdu_res = NULL;
    jint apdu_res_length;
    struct nfc_jni_callback_data cb_data;
    phNfc_sData_t sReceiveBuffer =
        { NULL, 0 };

    TRACE("com_android_nfc_NativeNfcCEFH_doreceive()");

    CONCURRENCY_LOCK();

    /* Create the local semaphore */
    if (!nfc_cb_data_init(&cb_data, NULL))
    {
        goto clean_and_return;
    }

    //handle = nfc_jni_get_connected_handle(e, o);

    TRACE("phLibNfc_RemoteDev_Receive()");
    REENTRANCE_LOCK();
    #ifdef  HOST_EMULATION
    status = phLibNfc_RemoteDev_Receive(mCeFromHostHandle,
            nfc_jni_cefh_receive_callback, (void *) &cb_data);
    #else
    status=NFCSTATUS_FEATURE_NOT_SUPPORTED;
    #endif
    REENTRANCE_UNLOCK();

    if (status != NFCSTATUS_PENDING)
    {
        ALOGE("phLibNfc_RemoteDev_Receive() returned 0x%04x[%s]", status,
                nfc_jni_get_status_name(status));
        goto clean_and_return;
    }
    TRACE(
            "phLibNfc_RemoteDev_Receive() returned 0x%04x[%s]", status, nfc_jni_get_status_name(status));

    /* Wait for callback response */
    if (sem_wait(&cb_data.sem))
    {
        ALOGE("Failed to wait for semaphore (errno=0x%08x)", errno);
        goto clean_and_return;
    }

    if (cb_data.status == NFCSTATUS_SUCCESS)
    {
        apdu_res_length = ((phNfc_sData_t *) cb_data.pContext)->length;
        apdu_res = e->NewByteArray(apdu_res_length);
        for (i = 0; i < apdu_res_length; i++)
        {
            jbyte a = (jbyte)((phNfc_sData_t *) cb_data.pContext)->buffer[i];
            e->SetByteArrayRegion(apdu_res, i, 1, &a);
        }
    }

    clean_and_return: nfc_cb_data_deinit(&cb_data);
    CONCURRENCY_UNLOCK();
    return apdu_res;
}

/*
 * JNI registration.
 */
static JNINativeMethod gMethods[] =
{
    { "dosetNfcCEFromHostTypeA", "(B[B[B)Z",
        (void *) com_android_nfc_NativeNfcCEFromHost_dosetTypeA },
    { "dosetNfcCEFromHostTypeB", "([B[BI)Z",
        (void *) com_android_nfc_NativeNfcCEFromHost_dosetTypeB },
    { "doresetNfcCEFromHostType", "()V",
        (void *) com_android_nfc_NativeNfcCEFromHost_doresetType },
    { "dosendNfcCEFromHost", "([B)Z",
        (void *) com_android_nfc_NativeNfcCEFromHost_dosend },
    { "doreceiveNfcCEFromHost", "()[B",
        (void *) com_android_nfc_NativeNfcCEFromHost_doreceive },
};

int register_com_android_nfc_NativeNfcCEFromHost(JNIEnv *e)
{
    return jniRegisterNativeMethods(e,
            "com/android/nfc/dhimpl/NativeNfcCEFromHost", gMethods,
            NELEM(gMethods));
}

} // namespace android

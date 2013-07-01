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
#include "JavaClassConstants.h"

extern "C"
{
    #include "nfa_api.h"
    #include "nfa_ee_api.h"
    #include "ce_api.h"
}

uint8_t CEFromHost::CE_TYPE_A = 0;
uint8_t CEFromHost::CE_TYPE_B = 1;

CEFromHost CEFromHost::sCefh;

/*******************************************************************************
**
** Function:        CEFromHost
**
** Description:     Initialize member variables.
**
** Returns:         None
**
*******************************************************************************/
CEFromHost::CEFromHost ()
:   mTechMask(0x00),
    mIsInit(false),
    mIsLinkActive(false),
    mIsEnabledA(false),
    mIsEnabledB(false),
    mNativeData (NULL),
    mAidHandle(0x00)
{

}

/*******************************************************************************
**
** Function:        ~CEFromHost
**
** Description:     Release all resources.
**
** Returns:         None
**
*******************************************************************************/
CEFromHost::~CEFromHost ()
{
}

/*******************************************************************************
**
** Function:        getInstance
**
** Description:     Get the CEFromHost singleton object.
**
** Returns:         CEFromHost object.
**
*******************************************************************************/
CEFromHost& CEFromHost::getInstance()
{
    return sCefh;
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
bool CEFromHost::initialize (nfc_jni_native_data* native)
{
    static const char fn [] = "CEFromHost::initialize";

    ALOGD ("%s: enter", fn);

    mNativeData     = native;

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
void CEFromHost::finalize ()
{
    static const char fn [] = "CEFromHost::finalize";
    ALOGD ("%s: enter", fn);
    mNativeData   = NULL;
    mIsInit       = false;

    ALOGD ("%s: exit", fn);
}

/*******************************************************************************
**
** Function:        notifyRfFieldEvent
**
** Description:     Notify the NFC service about RF field events from the stack.
**                  isActive: Whether any secure element is activated.
**
** Returns:         None
**
*******************************************************************************/
void CEFromHost::notifyCEFHLinkState (bool isActive)
{
    static const char fn [] = "CEFromHost::notifyCEFHLinkState";
    JNIEnv *e = NULL;

    ALOGD ("%s: enter; is active=%u", fn, isActive);
    mNativeData->vm->AttachCurrentThread (&e, NULL);

    if (e == NULL)
    {
        ALOGE ("%s: jni env is null", fn);
        return;
    }

    mMutex.lock();
    if (isActive) {
        mIsLinkActive = true;
        e->CallVoidMethod (mNativeData->manager, android::gCachedNfcManagerNotifyCEFromHostActivated);
    }
    else {
        mIsLinkActive = false;
        e->CallVoidMethod (mNativeData->manager, android::gCachedNfcManagerNotifyCEFromHostDeActivated);

        //This is break the waiting to receive APDU.
        mDataRecvEvent.notifyOne();

    }
    mMutex.unlock();

    if (e->ExceptionCheck())
    {
        e->ExceptionClear();
        ALOGE ("%s: fail notify", fn);
    }
    mNativeData->vm->DetachCurrentThread ();
    ALOGD ("%s: exit", fn);
}

/*******************************************************************************
**
** Function:        send
**
** Description:     Send raw data to peer device, if link is active.
**                  data: raw data to be sent to peer device.
**
** Returns:         true if data is sent, otherwise false..
**
*******************************************************************************/
bool CEFromHost::send(uint8_t data[],uint16_t len)
{
    static const char fn [] = "CEFromHost::send";
    tNFA_STATUS status = NFA_STATUS_FAILED;

    ALOGD ("%s: enter", fn);

    if(mIsInit == false)
    {
        ALOGD ("%s: can not send data,cefh is not initialized", fn);
        goto TheEnd;
    }

    if(mIsLinkActive)
    {
        status = NFA_SendRawFrame (data, len);
    }
    else
    {
        ALOGD ("%s: can not send data,cefh link is not active", fn);
    }

    ALOGD ("%s: exit", fn);

TheEnd:
    status = (status == NFA_STATUS_OK)?TRUE:FALSE;
    return status;
}


bool CEFromHost::receive(uint8_t** data, uint16_t* len)
{
    static const char fn [] = "CEFromHost::receive";
    bool status = false;

    if(NULL == data || NULL == len)
    {
        ALOGD ("%s: Invalid param", fn);
        goto TheEnd;
    }
    if(mIsInit == false)
    {
        ALOGD ("%s: cefh is not initialized", fn);
        goto TheEnd;
    }

    {
        if(mIsNewData == false)
        {
            SyncEventGuard guard (mDataRecvEvent);
            mDataRecvEvent.wait();
        }

        if(mIsLinkActive && mIsNewData == true)
        {
            *data = mRecvData;
            *len = mRecvLen;
            status = true;

            mDataMutex.lock();
            mIsNewData = false;
            mDataMutex.unlock();
        }
        else
        {
            ALOGD ("%s: can not send data,cefh link is not active", fn);
            status = false;
        }
    }
    TheEnd:
    return status;
}

bool CEFromHost::isEnabled()
{
    if(mIsEnabledA || mIsEnabledB)
        return true;
    else
        return false;
}

bool CEFromHost::isEnabled(uint8_t type)
{
    if(type == CE_TYPE_A)
    {
        return mIsEnabledA;
    }
    else if(type == CE_TYPE_B)
    {
        return mIsEnabledB;
    }

    return false;
}

void CEFromHost::setEnabled(uint8_t type,bool enabled)
{
    if(type == CE_TYPE_A)
    {
        mIsEnabledA = enabled;
    }
    else if(type == CE_TYPE_B)
    {
        mIsEnabledB = enabled;
    }
}

void CEFromHost::connectionEventHandler (UINT8 event, tNFA_CONN_EVT_DATA* eventData)
{
    static const char fn [] = "CEFromHost::connectionEventHandler";

    switch (event)
    {
    case NFA_CE_UICC_LISTEN_CONFIGURED_EVT:
    {
        SyncEventGuard guard (mUiccListenEvent);
        mUiccListenEvent.notifyOne ();
    }
    break;
    case NFA_CE_REGISTERED_EVT:
    {
        mAidHandle = eventData->ce_registered.handle;
        SyncEventGuard guard (mAidRegisterEvent);
        mAidRegisterEvent.notifyOne ();
    }
    break;

    case NFA_CE_DEREGISTERED_EVT:
        if(mAidHandle == eventData->ce_deregistered.handle)
        {
            SyncEventGuard guard (mAidDegisterEvent);
            mAidDegisterEvent.notifyOne ();
        }
        break;
    case NFA_CE_LOCAL_TAG_CONFIGURED_EVT:
    {
        SyncEventGuard guard (mSetConfigTagEvent);
        mSetConfigTagEvent.notifyOne ();
    }
    break;

    case NFA_CE_DATA_EVT:
    {
        mRecvData = eventData->ce_data.p_data;
        mRecvLen = eventData->ce_data.len;
        ALOGD ("%s: NFA_CE_DATA_EVT", fn);
        ALOGD ("%s: dataLen = %d", fn, mRecvLen);
//        mDataRecvEvent.wait();
        mDataMutex.lock();
        mIsNewData = true;
        mDataMutex.unlock();
        SyncEventGuard guard (mDataRecvEvent);
        mDataRecvEvent.notifyOne();

//        mDataRecvEvent.notifyOne();
    }
    break;
    }
}

bool CEFromHost::enableCEType(uint8_t tech)
{
    static const char fn [] = "CEFromHost::enableCEType";
    tNFA_STATUS status = NFA_STATUS_FAILED;

    if(mIsInit == false)
    {
        ALOGD ("%s: cefh is not initialized", fn);
        goto TheEnd;
    }

    {
        SyncEventGuard guard (mUiccListenEvent);

        mTechMask |= tech;

        status = NFA_CeConfigureUiccListenTech (NFA_EE_HANDLE_DH, mTechMask);
        if (status == NFA_STATUS_OK)
        {
            mUiccListenEvent.wait ();
            ALOGE ("Start  listen on host");
        }
        else
        {
            goto TheEnd;
            ALOGE ("Fail to start  listen on host");
        }
    }
    status = NFA_CeSetIsoDepListenTech( mTechMask);
    if (status == NFA_STATUS_OK) {
         ALOGD ("%s: Enabled tech=0x%0x Tech for CEFH", __FUNCTION__,mTechMask);
     } else {
         goto TheEnd;
         ALOGE ("%s: Failed to enable tech=0x%0x for CEFH", __FUNCTION__,mTechMask);
     }

TheEnd:
    status = (status == NFA_STATUS_OK)?TRUE:FALSE;
    return status;
}

bool CEFromHost::registerCEEventsListener(tNFA_CONN_CBACK *cback)
{
    static const char fn [] = "CEFromHost::registerCEEventsListener";
    tNFA_STATUS status = NFA_STATUS_FAILED;

    if(mIsInit == false)
    {
        ALOGD ("%s: cefh is not initialized", fn);
        goto TheEnd;
    }

    {
        SyncEventGuard guard (mAidRegisterEvent);

        status = NFA_CeRegisterAidOnDH (NULL, 0x00, cback);

        if (status == NFA_STATUS_OK)
        {
            mAidRegisterEvent.wait();
            ALOGD ("%s: CEFH events listener setup on DH for CEFH", __FUNCTION__);
        }
        else
        {
            goto TheEnd;
            ALOGE ("%s: Failed to setup CEFH events listener on DH for CEFH", __FUNCTION__);
        }
    }
TheEnd:
    status = (status == NFA_STATUS_OK)?TRUE:FALSE;
    return status;
}

bool CEFromHost::deRegisterCEEventsListener()
{
    static const char fn [] = "CEFromHost::deRegisterCEEventsListener";
    tNFA_STATUS status = NFA_STATUS_FAILED;

    if(mIsInit == false)
    {
        ALOGD ("%s: cefh is not initialized", fn);
        goto TheEnd;
    }

    {
        SyncEventGuard guard (mAidDegisterEvent);

        status = NFA_CeDeregisterAidOnDH (mAidHandle);

        if (status == NFA_STATUS_OK) {
            mAidDegisterEvent.wait();
            ALOGD ("%s: AID removed on DH for CEFH", __FUNCTION__);
        } else {
            goto TheEnd;
            ALOGE ("%s: Failed to remove AID on DH for CEFH", __FUNCTION__);
        }
    }
TheEnd:
    status = (status == NFA_STATUS_OK)?TRUE:FALSE;
    return status;
}

bool CEFromHost::configLocalTag(uint8_t* appdata)
{

    static const char fn [] = "CEFromHost::configLocalTag";
    tNFA_STATUS status = NFA_STATUS_FAILED;

    if(mIsInit == false)
    {
        ALOGD ("%s: cefh is not initialized", fn);
        goto TheEnd;
    }

    {
        SyncEventGuard guard (mSetConfigTagEvent);

        status = NFA_CeConfigureLocalTag(NFA_PROTOCOL_MASK_ISO_DEP, //protocol_mask
                appdata, //p_ndef_data
                0x04  ,//ndef_cur_size
                1024,//ndef_max_size
                false, //read_only
                0, //uid_len
                NULL//p_uid
        );

        if (status == NFA_STATUS_OK) {
            mSetConfigTagEvent.wait();
            ALOGD ("%s: Enabled TAG for CEFH", __FUNCTION__);
        } else {
            goto TheEnd;
            ALOGE ("%s: Failed to create TAG for CEFH", __FUNCTION__);
        }
    }


    TheEnd:
        status = (status == NFA_STATUS_OK)?TRUE:FALSE;
        return status;
}

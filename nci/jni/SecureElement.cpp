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
/*
 *  Communicate with secure elements that are attached to the NFC
 *  controller.
 */
#include <semaphore.h>
#include <errno.h>
#include <ScopedLocalRef.h>
#include "OverrideLog.h"
#include "SecureElement.h"
#include "config.h"
#include "PowerSwitch.h"
#include "HostAidRouter.h"
#include "JavaClassConstants.h"
#ifdef NXP_EXT
#include "CEFromHost.h"
#include "nfc_api.h"
#endif
/*****************************************************************************
**
** public variables
**
*****************************************************************************/
int gSEId = -1;     // secure element ID to use in connectEE(), -1 means not set
int gGatePipe = -1; // gate id or static pipe id to use in connectEE(), -1 means not set
bool gUseStaticPipe = false;    // if true, use gGatePipe as static pipe id.  if false, use as gate id

namespace android
{
    extern void startRfDiscovery (bool isStart);
    extern void setUiccIdleTimeout (bool enable);
#ifdef NXP_EXT
    extern SyncEvent sNfaSetConfigEvent;
#endif
}

//////////////////////////////////////////////
//////////////////////////////////////////////

#ifdef NXP_EXT

#define NFC_NUM_INTERFACE_MAP 2

static const tNCI_DISCOVER_MAPS nfc_interface_mapping_default[NFC_NUM_INTERFACE_MAP] =
{
    /* Protocols that use Frame Interface do not need to be included in the interface mapping */
    {
        NCI_PROTOCOL_ISO_DEP,
        NCI_INTERFACE_MODE_POLL_N_LISTEN,
        NCI_INTERFACE_ISO_DEP
    }
    ,
    {
        NCI_PROTOCOL_NFC_DEP,
        NCI_INTERFACE_MODE_POLL_N_LISTEN,
        NCI_INTERFACE_NFC_DEP
    }
};
static const tNCI_DISCOVER_MAPS nfc_interface_mapping_uicc[NFC_NUM_INTERFACE_MAP] =
{
    /* Protocols that use Frame Interface do not need to be included in the interface mapping */
    {
        NCI_PROTOCOL_ISO_DEP,
        NCI_INTERFACE_MODE_POLL,
        NCI_INTERFACE_UICC_DIRECT
    }
    ,
    {
        NCI_PROTOCOL_NFC_DEP,
        NCI_INTERFACE_MODE_POLL_N_LISTEN,
        NCI_INTERFACE_NFC_DEP
    }
};

static const tNCI_DISCOVER_MAPS nfc_interface_mapping_ese[NFC_NUM_INTERFACE_MAP] =
{
        /* Protocols that use Frame Interface do not need to be included in the interface mapping */
    {
        NCI_PROTOCOL_ISO_DEP,
        NCI_INTERFACE_MODE_POLL,
        NCI_INTERFACE_ESE_DIRECT
    }
    ,
    {
        NCI_PROTOCOL_NFC_DEP,
        NCI_INTERFACE_MODE_POLL_N_LISTEN,
        NCI_INTERFACE_NFC_DEP
    }
};
void SecureElement::discovery_map_cb (tNFC_DISCOVER_EVT event, tNFC_DISCOVER *p_data)
{
    SyncEventGuard guard (sSecElem.mDiscMapEvent);
//    ALOGD ("discovery_map_cb; status=%u", eventData->ee_register);
    sSecElem.mDiscMapEvent.notifyOne();
}

char bcm_nfc_location[]="/etc";
#endif
SecureElement SecureElement::sSecElem;
const char* SecureElement::APP_NAME = "nfc_jni";


/*******************************************************************************
**
** Function:        SecureElement
**
** Description:     Initialize member variables.
**
** Returns:         None
**
*******************************************************************************/
SecureElement::SecureElement ()
:   mActiveEeHandle (NFA_HANDLE_INVALID),
    mDestinationGate (4), //loopback gate
    mNfaHciHandle (NFA_HANDLE_INVALID),
    mNativeData (NULL),
    mIsInit (false),
    mActualNumEe (0),
    mNumEePresent(0),
    mbNewEE (true),   // by default we start w/thinking there are new EE
    mNewPipeId (0),
    mNewSourceGate (0),
    mActiveSeOverride(0),
    mCommandStatus (NFA_STATUS_OK),
    mIsPiping (false),
    mCurrentRouteSelection (NoRoute),
    mActualResponseSize(0),
    mUseOberthurWarmReset (false),
    mActivatedInListenMode (false),
    mOberthurWarmResetCommand (3),
    mRfFieldIsOn(false)
{
    memset (&mEeInfo, 0, sizeof(mEeInfo));
    memset (&mUiccInfo, 0, sizeof(mUiccInfo));
    memset (&mHciCfg, 0, sizeof(mHciCfg));
    memset (mResponseData, 0, sizeof(mResponseData));
    memset (mAidForEmptySelect, 0, sizeof(mAidForEmptySelect));
    memset (&mLastRfFieldToggle, 0, sizeof(mLastRfFieldToggle));
}


/*******************************************************************************
**
** Function:        ~SecureElement
**
** Description:     Release all resources.
**
** Returns:         None
**
*******************************************************************************/
SecureElement::~SecureElement ()
{
}


/*******************************************************************************
**
** Function:        getInstance
**
** Description:     Get the SecureElement singleton object.
**
** Returns:         SecureElement object.
**
*******************************************************************************/
SecureElement& SecureElement::getInstance()
{
    return sSecElem;
}


/*******************************************************************************
**
** Function:        setActiveSeOverride
**
** Description:     Specify which secure element to turn on.
**                  activeSeOverride: ID of secure element
**
** Returns:         None
**
*******************************************************************************/
void SecureElement::setActiveSeOverride(UINT8 activeSeOverride)
{
    ALOGD ("SecureElement::setActiveSeOverride, seid=0x%X", activeSeOverride);
    mActiveSeOverride = activeSeOverride;
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
bool SecureElement::initialize (nfc_jni_native_data* native)
{
    static const char fn [] = "SecureElement::initialize";
    tNFA_STATUS nfaStat;
    unsigned long num = 0;

    ALOGD ("%s: enter", fn);

    if (GetNumValue("NFA_HCI_DEFAULT_DEST_GATE", &num, sizeof(num)))
        mDestinationGate = num;
    ALOGD ("%s: Default destination gate: 0x%X", fn, mDestinationGate);

    // active SE, if not set active all SEs
    if (GetNumValue("ACTIVE_SE", &num, sizeof(num)))
        mActiveSeOverride = num;
    ALOGD ("%s: Active SE override: 0x%X", fn, mActiveSeOverride);

    if (GetNumValue("OBERTHUR_WARM_RESET_COMMAND", &num, sizeof(num)))
    {
        mUseOberthurWarmReset = true;
        mOberthurWarmResetCommand = (UINT8) num;
    }

    mActiveEeHandle = NFA_HANDLE_INVALID;
    mNfaHciHandle = NFA_HANDLE_INVALID;

    mNativeData     = native;
    mActualNumEe    = MAX_NUM_EE;
    mbNewEE         = true;
    mNewPipeId      = 0;
    mNewSourceGate  = 0;
    mRfFieldIsOn    = false;
    mActivatedInListenMode = false;
    mCurrentRouteSelection = NoRoute;
    memset (mEeInfo, 0, sizeof(mEeInfo));
    memset (&mUiccInfo, 0, sizeof(mUiccInfo));
    memset (&mHciCfg, 0, sizeof(mHciCfg));
    mUsedAids.clear ();
    memset(mAidForEmptySelect, 0, sizeof(mAidForEmptySelect));

    // Get Fresh EE info.
    if (! getEeInfo())
        return (false);

    {
        SyncEventGuard guard (mEeRegisterEvent);
        ALOGD ("%s: try ee register", fn);
        nfaStat = NFA_EeRegister (nfaEeCallback);
        if (nfaStat != NFA_STATUS_OK)
        {
            ALOGE ("%s: fail ee register; error=0x%X", fn, nfaStat);
            return (false);
        }
        mEeRegisterEvent.wait ();
    }

    // If the controller has an HCI Network, register for that
    for (size_t xx = 0; xx < mActualNumEe; xx++)
    {
#ifdef NXP_EXT
        if ((mEeInfo[xx].num_interface > 0) && (mEeInfo[xx].ee_handle != EE_HANDLE_0xF4 ))
#else
        if ((mEeInfo[xx].num_interface > 0) && (mEeInfo[xx].ee_interface[0] == NCI_NFCEE_INTERFACE_HCI_ACCESS) )
#endif
        {
            ALOGD ("%s: Found HCI network, try hci register", fn);
            SyncEventGuard guard (mHciRegisterEvent);

            nfaStat = NFA_HciRegister (const_cast<char*>(APP_NAME), nfaHciCallback, TRUE);
            if (nfaStat != NFA_STATUS_OK)
            {
                ALOGE ("%s: fail hci register; error=0x%X", fn, nfaStat);
                return (false);
            }
            mHciRegisterEvent.wait();
            break;
        }
    }

    mRouteDataSet.initialize ();
    mRouteDataSet.import (); //read XML file
    HostAidRouter::getInstance().initialize ();

    GetStrValue(NAME_AID_FOR_EMPTY_SELECT, (char*)&mAidForEmptySelect[0], sizeof(mAidForEmptySelect));

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
void SecureElement::finalize ()
{
    static const char fn [] = "SecureElement::finalize";
    ALOGD ("%s: enter", fn);

    NFA_EeDeregister (nfaEeCallback);

    if (mNfaHciHandle != NFA_HANDLE_INVALID)
        NFA_HciDeregister (const_cast<char*>(APP_NAME));

    mNfaHciHandle = NFA_HANDLE_INVALID;
    mNativeData   = NULL;
    mIsInit       = false;
    mActualNumEe  = 0;
    mNumEePresent = 0;
    mNewPipeId    = 0;
    mNewSourceGate = 0;
    mIsPiping = false;
    memset (mEeInfo, 0, sizeof(mEeInfo));
    memset (&mUiccInfo, 0, sizeof(mUiccInfo));

    ALOGD ("%s: exit", fn);
}


/*******************************************************************************
**
** Function:        getEeInfo
**
** Description:     Get latest information about execution environments from stack.
**
** Returns:         True if at least 1 EE is available.
**
*******************************************************************************/
bool SecureElement::getEeInfo()
{
    static const char fn [] = "SecureElement::getEeInfo";
    ALOGD ("%s: enter; mbNewEE=%d, mActualNumEe=%d", fn, mbNewEE, mActualNumEe);
    tNFA_STATUS nfaStat = NFA_STATUS_FAILED;

    // If mbNewEE is true then there is new EE info.
    if (mbNewEE)
    {
        mActualNumEe = MAX_NUM_EE;

        if ((nfaStat = NFA_EeGetInfo (&mActualNumEe, mEeInfo)) != NFA_STATUS_OK)
        {
            ALOGE ("%s: fail get info; error=0x%X", fn, nfaStat);
            mActualNumEe = 0;
        }
        else
        {
            mbNewEE = false;

            ALOGD ("%s: num EEs discovered: %u", fn, mActualNumEe);
            if (mActualNumEe != 0)
            {
                for (UINT8 xx = 0; xx < mActualNumEe; xx++)
                {
                    if ((mEeInfo[xx].num_interface != 0) && (mEeInfo[xx].ee_interface[0] != NCI_NFCEE_INTERFACE_HCI_ACCESS) )
                        mNumEePresent++;

                    ALOGD ("%s: EE[%u] Handle: 0x%04x  Status: %s  Num I/f: %u: (0x%02x, 0x%02x)  Num TLVs: %u",
                          fn, xx, mEeInfo[xx].ee_handle, eeStatusToString(mEeInfo[xx].ee_status), mEeInfo[xx].num_interface,
                          mEeInfo[xx].ee_interface[0], mEeInfo[xx].ee_interface[1], mEeInfo[xx].num_tlvs);

                    for (size_t yy = 0; yy < mEeInfo[xx].num_tlvs; yy++)
                    {
                        ALOGD ("%s: EE[%u] TLV[%u]  Tag: 0x%02x  Len: %u  Values[]: 0x%02x  0x%02x  0x%02x ...",
                              fn, xx, yy, mEeInfo[xx].ee_tlv[yy].tag, mEeInfo[xx].ee_tlv[yy].len, mEeInfo[xx].ee_tlv[yy].info[0],
                              mEeInfo[xx].ee_tlv[yy].info[1], mEeInfo[xx].ee_tlv[yy].info[2]);
                    }
                }
            }
        }
    }
    ALOGD ("%s: exit; mActualNumEe=%d, mNumEePresent=%d", fn, mActualNumEe,mNumEePresent);
    return (mActualNumEe != 0);
}


/*******************************************************************************
**
** Function         TimeDiff
**
** Description      Computes time difference in milliseconds.
**
** Returns          Time difference in milliseconds
**
*******************************************************************************/
static UINT32 TimeDiff(timespec start, timespec end)
{
    end.tv_sec -= start.tv_sec;
    end.tv_nsec -= start.tv_nsec;

    if (end.tv_nsec < 0) {
        end.tv_nsec += 10e8;
        end.tv_sec -=1;
    }

    return (end.tv_sec * 1000) + (end.tv_nsec / 10e5);
}

/*******************************************************************************
**
** Function:        isRfFieldOn
**
** Description:     Can be used to determine if the SE is in an RF field
**
** Returns:         True if the SE is activated in an RF field
**
*******************************************************************************/
bool SecureElement::isRfFieldOn() {
    AutoMutex mutex(mMutex);
    if (mRfFieldIsOn) {
        return true;
    }
    struct timespec now;
    int ret = clock_gettime(CLOCK_MONOTONIC, &now);
    if (ret == -1) {
        ALOGE("isRfFieldOn(): clock_gettime failed");
        return false;
    }
    if (TimeDiff(mLastRfFieldToggle, now) < 50) {
        // If it was less than 50ms ago that RF field
        // was turned off, still return ON.
        return true;
    } else {
        return false;
    }
}

/*******************************************************************************
**
** Function:        isActivatedInListenMode
**
** Description:     Can be used to determine if the SE is activated in listen mode
**
** Returns:         True if the SE is activated in listen mode
**
*******************************************************************************/
bool SecureElement::isActivatedInListenMode() {
    return mActivatedInListenMode;
}

/*******************************************************************************
**
** Function:        getListOfEeHandles
**
** Description:     Get the list of handles of all execution environments.
**                  e: Java Virtual Machine.
**
** Returns:         List of handles of all execution environments.
**
*******************************************************************************/
jintArray SecureElement::getListOfEeHandles (JNIEnv* e)
{
    static const char fn [] = "SecureElement::getListOfEeHandles";
    ALOGD ("%s: enter", fn);
    if (mNumEePresent == 0)
        return NULL;

    if (!mIsInit)
    {
        ALOGE ("%s: not init", fn);
        return (NULL);
    }

    // Get Fresh EE info.
    if (! getEeInfo())
        return (NULL);

    jintArray list = e->NewIntArray (mNumEePresent); //allocate array
    jint jj = 0;
    int cnt = 0;
    for (int ii = 0; ii < mActualNumEe && cnt < mNumEePresent; ii++)
    {
        ALOGD ("%s: %u = 0x%X", fn, ii, mEeInfo[ii].ee_handle);
        if ((mEeInfo[ii].num_interface == 0) || (mEeInfo[ii].ee_interface[0] == NCI_NFCEE_INTERFACE_HCI_ACCESS) )
        {
            continue;
        }

        jj = mEeInfo[ii].ee_handle & ~NFA_HANDLE_GROUP_EE;
#ifdef NXP_EXT
        ALOGD ("%s: Handle %u = 0x%X", fn, ii, jj);

        jj = getGenericEseId(jj);

        ALOGD ("%s: Generic id %u = 0x%X", fn, ii, jj);
#endif
        e->SetIntArrayRegion (list, cnt++, 1, &jj);
    }

    ALOGD("%s: exit", fn);
    return list;
}


/*******************************************************************************
**
** Function:        activate
**
** Description:     Turn on the secure element.
**                  seID: ID of secure element; 0xF3 or 0xF4.
**
** Returns:         True if ok.
**
*******************************************************************************/
bool SecureElement::activate (jint seID)
{
    static const char fn [] = "SecureElement::activate";
    tNFA_STATUS nfaStat = NFA_STATUS_FAILED;
    int numActivatedEe = 0;

    ALOGD ("%s: enter; seID=0x%X", fn, seID);
#ifdef NXP_EXT
    tNFA_HANDLE handle = getEseHandleFromGenericId(seID);

    ALOGD ("%s: handle=0x%X", fn, handle);
#endif
    if (!mIsInit)
    {
        ALOGE ("%s: not init", fn);
        return false;
    }
#ifndef NXP_EXT
    if (mActiveEeHandle != NFA_HANDLE_INVALID)
    {
        ALOGD ("%s: already active", fn);
        return true;
    }
#endif
    // Get Fresh EE info if needed.
    if (! getEeInfo())
    {
        ALOGE ("%s: no EE info", fn);
        return false;
    }

    UINT16 overrideEeHandle = 0;
    if (mActiveSeOverride) {
        overrideEeHandle = NFA_HANDLE_GROUP_EE | mActiveSeOverride;
    }
#ifdef NXP_EXT
    else
    {
        overrideEeHandle = handle;
    }
#endif
    if (mRfFieldIsOn) {
        ALOGE("%s: RF field indication still on, resetting", fn);
        mRfFieldIsOn = false;
    }

    ALOGD ("%s: override ee h=0x%X", fn, overrideEeHandle );
    //activate every discovered secure element
    for (int index=0; index < mActualNumEe; index++)
    {
        tNFA_EE_INFO& eeItem = mEeInfo[index];

        if ((eeItem.ee_handle == EE_HANDLE_0xF3) || (eeItem.ee_handle == EE_HANDLE_0xF4))
        {
            if (overrideEeHandle && (overrideEeHandle != eeItem.ee_handle) )
                continue;   // do not enable all SEs; only the override one

            if (eeItem.ee_status != NFC_NFCEE_STATUS_INACTIVE)
            {
                ALOGD ("%s: h=0x%X already activated", fn, eeItem.ee_handle);
                numActivatedEe++;
                continue;
            }

            {
                SyncEventGuard guard (mEeSetModeEvent);
                ALOGD ("%s: set EE mode activate; h=0x%X", fn, eeItem.ee_handle);
                if ((nfaStat = NFA_EeModeSet (eeItem.ee_handle, NFA_EE_MD_ACTIVATE)) == NFA_STATUS_OK)
                {
                    mEeSetModeEvent.wait (); //wait for NFA_EE_MODE_SET_EVT
                    if (eeItem.ee_status == NFC_NFCEE_STATUS_ACTIVE)
                        numActivatedEe++;
                }
                else
                    ALOGE ("%s: NFA_EeModeSet failed; error=0x%X", fn, nfaStat);
            }
        }
    } //for

    mActiveEeHandle = getDefaultEeHandle();
    if (mActiveEeHandle == NFA_HANDLE_INVALID)
        ALOGE ("%s: ee handle not found", fn);
    ALOGD ("%s: exit; active ee h=0x%X", fn, mActiveEeHandle);
    return mActiveEeHandle != NFA_HANDLE_INVALID;
}


/*******************************************************************************
**
** Function:        deactivate
**
** Description:     Turn off the secure element.
**                  seID: ID of secure element.
**
** Returns:         True if ok.
**
*******************************************************************************/
bool SecureElement::deactivate (jint seID)
{
    static const char fn [] = "SecureElement::deactivate";
    bool retval = false;

    ALOGD ("%s: enter; seID=0x%X, mActiveEeHandle=0x%X", fn, seID, mActiveEeHandle);
#ifdef NXP_EXT
    tNFA_STATUS nfaStat = NFA_STATUS_FAILED;
    tNFA_HANDLE handle = getEseHandleFromGenericId(seID);
    int numDeactivatedEe = 0;
    ALOGD ("%s: handle=0x%X", fn, handle);
#endif
    if (!mIsInit)
    {
        ALOGE ("%s: not init", fn);
        goto TheEnd;
    }

    //if the controller is routing to sec elems or piping,
    //then the secure element cannot be deactivated
    if ((mCurrentRouteSelection == SecElemRoute) || mIsPiping)
    {
        ALOGE ("%s: still busy", fn);
        goto TheEnd;
    }

#ifdef NXP_EXT
    if (seID == NFA_HANDLE_INVALID)
#else
    if (mActiveEeHandle == NFA_HANDLE_INVALID)
#endif
    {
        ALOGE ("%s: invalid EE handle", fn);
        goto TheEnd;
    }

    mActiveEeHandle = NFA_HANDLE_INVALID;

#ifdef NXP_EXT
    // Deactivate secure element
    for (int index=0; index < mActualNumEe; index++)
    {
        tNFA_EE_INFO& eeItem = mEeInfo[index];

        if ( eeItem.ee_handle == handle &&
                ((eeItem.ee_handle == EE_HANDLE_0xF3) || (eeItem.ee_handle == EE_HANDLE_0xF4)))
        {
            if (eeItem.ee_status == NFC_NFCEE_STATUS_INACTIVE)
            {
                ALOGD ("%s: h=0x%X already deactivated", fn, eeItem.ee_handle);
                break;
            }

            {
                SyncEventGuard guard (mEeSetModeEvent);
                ALOGD ("%s: set EE mode activate; h=0x%X", fn, eeItem.ee_handle);
                if ((nfaStat = NFA_EeModeSet (eeItem.ee_handle, NFA_EE_MD_DEACTIVATE)) == NFA_STATUS_OK)
                {
                    mEeSetModeEvent.wait (); // wait for NFA_EE_MODE_SET_EVT
                    if (eeItem.ee_status == NFC_NFCEE_STATUS_INACTIVE)
                    {
                        ALOGE ("%s: NFA_EeModeSet success; status=0x%X", fn, nfaStat);
                        retval = true;
                    }
                }
                else
                    ALOGE ("%s: NFA_EeModeSet failed; error=0x%X", fn, nfaStat);
            }
        }
    } //for
#endif

TheEnd:
    ALOGD ("%s: exit; ok=%u", fn, retval);
    return retval;
}


/*******************************************************************************
**
** Function:        notifyTransactionListenersOfAid
**
** Description:     Notify the NFC service about a transaction event from secure element.
**                  aid: Buffer contains application ID.
**                  aidLen: Length of application ID.
**
** Returns:         None
**
*******************************************************************************/
#ifdef NXP_EXT
void SecureElement::notifyTransactionListenersOfAid (const UINT8* aidBuffer, UINT8 aidBufferLen,
        const UINT8* dataBuffer, UINT8 dataBufferLen,UINT32 evtSrc)
#else
void SecureElement::notifyTransactionListenersOfAid (const UINT8* aidBuffer, UINT8 aidBufferLen)
#endif
{
    static const char fn [] = "SecureElement::notifyTransactionListenersOfAid";
    ALOGD ("%s: enter; aid len=%u", fn, aidBufferLen);

    if (aidBufferLen == 0) {
        return;
    }

    JNIEnv* e = NULL;
    ScopedAttach attach(mNativeData->vm, &e);
    if (e == NULL)
    {
        ALOGE ("%s: jni env is null", fn);
        return;
    }

    const UINT16 tlvMaxLen = aidBufferLen + 10;
    UINT8* tlv = new UINT8 [tlvMaxLen];
    if (tlv == NULL)
    {
        ALOGE ("%s: fail allocate tlv", fn);
        return;
    }

    memcpy (tlv, aidBuffer, aidBufferLen);
    UINT16 tlvActualLen = aidBufferLen;

    ScopedLocalRef<jobject> tlvJavaArray(e, e->NewByteArray(tlvActualLen));
    if (tlvJavaArray.get() == NULL)
    {
        ALOGE ("%s: fail allocate array", fn);
        goto TheEnd;
    }

    e->SetByteArrayRegion ((jbyteArray)tlvJavaArray.get(), 0, tlvActualLen, (jbyte *)tlv);
    if (e->ExceptionCheck())
    {
        e->ExceptionClear();
        ALOGE ("%s: fail fill array", fn);
        goto TheEnd;
    }
#ifdef NXP_EXT
    if (dataBufferLen > 0)
    {
        const UINT16 dataTlvMaxLen = dataBufferLen + 10;
        UINT8* datatlv = new UINT8 [dataTlvMaxLen];
        if (datatlv == NULL)
        {
            ALOGE ("%s: fail allocate tlv", fn);
            return;
        }

        memcpy (datatlv, dataBuffer, dataBufferLen);
        UINT16 dataTlvActualLen = dataBufferLen;

        ScopedLocalRef<jobject> dataTlvJavaArray(e, e->NewByteArray(dataTlvActualLen));
        if (dataTlvJavaArray.get() == NULL)
        {
            ALOGE ("%s: fail allocate array", fn);
            goto Clean;
        }

        e->SetByteArrayRegion ((jbyteArray)dataTlvJavaArray.get(), 0,
                dataTlvActualLen, (jbyte *)datatlv);
        if (e->ExceptionCheck())
        {
            e->ExceptionClear();
            ALOGE ("%s: fail fill array", fn);
            goto Clean;
        }

        e->CallVoidMethod (mNativeData->manager,
                android::gCachedNfcManagerNotifyTransactionListeners,
                tlvJavaArray.get(), dataTlvJavaArray.get(), evtSrc);
        if (e->ExceptionCheck())
        {
            e->ExceptionClear();
            ALOGE ("%s: fail notify", fn);
            goto Clean;
        }

     Clean:
        delete [] datatlv;
    }
    else
    {
        e->CallVoidMethod (mNativeData->manager,
                android::gCachedNfcManagerNotifyTransactionListeners,
                tlvJavaArray.get(), NULL, evtSrc);

        if (e->ExceptionCheck())
        {
            e->ExceptionClear();
            ALOGE ("%s: fail notify", fn);
            goto TheEnd;
        }
    }
#else
    e->CallVoidMethod (mNativeData->manager,
            android::gCachedNfcManagerNotifyTransactionListeners, tlvJavaArray.get());

    if (e->ExceptionCheck())
    {
        e->ExceptionClear();
        ALOGE ("%s: fail notify", fn);
        goto TheEnd;
    }
#endif

TheEnd:
    delete [] tlv;
    ALOGD ("%s: exit", fn);
}

#ifdef NXP_EXT
/*******************************************************************************
**
** Function:        notifyConnectivityListeners
**
** Description:     Notify the NFC service about a connectivity event from secure element.
**                  evtSrc: source of event UICC/eSE.
**
** Returns:         None
**
*******************************************************************************/
void SecureElement::notifyConnectivityListeners (UINT8 evtSrc)
{
    static const char fn [] = "SecureElement::notifyConnectivityListeners";
    ALOGD ("%s: enter; evtSrc =%u", fn, evtSrc);

    JNIEnv* e = NULL;
    ScopedAttach attach(mNativeData->vm, &e);
    if (e == NULL)
    {
        ALOGE ("%s: jni env is null", fn);
        return;
    }

    e->CallVoidMethod (mNativeData->manager, android::gCachedNfcManagerNotifyConnectivityListeners,
            evtSrc);
    if (e->ExceptionCheck())
    {
        e->ExceptionClear();
        ALOGE ("%s: fail notify", fn);
        goto TheEnd;
    }

TheEnd:
    ALOGD ("%s: exit", fn);
}

/*******************************************************************************
**
** Function:        notifyEmvcoMultiCardDetectedListeners
**
** Description:     Notify the NFC service about a multiple card presented to
**                  Emvco reader.
**
** Returns:         None
**
*******************************************************************************/
void SecureElement::notifyEmvcoMultiCardDetectedListeners ()
{
    static const char fn [] = "SecureElement::notifyEmvcoMultiCardDetectedListeners";
    ALOGD ("%s: enter; evtSrc =%u", fn);

    JNIEnv* e = NULL;
    ScopedAttach attach(mNativeData->vm, &e);
    if (e == NULL)
    {
        ALOGE ("%s: jni env is null", fn);
        return;
    }

    e->CallVoidMethod (mNativeData->manager,
        android::gCachedNfcManagerNotifyEmvcoMultiCardDetectedListeners);
    if (e->ExceptionCheck())
    {
        e->ExceptionClear();
        ALOGE ("%s: fail notify", fn);
        goto TheEnd;
    }

TheEnd:
    ALOGD ("%s: exit", fn);
}
#endif
/*******************************************************************************
**
** Function:        connectEE
**
** Description:     Connect to the execution environment.
**
** Returns:         True if ok.
**
*******************************************************************************/
bool SecureElement::connectEE ()
{
    static const char fn [] = "SecureElement::connectEE";
    tNFA_STATUS nfaStat = NFA_STATUS_FAILED;
    bool        retVal = false;
    UINT8       destHost = 0;
    unsigned long num = 0;
    char pipeConfName[40];
    tNFA_HANDLE  eeHandle = mActiveEeHandle;

    ALOGD ("%s: enter, mActiveEeHandle: 0x%04x, SEID: 0x%x, pipe_gate_num=%d, use pipe=%d",
        fn, mActiveEeHandle, gSEId, gGatePipe, gUseStaticPipe);

    if (!mIsInit)
    {
        ALOGE ("%s: not init", fn);
        return (false);
    }

    if (gSEId != -1)
    {
        eeHandle = gSEId | NFA_HANDLE_GROUP_EE;
        ALOGD ("%s: Using SEID: 0x%x", fn, eeHandle );
    }

    if (eeHandle == NFA_HANDLE_INVALID)
    {
        ALOGE ("%s: invalid handle 0x%X", fn, eeHandle);
        return (false);
    }

    tNFA_EE_INFO *pEE = findEeByHandle (eeHandle);

    if (pEE == NULL)
    {
        ALOGE ("%s: Handle 0x%04x  NOT FOUND !!", fn, eeHandle);
        return (false);
    }

    // Disable RF discovery completely while the DH is connected
    android::startRfDiscovery(false);

    // Disable UICC idle timeout while the DH is connected
    android::setUiccIdleTimeout (false);

    mNewSourceGate = 0;

    if (gGatePipe == -1)
    {
        // pipe/gate num was not specifed by app, get from config file
        mNewPipeId     = 0;

        // Construct the PIPE name based on the EE handle (e.g. NFA_HCI_STATIC_PIPE_ID_F3 for UICC0).
        snprintf (pipeConfName, sizeof(pipeConfName), "NFA_HCI_STATIC_PIPE_ID_%02X", eeHandle & NFA_HANDLE_MASK);

        if (GetNumValue(pipeConfName, &num, sizeof(num)) && (num != 0))
        {
            mNewPipeId = num;
            ALOGD ("%s: Using static pipe id: 0x%X", __FUNCTION__, mNewPipeId);
        }
        else
        {
            ALOGD ("%s: Did not find value '%s' defined in the .conf", __FUNCTION__, pipeConfName);
        }
    }
    else
    {
        if (gUseStaticPipe)
        {
            mNewPipeId     = gGatePipe;
        }
        else
        {
            mNewPipeId      = 0;
            mDestinationGate= gGatePipe;
        }
    }

    // If the .conf file had a static pipe to use, just use it.
    if (mNewPipeId != 0)
    {
#ifdef NXP_EXT
        UINT8 host = (mNewPipeId == STATIC_PIPE_0x70) ? 0xC0 : 0x03;
#else
        UINT8 host = (mNewPipeId == STATIC_PIPE_0x70) ? 0x02 : 0x03;
#endif
        UINT8 gate = (mNewPipeId == STATIC_PIPE_0x70) ? 0xF0 : 0xF1;
        nfaStat = NFA_HciAddStaticPipe(mNfaHciHandle, host, gate, mNewPipeId);
        if (nfaStat != NFA_STATUS_OK)
        {
            ALOGE ("%s: fail create static pipe; error=0x%X", fn, nfaStat);
            retVal = false;
            goto TheEnd;
        }
    }
    else
    {
        if ( (pEE->num_tlvs >= 1) && (pEE->ee_tlv[0].tag == NFA_EE_TAG_HCI_HOST_ID) )
            destHost = pEE->ee_tlv[0].info[0];
        else
#ifdef NXP_EXT
            destHost = 0xC0;
#else
            destHost = 2;
#endif

        // Get a list of existing gates and pipes
        {
            ALOGD ("%s: get gate, pipe list", fn);
            SyncEventGuard guard (mPipeListEvent);
            nfaStat = NFA_HciGetGateAndPipeList (mNfaHciHandle);
            if (nfaStat == NFA_STATUS_OK)
            {
                mPipeListEvent.wait();
                if (mHciCfg.status == NFA_STATUS_OK)
                {
                    for (UINT8 xx = 0; xx < mHciCfg.num_pipes; xx++)
                    {
                        if ( (mHciCfg.pipe[xx].dest_host == destHost)
                         &&  (mHciCfg.pipe[xx].dest_gate == mDestinationGate) )
                        {
                            mNewSourceGate = mHciCfg.pipe[xx].local_gate;
                            mNewPipeId     = mHciCfg.pipe[xx].pipe_id;

                            ALOGD ("%s: found configured gate: 0x%02x  pipe: 0x%02x", fn, mNewSourceGate, mNewPipeId);
                            break;
                        }
                    }
                }
            }
        }

        if (mNewSourceGate == 0)
        {
            ALOGD ("%s: allocate gate", fn);
            //allocate a source gate and store in mNewSourceGate
            SyncEventGuard guard (mAllocateGateEvent);
            if ((nfaStat = NFA_HciAllocGate (mNfaHciHandle)) != NFA_STATUS_OK)
            {
                ALOGE ("%s: fail allocate source gate; error=0x%X", fn, nfaStat);
                goto TheEnd;
            }
            mAllocateGateEvent.wait ();
            if (mCommandStatus != NFA_STATUS_OK)
               goto TheEnd;
        }

        if (mNewPipeId == 0)
        {
            ALOGD ("%s: create pipe", fn);
            SyncEventGuard guard (mCreatePipeEvent);
            nfaStat = NFA_HciCreatePipe (mNfaHciHandle, mNewSourceGate, destHost, mDestinationGate);
            if (nfaStat != NFA_STATUS_OK)
            {
                ALOGE ("%s: fail create pipe; error=0x%X", fn, nfaStat);
                goto TheEnd;
            }
            mCreatePipeEvent.wait ();
            if (mCommandStatus != NFA_STATUS_OK)
               goto TheEnd;
        }

        {
            ALOGD ("%s: open pipe", fn);
            SyncEventGuard guard (mPipeOpenedEvent);
            nfaStat = NFA_HciOpenPipe (mNfaHciHandle, mNewPipeId);
            if (nfaStat != NFA_STATUS_OK)
            {
                ALOGE ("%s: fail open pipe; error=0x%X", fn, nfaStat);
                goto TheEnd;
            }
            mPipeOpenedEvent.wait ();
            if (mCommandStatus != NFA_STATUS_OK)
               goto TheEnd;
        }
    }

    retVal = true;

TheEnd:
    mIsPiping = retVal;
    if (!retVal)
    {
        // if open failed we need to de-allocate the gate
        disconnectEE(0);
    }

    ALOGD ("%s: exit; ok=%u", fn, retVal);
    return retVal;
}


/*******************************************************************************
**
** Function:        disconnectEE
**
** Description:     Disconnect from the execution environment.
**                  seID: ID of secure element.
**
** Returns:         True if ok.
**
*******************************************************************************/
bool SecureElement::disconnectEE (jint seID)
{
    static const char fn [] = "SecureElement::disconnectEE";
    tNFA_STATUS nfaStat = NFA_STATUS_FAILED;
    tNFA_HANDLE eeHandle = seID;

    ALOGD("%s: seID=0x%X; handle=0x%04x", fn, seID, eeHandle);

    if (mUseOberthurWarmReset)
    {
        //send warm-reset command to Oberthur secure element which deselects the applet;
        //this is an Oberthur-specific command;
        ALOGD("%s: try warm-reset on pipe id 0x%X; cmd=0x%X", fn, mNewPipeId, mOberthurWarmResetCommand);
        SyncEventGuard guard (mRegistryEvent);
        nfaStat = NFA_HciSetRegistry (mNfaHciHandle, mNewPipeId,
                1, 1, &mOberthurWarmResetCommand);
        if (nfaStat == NFA_STATUS_OK)
        {
            mRegistryEvent.wait ();
            ALOGD("%s: completed warm-reset on pipe 0x%X", fn, mNewPipeId);
        }
    }

    if (mNewSourceGate)
    {
        SyncEventGuard guard (mDeallocateGateEvent);
        if ((nfaStat = NFA_HciDeallocGate (mNfaHciHandle, mNewSourceGate)) == NFA_STATUS_OK)
            mDeallocateGateEvent.wait ();
        else
            ALOGE ("%s: fail dealloc gate; error=0x%X", fn, nfaStat);
    }

    mIsPiping = false;

    // Re-enable UICC low-power mode
    android::setUiccIdleTimeout (true);
    // Re-enable RF discovery
    // Note that it only effactuates the current configuration,
    // so if polling/listening were configured OFF (forex because
    // the screen was off), they will stay OFF with this call.
    android::startRfDiscovery(true);

    return true;
}


/*******************************************************************************
**
** Function:        transceive
**
** Description:     Send data to the secure element; read it's response.
**                  xmitBuffer: Data to transmit.
**                  xmitBufferSize: Length of data.
**                  recvBuffer: Buffer to receive response.
**                  recvBufferMaxSize: Maximum size of buffer.
**                  recvBufferActualSize: Actual length of response.
**                  timeoutMillisec: timeout in millisecond.
**
** Returns:         True if ok.
**
*******************************************************************************/
bool SecureElement::transceive (UINT8* xmitBuffer, INT32 xmitBufferSize, UINT8* recvBuffer,
        INT32 recvBufferMaxSize, INT32& recvBufferActualSize, INT32 timeoutMillisec)
{
    static const char fn [] = "SecureElement::transceive";
    tNFA_STATUS nfaStat = NFA_STATUS_FAILED;
    bool isSuccess = false;
    bool waitOk = false;
    UINT8 newSelectCmd[NCI_MAX_AID_LEN + 10];

    ALOGD ("%s: enter; xmitBufferSize=%ld; recvBufferMaxSize=%ld; timeout=%ld", fn, xmitBufferSize, recvBufferMaxSize, timeoutMillisec);

    // Check if we need to replace an "empty" SELECT command.
    // 1. Has there been a AID configured, and
    // 2. Is that AID a valid length (i.e 16 bytes max), and
    // 3. Is the APDU at least 4 bytes (for header), and
    // 4. Is INS == 0xA4 (SELECT command), and
    // 5. Is P1 == 0x04 (SELECT by AID), and
    // 6. Is the APDU len 4 or 5 bytes.
    //
    // Note, the length of the configured AID is in the first
    //   byte, and AID starts from the 2nd byte.
    if (mAidForEmptySelect[0]                           // 1
        && (mAidForEmptySelect[0] <= NCI_MAX_AID_LEN)   // 2
        && (xmitBufferSize >= 4)                        // 3
        && (xmitBuffer[1] == 0xA4)                      // 4
        && (xmitBuffer[2] == 0x04)                      // 5
        && (xmitBufferSize <= 5))                       // 6
    {
        UINT8 idx = 0;

        // Copy APDU command header from the input buffer.
        memcpy(&newSelectCmd[0], &xmitBuffer[0], 4);
        idx = 4;

        // Set the Lc value to length of the new AID
        newSelectCmd[idx++] = mAidForEmptySelect[0];

        // Copy the AID
        memcpy(&newSelectCmd[idx], &mAidForEmptySelect[1], mAidForEmptySelect[0]);
        idx += mAidForEmptySelect[0];

        // If there is an Le (5th byte of APDU), add it to the end.
        if (xmitBufferSize == 5)
            newSelectCmd[idx++] = xmitBuffer[4];

        // Point to the new APDU
        xmitBuffer = &newSelectCmd[0];
        xmitBufferSize = idx;

        ALOGD ("%s: Empty AID SELECT cmd detected, substituting AID from config file, new length=%d", fn, idx);
    }

    {
        SyncEventGuard guard (mTransceiveEvent);
        mActualResponseSize = 0;
        memset (mResponseData, 0, sizeof(mResponseData));
        if ((mNewPipeId == STATIC_PIPE_0x70) || (mNewPipeId == STATIC_PIPE_0x71))
            nfaStat = NFA_HciSendEvent (mNfaHciHandle, mNewPipeId, EVT_SEND_DATA, xmitBufferSize, xmitBuffer, sizeof(mResponseData), mResponseData, 0);
        else
            nfaStat = NFA_HciSendEvent (mNfaHciHandle, mNewPipeId, NFA_HCI_EVT_POST_DATA, xmitBufferSize, xmitBuffer, sizeof(mResponseData), mResponseData, 0);

        if (nfaStat == NFA_STATUS_OK)
        {
            waitOk = mTransceiveEvent.wait (timeoutMillisec);
            if (waitOk == false) //timeout occurs
            {
                ALOGE ("%s: wait response timeout", fn);
                goto TheEnd;
            }
        }
        else
        {
            ALOGE ("%s: fail send data; error=0x%X", fn, nfaStat);
            goto TheEnd;
        }
    }

    if (mActualResponseSize > recvBufferMaxSize)
        recvBufferActualSize = recvBufferMaxSize;
    else
        recvBufferActualSize = mActualResponseSize;

    memcpy (recvBuffer, mResponseData, recvBufferActualSize);
    isSuccess = true;

TheEnd:
    ALOGD ("%s: exit; isSuccess: %d; recvBufferActualSize: %ld", fn, isSuccess, recvBufferActualSize);
    return (isSuccess);
}


/*******************************************************************************
**
** Function:        notifyListenModeState
**
** Description:     Notify the NFC service about whether the SE was activated
**                  in listen mode.
**                  isActive: Whether the secure element is activated.
**
** Returns:         None
**
*******************************************************************************/
void SecureElement::notifyListenModeState (bool isActivated) {
    static const char fn [] = "SecureElement::notifyListenMode";

    ALOGD ("%s: enter; listen mode active=%u", fn, isActivated);

    JNIEnv* e = NULL;
    ScopedAttach attach(mNativeData->vm, &e);
    if (e == NULL)
    {
        ALOGE ("%s: jni env is null", fn);
        return;
    }

    mActivatedInListenMode = isActivated;
    if (isActivated) {
        e->CallVoidMethod (mNativeData->manager, android::gCachedNfcManagerNotifySeListenActivated);
    }
    else {
        e->CallVoidMethod (mNativeData->manager, android::gCachedNfcManagerNotifySeListenDeactivated);
    }

    if (e->ExceptionCheck())
    {
        e->ExceptionClear();
        ALOGE ("%s: fail notify", fn);
    }

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
void SecureElement::notifyRfFieldEvent (bool isActive)
{
    static const char fn [] = "SecureElement::notifyRfFieldEvent";
    ALOGD ("%s: enter; is active=%u", fn, isActive);

    JNIEnv* e = NULL;
    ScopedAttach attach(mNativeData->vm, &e);
    if (e == NULL)
    {
        ALOGE ("%s: jni env is null", fn);
        return;
    }

    mMutex.lock();
    int ret = clock_gettime (CLOCK_MONOTONIC, &mLastRfFieldToggle);
    if (ret == -1) {
        ALOGE("%s: clock_gettime failed", fn);
        // There is no good choice here...
    }
    if (isActive) {
        mRfFieldIsOn = true;
        e->CallVoidMethod (mNativeData->manager, android::gCachedNfcManagerNotifySeFieldActivated);
    }
    else {
        mRfFieldIsOn = false;
        e->CallVoidMethod (mNativeData->manager, android::gCachedNfcManagerNotifySeFieldDeactivated);
    }
    mMutex.unlock();

    if (e->ExceptionCheck())
    {
        e->ExceptionClear();
        ALOGE ("%s: fail notify", fn);
    }
    ALOGD ("%s: exit", fn);
}

#ifdef NXP_EXT
/*Reader over SWP*/
void SecureElement::notifyEEReaderEvent (int evt, int data)
{
    static const char fn [] = "SecureElement::notifyEEReaderEvent";
    ALOGD ("%s: enter; event=%x", fn, evt);

    JNIEnv* e = NULL;
    ScopedAttach attach(mNativeData->vm, &e);
    if (e == NULL)
    {
        ALOGE ("%s: jni env is null", fn);
        return;
    }

    mMutex.lock();
    int ret = clock_gettime (CLOCK_MONOTONIC, &mLastRfFieldToggle);
    if (ret == -1) {
        ALOGE("%s: clock_gettime failed", fn);
        // There is no good choice here...
    }
    switch (evt) {
        case NFA_RD_SWP_READER_REQUESTED:
            ALOGD ("%s: NFA_RD_SWP_READER_REQUESTED for tech %x", fn, data);
            {
                jboolean istypeA = false;
                jboolean istypeB = false;

                if(data && NFA_TECHNOLOGY_MASK_A)
                    istypeA = true;
                if(data && NFA_TECHNOLOGY_MASK_B)
                    istypeB = true;

                e->CallVoidMethod (mNativeData->manager, android::gCachedNfcManagerNotifySWPReaderRequested,
                        istypeA, istypeB);
            }
            break;
        case NFA_RD_SWP_READER_START:
            ALOGD ("%s: NFA_RD_SWP_READER_START", fn);
            e->CallVoidMethod (mNativeData->manager, android::gCachedNfcManagerNotifySWPReaderActivated);
            break;
        case NFA_RD_SWP_READER_STOP:
            ALOGD ("%s: NFA_RD_SWP_READER_STOP", fn);
            e->CallVoidMethod (mNativeData->manager, android::gCachedNfcManagerNotifySWPReaderDeActivated);
            break;
        default:
            ALOGD ("%s: UNKNOWN EVENT ??", fn);
            break;
    }

    mMutex.unlock();

    if (e->ExceptionCheck())
    {
        e->ExceptionClear();
        ALOGE ("%s: fail notify", fn);
    }
    ALOGD ("%s: exit", fn);
}
void SecureElement::handleEEReaderEvent (int evt, int data, tNFA_HANDLE src)
{
    static const char fn [] = "SecureElement::handleEEReaderEvent";

    switch (evt) {
        case NFA_RD_SWP_READER_REQUESTED:
            ALOGD ("%s: NFA_RD_SWP_READER_REQUESTED for tech %x", fn, data);
            /*
            * 1. Stop the discovery.
            * 2. MAP the proprietary interface for Reader over SWP.NFC_DiscoveryMap, nfc_api.h
            * 3. start the discovery with reader req, type and DH configuration.
            * 4. Notify Nfc Service.
            */
            // Disable RF discovery completely while the DH is connected
            android::startRfDiscovery(false);
            tNFC_STATUS status;
            if(src == EE_HANDLE_0xF4) //UICC
            {
                SyncEventGuard guard (mDiscMapEvent);
                ALOGD ("%s: mapping intf for UICC", fn);
                status = NFC_DiscoveryMap (NFC_NUM_INTERFACE_MAP,(tNCI_DISCOVER_MAPS *)nfc_interface_mapping_uicc
                        ,SecureElement::discovery_map_cb);
                if (status != NFA_STATUS_OK)
                {
                    ALOGE ("%s: fail intf mapping for UICC; error=0x%X", fn, status);
                    return ;
                }
                mDiscMapEvent.wait ();

            }
            else if(src == EE_HANDLE_0xF4) //ESE
            {
                SyncEventGuard guard (mDiscMapEvent);
                ALOGD ("%s: mapping intf for ESE", fn);
                status = NFC_DiscoveryMap (NFC_NUM_INTERFACE_MAP,(tNCI_DISCOVER_MAPS *)nfc_interface_mapping_ese
                        ,SecureElement::discovery_map_cb);
                if (status != NFA_STATUS_OK)
                {
                    ALOGE ("%s: fail intf mapping for ESE; error=0x%X", fn, status);
                    return ;
                }
                mDiscMapEvent.wait ();
            }
            else
            {
                ALOGD ("%s: UNKNOWN SOURCE!!! ", fn);
            }
            //TODO: configure polling loop as swp reader configuration
            android::startRfDiscovery(true);

            SecureElement::getInstance().notifyEEReaderEvent(evt,data);
            break;

        case NFA_RD_SWP_READER_STOP:
           /*
            * 1. IF yes than send this info(STOP_READER_EVENT) till FWK level.
            * 2. MAP the DH interface for Reader over SWP. NFC_DiscoveryMap, nfc_api.h
            * 3. start the discovery with DH configuration.
            * 4. Notify Nfc Service.
            */

            ALOGD ("%s: NFA_RD_SWP_READER_STOP", fn);
            // Disable RF discovery completely while the DH is connected
            android::startRfDiscovery(false);
            {
                tNFC_STATUS status;
                SyncEventGuard guard (mDiscMapEvent);
                ALOGD ("%s: mapping intf for DH", fn);
                status = NFC_DiscoveryMap (NFC_NUM_INTERFACE_MAP,(tNCI_DISCOVER_MAPS *) nfc_interface_mapping_default
                        ,SecureElement::discovery_map_cb);
                if (status != NFA_STATUS_OK)
                {
                    ALOGE ("%s: fail intf mapping for DH; error=0x%X", fn, status);
                    return ;
                }
                mDiscMapEvent.wait ();
            }
            //TODO: configure polling loop as DH configuration
            android::startRfDiscovery(true);

            SecureElement::getInstance().notifyEEReaderEvent(evt,data);

            break;
        default:
            ALOGD ("%s: UNKNOWN EVENT ??", fn);
            break;
    }
}
#endif
/*******************************************************************************
**
** Function:        resetRfFieldStatus
**
** Description:     Resets the field status.
**                  isActive: Whether any secure element is activated.
**
** Returns:         None
**
*******************************************************************************/
void SecureElement::resetRfFieldStatus ()
{
    static const char fn [] = "SecureElement::resetRfFieldStatus`";
    ALOGD ("%s: enter;",fn);

    mMutex.lock();
    mRfFieldIsOn = false;
    int ret = clock_gettime (CLOCK_MONOTONIC, &mLastRfFieldToggle);
    if (ret == -1) {
        ALOGE("%s: clock_gettime failed", fn);
        // There is no good choice here...
    }
    mMutex.unlock();

    ALOGD ("%s: exit", fn);
}


/*******************************************************************************
**
** Function:        storeUiccInfo
**
** Description:     Store a copy of the execution environment information from the stack.
**                  info: execution environment information.
**
** Returns:         None
**
*******************************************************************************/
void SecureElement::storeUiccInfo (tNFA_EE_DISCOVER_REQ& info)
{
    static const char fn [] = "SecureElement::storeUiccInfo";
    ALOGD ("%s:  Status: %u   Num EE: %u", fn, info.status, info.num_ee);

    SyncEventGuard guard (mUiccInfoEvent);
    memcpy (&mUiccInfo, &info, sizeof(mUiccInfo));
    for (UINT8 xx = 0; xx < info.num_ee; xx++)
    {
        //for each technology (A, B, F, B'), print the bit field that shows
        //what protocol(s) is support by that technology
        ALOGD ("%s   EE[%u] Handle: 0x%04x  techA: 0x%02x  techB: 0x%02x  techF: 0x%02x  techBprime: 0x%02x",
                fn, xx, info.ee_disc_info[xx].ee_handle,
                info.ee_disc_info[xx].la_protocol,
                info.ee_disc_info[xx].lb_protocol,
                info.ee_disc_info[xx].lf_protocol,
                info.ee_disc_info[xx].lbp_protocol);
    }
    mUiccInfoEvent.notifyOne ();
}


/*******************************************************************************
**
** Function:        getUiccId
**
** Description:     Get the ID of the secure element.
**                  eeHandle: Handle to the secure element.
**                  uid: Array to receive the ID.
**
** Returns:         True if ok.
**
*******************************************************************************/
bool SecureElement::getUiccId (tNFA_HANDLE eeHandle, jbyteArray& uid)
{
    static const char fn [] = "SecureElement::getUiccId";
    ALOGD ("%s: ee h=0x%X", fn, eeHandle);
    bool retval = false;

    JNIEnv* e = NULL;
    ScopedAttach attach(mNativeData->vm, &e);
    if (e == NULL)
    {
        ALOGE ("%s: jni env is null", fn);
        return false;
    }

    findUiccByHandle (eeHandle);
    //cannot get UID from the stack; nothing to do

    // TODO: uid is unused --- bug?

    // TODO: retval is always false --- bug?
    ALOGD ("%s: exit; ret=%u", fn, retval);
    return retval;
}


/*******************************************************************************
**
** Function:        getTechnologyList
**
** Description:     Get all the technologies supported by a secure element.
**                  eeHandle: Handle of secure element.
**                  techList: List to receive the technologies.
**
** Returns:         True if ok.
**
*******************************************************************************/
bool SecureElement::getTechnologyList (tNFA_HANDLE eeHandle, jintArray& techList)
{
    static const char fn [] = "SecureElement::getTechnologyList";
    ALOGD ("%s: ee h=0x%X", fn, eeHandle);
    bool retval = false;

    JNIEnv* e = NULL;
    ScopedAttach attach(mNativeData->vm, &e);
    if (e == NULL)
    {
        ALOGE ("%s: jni env is null", fn);
        return false;
    }

    tNFA_EE_DISCOVER_INFO *pUICC = findUiccByHandle (eeHandle);

    // TODO: theList is written but not set --- bug?
    jint theList = 0;
    if (pUICC->la_protocol != 0)
        theList = TARGET_TYPE_ISO14443_3A;
    else if (pUICC->lb_protocol != 0)
        theList = TARGET_TYPE_ISO14443_3B;
    else if (pUICC->lf_protocol != 0)
        theList = TARGET_TYPE_FELICA;
    else if (pUICC->lbp_protocol != 0)
        theList = TARGET_TYPE_ISO14443_3B;
    else
        theList = TARGET_TYPE_UNKNOWN;

    // TODO: techList is neither read nor written --- bug?

    // TODO: retval is always false --- bug?
    ALOGD ("%s: exit; ret=%u", fn, retval);
    return retval;
}


/*******************************************************************************
**
** Function:        adjustRoutes
**
** Description:     Adjust routes in the controller's listen-mode routing table.
**                  selection: which set of routes to configure the controller.
**
** Returns:         None
**
*******************************************************************************/
void SecureElement::adjustRoutes (RouteSelection selection)
{
    static const char fn [] = "SecureElement::adjustRoutes";
    ALOGD ("%s: enter; selection=%u", fn, selection);
    RouteDataSet::Database* db = mRouteDataSet.getDatabase (RouteDataSet::DefaultRouteDatabase);

    if (selection == SecElemRoute)
        db = mRouteDataSet.getDatabase (RouteDataSet::SecElemRouteDatabase);

    mCurrentRouteSelection = selection;
    adjustProtocolRoutes (db, selection);
    adjustTechnologyRoutes (db, selection);
    HostAidRouter::getInstance ().deleteAllRoutes (); //stop all AID routes to host

    if (db->empty())
    {
        ALOGD ("%s: no route configuration", fn);
        goto TheEnd;
    }


TheEnd:
    NFA_EeUpdateNow (); //apply new routes now
    ALOGD ("%s: exit", fn);
}


/*******************************************************************************
**
** Function:        applyRoutes
**
** Description:     Read route data from file and apply them again.
**
** Returns:         None
**
*******************************************************************************/
void SecureElement::applyRoutes ()
{
    static const char fn [] = "SecureElement::applyRoutes";
    ALOGD ("%s: enter", fn);
    if (mCurrentRouteSelection != NoRoute)
    {
        mRouteDataSet.import (); //read XML file
        adjustRoutes (mCurrentRouteSelection);
    }
    ALOGD ("%s: exit", fn);
}


/*******************************************************************************
**
** Function:        adjustProtocolRoutes
**
** Description:     Adjust default routing based on protocol in NFC listen mode.
**                  isRouteToEe: Whether routing to EE (true) or host (false).
**
** Returns:         None
**
*******************************************************************************/
void SecureElement::adjustProtocolRoutes (RouteDataSet::Database* db, RouteSelection routeSelection)
{
    static const char fn [] = "SecureElement::adjustProtocolRoutes";
    ALOGD ("%s: enter", fn);
    tNFA_STATUS nfaStat = NFA_STATUS_FAILED;
    const tNFA_PROTOCOL_MASK protoMask = NFA_PROTOCOL_MASK_ISO_DEP;

    ///////////////////////
    // delete route to host
    ///////////////////////
    {
        ALOGD ("%s: delete route to host", fn);
        SyncEventGuard guard (mRoutingEvent);
        if ((nfaStat = NFA_EeSetDefaultProtoRouting (NFA_EE_HANDLE_DH, 0, 0, 0)) == NFA_STATUS_OK)
            mRoutingEvent.wait ();
        else
            ALOGE ("%s: fail delete route to host; error=0x%X", fn, nfaStat);
    }

    ///////////////////////
    // delete route to every sec elem
    ///////////////////////
    for (int i=0; i < mActualNumEe; i++)
    {
        if ((mEeInfo[i].num_interface != 0) &&
                (mEeInfo[i].ee_interface[0] != NFC_NFCEE_INTERFACE_HCI_ACCESS) &&
                (mEeInfo[i].ee_status == NFA_EE_STATUS_ACTIVE))
        {
            ALOGD ("%s: delete route to EE h=0x%X", fn, mEeInfo[i].ee_handle);
            SyncEventGuard guard (mRoutingEvent);
            if ((nfaStat = NFA_EeSetDefaultProtoRouting (mEeInfo[i].ee_handle, 0, 0, 0)) == NFA_STATUS_OK)
                mRoutingEvent.wait ();
            else
                ALOGE ("%s: fail delete route to EE; error=0x%X", fn, nfaStat);
        }
    }

    //////////////////////
    // configure route for every discovered sec elem
    //////////////////////
    for (int i=0; i < mActualNumEe; i++)
    {
        //if sec elem is active
        if ((mEeInfo[i].num_interface != 0) &&
                (mEeInfo[i].ee_interface[0] != NFC_NFCEE_INTERFACE_HCI_ACCESS) &&
                (mEeInfo[i].ee_status == NFA_EE_STATUS_ACTIVE))
        {
            tNFA_PROTOCOL_MASK protocolsSwitchOn = 0; //all protocols that are active at full power
            tNFA_PROTOCOL_MASK protocolsSwitchOff = 0; //all protocols that are active when phone is turned off
            tNFA_PROTOCOL_MASK protocolsBatteryOff = 0; //all protocols that are active when there is no power

            //for every route in XML, look for protocol route;
            //collect every protocol according to it's desired power mode
            for (RouteDataSet::Database::iterator iter = db->begin(); iter != db->end(); iter++)
            {
                RouteData* routeData = *iter;
                RouteDataForProtocol* route = NULL;
                if (routeData->mRouteType != RouteData::ProtocolRoute)
                    continue; //skip other kinds of routing data
                route = (RouteDataForProtocol*) (*iter);
                if (route->mNfaEeHandle == mEeInfo[i].ee_handle)
                {
                    if (route->mSwitchOn)
                        protocolsSwitchOn |= route->mProtocol;
                    if (route->mSwitchOff)
                        protocolsSwitchOff |= route->mProtocol;
                    if (route->mBatteryOff)
                        protocolsBatteryOff |= route->mProtocol;
                }
            }

            if (protocolsSwitchOn | protocolsSwitchOff | protocolsBatteryOff)
            {
                ALOGD ("%s: route to EE h=0x%X", fn, mEeInfo[i].ee_handle);
                SyncEventGuard guard (mRoutingEvent);
                nfaStat = NFA_EeSetDefaultProtoRouting (mEeInfo[i].ee_handle,
                        protocolsSwitchOn, protocolsSwitchOff, protocolsBatteryOff);
                if (nfaStat == NFA_STATUS_OK)
                    mRoutingEvent.wait ();
                else
                    ALOGE ("%s: fail route to EE; error=0x%X", fn, nfaStat);
            }
        } //if sec elem is active
    } //for every discovered sec elem

    //////////////////////
    // configure route to host
    //////////////////////
    {
        tNFA_PROTOCOL_MASK protocolsSwitchOn = 0; //all protocols that are active at full power
        tNFA_PROTOCOL_MASK protocolsSwitchOff = 0; //all protocols that are active when phone is turned off
        tNFA_PROTOCOL_MASK protocolsBatteryOff = 0; //all protocols that are active when there is no power

        //for every route in XML, look for protocol route;
        //collect every protocol according to it's desired power mode
        for (RouteDataSet::Database::iterator iter = db->begin(); iter != db->end(); iter++)
        {
            RouteData* routeData = *iter;
            RouteDataForProtocol* route = NULL;
            if (routeData->mRouteType != RouteData::ProtocolRoute)
                continue; //skip other kinds of routing data
            route = (RouteDataForProtocol*) (*iter);
            if (route->mNfaEeHandle == NFA_EE_HANDLE_DH)
            {
                if (route->mSwitchOn)
                    protocolsSwitchOn |= route->mProtocol;
                if (route->mSwitchOff)
                    protocolsSwitchOff |= route->mProtocol;
                if (route->mBatteryOff)
                    protocolsBatteryOff |= route->mProtocol;
            }
        }

        if (protocolsSwitchOn | protocolsSwitchOff | protocolsBatteryOff)
        {
            ALOGD ("%s: route to EE h=0x%X", fn, NFA_EE_HANDLE_DH);
            SyncEventGuard guard (mRoutingEvent);
            nfaStat = NFA_EeSetDefaultProtoRouting (NFA_EE_HANDLE_DH,
                    protocolsSwitchOn, protocolsSwitchOff, protocolsBatteryOff);
            if (nfaStat == NFA_STATUS_OK)
                mRoutingEvent.wait ();
            else
                ALOGE ("%s: fail route to EE; error=0x%X", fn, nfaStat);
        }
    }

    //////////////////////
    // if route database is empty, setup a default route
    //////////////////////
#ifndef NXP_EXT
    if (db->empty())
    {
        tNFA_HANDLE eeHandle = NFA_EE_HANDLE_DH;
        if (routeSelection == SecElemRoute)
            eeHandle = mActiveEeHandle;
        ALOGD ("%s: route to default EE h=0x%X", fn, eeHandle);
        SyncEventGuard guard (mRoutingEvent);
        nfaStat = NFA_EeSetDefaultProtoRouting (eeHandle, protoMask, 0, 0);
        if (nfaStat == NFA_STATUS_OK)
            mRoutingEvent.wait ();
        else
            ALOGE ("%s: fail route to EE; error=0x%X", fn, nfaStat);
    }
#endif
    ALOGD ("%s: exit", fn);
}


/*******************************************************************************
**
** Function:        adjustTechnologyRoutes
**
** Description:     Adjust default routing based on technology in NFC listen mode.
**                  isRouteToEe: Whether routing to EE (true) or host (false).
**
** Returns:         None
**
*******************************************************************************/
void SecureElement::adjustTechnologyRoutes (RouteDataSet::Database* db, RouteSelection routeSelection)
{
    static const char fn [] = "SecureElement::adjustTechnologyRoutes";
    ALOGD ("%s: enter", fn);
    tNFA_STATUS nfaStat = NFA_STATUS_FAILED;
    const tNFA_TECHNOLOGY_MASK techMask = NFA_TECHNOLOGY_MASK_A | NFA_TECHNOLOGY_MASK_B;

    ///////////////////////
    // delete route to host
    ///////////////////////
    {
        ALOGD ("%s: delete route to host", fn);
        SyncEventGuard guard (mRoutingEvent);
        if ((nfaStat = NFA_EeSetDefaultTechRouting (NFA_EE_HANDLE_DH, 0, 0, 0)) == NFA_STATUS_OK)
            mRoutingEvent.wait ();
        else
            ALOGE ("%s: fail delete route to host; error=0x%X", fn, nfaStat);
    }

    ///////////////////////
    // delete route to every sec elem
    ///////////////////////
    for (int i=0; i < mActualNumEe; i++)
    {
        if ((mEeInfo[i].num_interface != 0) &&
                (mEeInfo[i].ee_interface[0] != NFC_NFCEE_INTERFACE_HCI_ACCESS) &&
                (mEeInfo[i].ee_status == NFA_EE_STATUS_ACTIVE))
        {
            ALOGD ("%s: delete route to EE h=0x%X", fn, mEeInfo[i].ee_handle);
            SyncEventGuard guard (mRoutingEvent);
            if ((nfaStat = NFA_EeSetDefaultTechRouting (mEeInfo[i].ee_handle, 0, 0, 0)) == NFA_STATUS_OK)
                mRoutingEvent.wait ();
            else
                ALOGE ("%s: fail delete route to EE; error=0x%X", fn, nfaStat);
        }
    }

    //////////////////////
    // configure route for every discovered sec elem
    //////////////////////
    for (int i=0; i < mActualNumEe; i++)
    {
        //if sec elem is active
        if ((mEeInfo[i].num_interface != 0) &&
                (mEeInfo[i].ee_interface[0] != NFC_NFCEE_INTERFACE_HCI_ACCESS) &&
                (mEeInfo[i].ee_status == NFA_EE_STATUS_ACTIVE))
        {
            tNFA_TECHNOLOGY_MASK techsSwitchOn = 0; //all techs that are active at full power
            tNFA_TECHNOLOGY_MASK techsSwitchOff = 0; //all techs that are active when phone is turned off
            tNFA_TECHNOLOGY_MASK techsBatteryOff = 0; //all techs that are active when there is no power

            //for every route in XML, look for tech route;
            //collect every tech according to it's desired power mode
            for (RouteDataSet::Database::iterator iter = db->begin(); iter != db->end(); iter++)
            {
                RouteData* routeData = *iter;
                RouteDataForTechnology* route = NULL;
                if (routeData->mRouteType != RouteData::TechnologyRoute)
                    continue; //skip other kinds of routing data
                route = (RouteDataForTechnology*) (*iter);
                if (route->mNfaEeHandle == mEeInfo[i].ee_handle)
                {
                    if (route->mSwitchOn)
                        techsSwitchOn |= route->mTechnology;
                    if (route->mSwitchOff)
                        techsSwitchOff |= route->mTechnology;
                    if (route->mBatteryOff)
                        techsBatteryOff |= route->mTechnology;
                }
            }

            if (techsSwitchOn | techsSwitchOff | techsBatteryOff)
            {
                ALOGD ("%s: route to EE h=0x%X", fn, mEeInfo[i].ee_handle);
                SyncEventGuard guard (mRoutingEvent);
                nfaStat = NFA_EeSetDefaultTechRouting (mEeInfo[i].ee_handle,
                        techsSwitchOn, techsSwitchOff, techsBatteryOff);
                if (nfaStat == NFA_STATUS_OK)
                    mRoutingEvent.wait ();
                else
                    ALOGE ("%s: fail route to EE; error=0x%X", fn, nfaStat);
            }
        } //if sec elem is active
    } //for every discovered sec elem

    //////////////////////
    // configure route to host
    //////////////////////
    {
        tNFA_TECHNOLOGY_MASK techsSwitchOn = 0; //all techs that are active at full power
        tNFA_TECHNOLOGY_MASK techsSwitchOff = 0; //all techs that are active when phone is turned off
        tNFA_TECHNOLOGY_MASK techsBatteryOff = 0; //all techs that are active when there is no power

        //for every route in XML, look for protocol route;
        //collect every protocol according to it's desired power mode
        for (RouteDataSet::Database::iterator iter = db->begin(); iter != db->end(); iter++)
        {
            RouteData* routeData = *iter;
            RouteDataForTechnology * route = NULL;
            if (routeData->mRouteType != RouteData::TechnologyRoute)
                continue; //skip other kinds of routing data
            route = (RouteDataForTechnology*) (*iter);
            if (route->mNfaEeHandle == NFA_EE_HANDLE_DH)
            {
                if (route->mSwitchOn)
                    techsSwitchOn |= route->mTechnology;
                if (route->mSwitchOff)
                    techsSwitchOff |= route->mTechnology;
                if (route->mBatteryOff)
                    techsBatteryOff |= route->mTechnology;
            }
        }

        if (techsSwitchOn | techsSwitchOff | techsBatteryOff)
        {
            ALOGD ("%s: route to EE h=0x%X", fn, NFA_EE_HANDLE_DH);
            SyncEventGuard guard (mRoutingEvent);
            nfaStat = NFA_EeSetDefaultTechRouting (NFA_EE_HANDLE_DH,
                    techsSwitchOn, techsSwitchOff, techsBatteryOff);
            if (nfaStat == NFA_STATUS_OK)
                mRoutingEvent.wait ();
            else
                ALOGE ("%s: fail route to EE; error=0x%X", fn, nfaStat);
        }
    }

    //////////////////////
    // if route database is empty, setup a default route
    //////////////////////
#ifndef NXP_EXT
    if (db->empty())
    {
        tNFA_HANDLE eeHandle = NFA_EE_HANDLE_DH;
        if (routeSelection == SecElemRoute)
            eeHandle = mActiveEeHandle;
        ALOGD ("%s: route to default EE h=0x%X", fn, eeHandle);
        SyncEventGuard guard (mRoutingEvent);
        nfaStat = NFA_EeSetDefaultTechRouting (eeHandle, techMask, 0, 0);
        if (nfaStat == NFA_STATUS_OK)
            mRoutingEvent.wait ();
        else
            ALOGE ("%s: fail route to EE; error=0x%X", fn, nfaStat);
    }
#endif
    ALOGD ("%s: exit", fn);
}


/*******************************************************************************
**
** Function:        nfaEeCallback
**
** Description:     Receive execution environment-related events from stack.
**                  event: Event code.
**                  eventData: Event data.
**
** Returns:         None
**
*******************************************************************************/
void SecureElement::nfaEeCallback (tNFA_EE_EVT event, tNFA_EE_CBACK_DATA* eventData)
{
    static const char fn [] = "SecureElement::nfaEeCallback";
#ifdef NXP_EXT
    tNFA_EE_DISCOVER_REQ info = eventData->discover_req;
#endif
    switch (event)
    {
    case NFA_EE_REGISTER_EVT:
        {
            SyncEventGuard guard (sSecElem.mEeRegisterEvent);
            ALOGD ("%s: NFA_EE_REGISTER_EVT; status=%u", fn, eventData->ee_register);
            sSecElem.mEeRegisterEvent.notifyOne();
        }
        break;

    case NFA_EE_MODE_SET_EVT:
        {
            ALOGD ("%s: NFA_EE_MODE_SET_EVT; status: 0x%04X  handle: 0x%04X  mActiveEeHandle: 0x%04X", fn,
                    eventData->mode_set.status, eventData->mode_set.ee_handle, sSecElem.mActiveEeHandle);

            if (eventData->mode_set.status == NFA_STATUS_OK)
            {
                tNFA_EE_INFO *pEE = sSecElem.findEeByHandle (eventData->mode_set.ee_handle);
                if (pEE)
                {
                    pEE->ee_status ^= 1;
                    ALOGD ("%s: NFA_EE_MODE_SET_EVT; pEE->ee_status: %s (0x%04x)", fn, SecureElement::eeStatusToString(pEE->ee_status), pEE->ee_status);
                }
                else
                    ALOGE ("%s: NFA_EE_MODE_SET_EVT; EE: 0x%04x not found.  mActiveEeHandle: 0x%04x", fn, eventData->mode_set.ee_handle, sSecElem.mActiveEeHandle);
            }
            SyncEventGuard guard (sSecElem.mEeSetModeEvent);
            sSecElem.mEeSetModeEvent.notifyOne();
        }
        break;

    case NFA_EE_SET_TECH_CFG_EVT:
        {
            ALOGD ("%s: NFA_EE_SET_TECH_CFG_EVT; status=0x%X", fn, eventData->status);
            SyncEventGuard guard (sSecElem.mRoutingEvent);
            sSecElem.mRoutingEvent.notifyOne ();
        }
        break;

    case NFA_EE_SET_PROTO_CFG_EVT:
        {
            ALOGD ("%s: NFA_EE_SET_PROTO_CFG_EVT; status=0x%X", fn, eventData->status);
            SyncEventGuard guard (sSecElem.mRoutingEvent);
            sSecElem.mRoutingEvent.notifyOne ();
        }
        break;

    case NFA_EE_ACTION_EVT:
        {
            tNFA_EE_ACTION& action = eventData->action;
            if (action.trigger == NFC_EE_TRIG_SELECT)
                ALOGD ("%s: NFA_EE_ACTION_EVT; h=0x%X; trigger=select (0x%X)", fn, action.ee_handle, action.trigger);
            else if (action.trigger == NFC_EE_TRIG_APP_INIT)
            {
                tNFC_APP_INIT& app_init = action.param.app_init;
                ALOGD ("%s: NFA_EE_ACTION_EVT; h=0x%X; trigger=app-init (0x%X); aid len=%u; data len=%u", fn,
                        action.ee_handle, action.trigger, app_init.len_aid, app_init.len_data);
                //if app-init operation is successful;
                //app_init.data[] contains two bytes, which are the status codes of the event;
                //app_init.data[] does not contain an APDU response;
                //see EMV Contactless Specification for Payment Systems; Book B; Entry Point Specification;
                //version 2.1; March 2011; section 3.3.3.5;
                if ( (app_init.len_data > 1) &&
                     (app_init.data[0] == 0x90) &&
                     (app_init.data[1] == 0x00) )
                {
#ifdef NXP_EXT
                    sSecElem.notifyTransactionListenersOfAid (app_init.aid, app_init.len_aid,
                            app_init.data, app_init.len_data,
                            SecureElement::getInstance().getGenericEseId(action.ee_handle));
#else
                    sSecElem.notifyTransactionListenersOfAid (app_init.aid, app_init.len_aid);
#endif
                }
            }
            else if (action.trigger == NFC_EE_TRIG_RF_PROTOCOL)
                ALOGD ("%s: NFA_EE_ACTION_EVT; h=0x%X; trigger=rf protocol (0x%X)", fn, action.ee_handle, action.trigger);
            else if (action.trigger == NFC_EE_TRIG_RF_TECHNOLOGY)
                ALOGD ("%s: NFA_EE_ACTION_EVT; h=0x%X; trigger=rf tech (0x%X)", fn, action.ee_handle, action.trigger);
            else
                ALOGE ("%s: NFA_EE_ACTION_EVT; h=0x%X; unknown trigger (0x%X)", fn, action.ee_handle, action.trigger);
        }
        break;

    case NFA_EE_DISCOVER_REQ_EVT:
        ALOGD ("%s: NFA_EE_DISCOVER_REQ_EVT; status=0x%X; num ee=%u", __FUNCTION__,
                eventData->discover_req.status, eventData->discover_req.num_ee);
#ifdef NXP_EXT
        /* Handle Reader over SWP.
         * 1. Check if the event is for Reader over SWP.
         * 2. IF yes than send this info(READER_REQUESTED_EVENT) till FWK level.
         * 3. Stop the discovery.
         * 4. MAP the proprietary interface for Reader over SWP.NFC_DiscoveryMap, nfc_api.h
         * 5. start the discovery with reader req, type and DH configuration.
         *
         * 6. IF yes than send this info(STOP_READER_EVENT) till FWK level.
         * 7. MAP the DH interface for Reader over SWP. NFC_DiscoveryMap, nfc_api.h
         * 8. start the discovery with DH configuration.
         */
        for (UINT8 xx = 0; xx < info.num_ee; xx++)
        {
            //for each technology (A, B, F, B'), print the bit field that shows
            //what protocol(s) is support by that technology
            ALOGD ("%s   EE[%u] Handle: 0x%04x  PA: 0x%02x  PB: 0x%02x",
                    fn, xx, info.ee_disc_info[xx].ee_handle,
                    info.ee_disc_info[xx].pa_protocol,
                    info.ee_disc_info[xx].pb_protocol);

            if(info.ee_disc_info[xx].pa_protocol !=  0 || info.ee_disc_info[xx].pb_protocol != 0)
            {
                uint8_t tech = 0x00;
                if(info.ee_disc_info[xx].pa_protocol !=  0)
                    tech |= NFA_TECHNOLOGY_MASK_A;
                if(info.ee_disc_info[xx].pb_protocol !=  0)
                    tech |= NFA_TECHNOLOGY_MASK_B;
                //Reader over SWP - Reader Requested.
                SecureElement::getInstance().handleEEReaderEvent(NFA_RD_SWP_READER_REQUESTED, tech, info.ee_disc_info[xx].ee_handle);
                break;
            }
            else if(info.ee_disc_info[xx].pa_protocol ==  0xFF || info.ee_disc_info[xx].pb_protocol == 0xFF)
            {
                //Reader over SWP - Stop Reader Requested.
                SecureElement::getInstance().handleEEReaderEvent(NFA_RD_SWP_READER_STOP, 0x00,info.ee_disc_info[xx].ee_handle);
                break;
            }
        }
#endif
        /*Set the configuration for UICC/ESE */
        sSecElem.storeUiccInfo (eventData->discover_req);
        break;

    case NFA_EE_NO_CB_ERR_EVT:
        ALOGD ("%s: NFA_EE_NO_CB_ERR_EVT  status=%u", fn, eventData->status);
        break;

    case NFA_EE_ADD_AID_EVT:
        {
            ALOGD ("%s: NFA_EE_ADD_AID_EVT  status=%u", fn, eventData->status);
            SyncEventGuard guard (sSecElem.mAidAddRemoveEvent);
            sSecElem.mAidAddRemoveEvent.notifyOne ();
        }
        break;

    case NFA_EE_REMOVE_AID_EVT:
        {
            ALOGD ("%s: NFA_EE_REMOVE_AID_EVT  status=%u", fn, eventData->status);
            SyncEventGuard guard (sSecElem.mAidAddRemoveEvent);
            sSecElem.mAidAddRemoveEvent.notifyOne ();
        }
        break;

    case NFA_EE_NEW_EE_EVT:
        {
            ALOGD ("%s: NFA_EE_NEW_EE_EVT  h=0x%X; status=%u", fn,
                eventData->new_ee.ee_handle, eventData->new_ee.ee_status);
            // Indicate there are new EE
            sSecElem.mbNewEE = true;
        }
        break;

    default:
        ALOGE ("%s: unknown event=%u ????", fn, event);
        break;
    }
}

/*******************************************************************************
**
** Function         getSeVerInfo
**
** Description      Gets version information and id for a secure element.  The
**                  seIndex parmeter is the zero based index of the secure
**                  element to get verion info for.  The version infommation
**                  is returned as a string int the verInfo parameter.
**
** Returns          ture on success, false on failure
**
*******************************************************************************/
bool SecureElement::getSeVerInfo(int seIndex, char * verInfo, int verInfoSz, UINT8 * seid)
{
    ALOGD("%s: enter, seIndex=%d", __FUNCTION__, seIndex);

    if (seIndex > (mActualNumEe-1))
    {
        ALOGE("%s: invalid se index: %d, only %d SEs in system", __FUNCTION__, seIndex, mActualNumEe);
        return false;
    }

    *seid = mEeInfo[seIndex].ee_handle;

    if ((mEeInfo[seIndex].num_interface == 0) || (mEeInfo[seIndex].ee_interface[0] == NCI_NFCEE_INTERFACE_HCI_ACCESS) )
    {
        return false;
    }

    strncpy(verInfo, "Version info not available", verInfoSz-1);
    verInfo[verInfoSz-1] = '\0';

    UINT8 pipe = (mEeInfo[seIndex].ee_handle == EE_HANDLE_0xF3) ? 0x70 : 0x71;
    UINT8 host = (pipe == STATIC_PIPE_0x70) ? 0x02 : 0x03;
    UINT8 gate = (pipe == STATIC_PIPE_0x70) ? 0xF0 : 0xF1;

    tNFA_STATUS nfaStat = NFA_HciAddStaticPipe(mNfaHciHandle, host, gate, pipe);
    if (nfaStat != NFA_STATUS_OK)
    {
        ALOGE ("%s: NFA_HciAddStaticPipe() failed, pipe = 0x%x, error=0x%X", __FUNCTION__, pipe, nfaStat);
        return true;
    }

    SyncEventGuard guard (mVerInfoEvent);
    if (NFA_STATUS_OK == (nfaStat = NFA_HciGetRegistry (mNfaHciHandle, pipe, 0x02)))
    {
        if (false == mVerInfoEvent.wait(200))
        {
            ALOGE ("%s: wait response timeout", __FUNCTION__);
        }
        else
        {
            snprintf(verInfo, verInfoSz-1, "Oberthur OS S/N: 0x%02x%02x%02x", mVerInfo[0], mVerInfo[1], mVerInfo[2]);
            verInfo[verInfoSz-1] = '\0';
        }
    }
    else
    {
        ALOGE ("%s: NFA_HciGetRegistry () failed: 0x%X", __FUNCTION__, nfaStat);
    }
    return true;
}

/*******************************************************************************
**
** Function         getActualNumEe
**
** Description      Returns number of secure elements we know about.
**
** Returns          Number of secure elements we know about.
**
*******************************************************************************/
UINT8 SecureElement::getActualNumEe()
{
    return mActualNumEe;
}

/*******************************************************************************
**
** Function:        nfaHciCallback
**
** Description:     Receive Host Controller Interface-related events from stack.
**                  event: Event code.
**                  eventData: Event data.
**
** Returns:         None
**
*******************************************************************************/
void SecureElement::nfaHciCallback (tNFA_HCI_EVT event, tNFA_HCI_EVT_DATA* eventData)
{
    static const char fn [] = "SecureElement::nfaHciCallback";
    ALOGD ("%s: event=0x%X", fn, event);
#ifdef NXP_EXT
    int evtSrc = 0xFF;
#endif
    switch (event)
    {
    case NFA_HCI_REGISTER_EVT:
        {
            ALOGD ("%s: NFA_HCI_REGISTER_EVT; status=0x%X; handle=0x%X", fn,
                    eventData->hci_register.status, eventData->hci_register.hci_handle);
            SyncEventGuard guard (sSecElem.mHciRegisterEvent);
            sSecElem.mNfaHciHandle = eventData->hci_register.hci_handle;
            sSecElem.mHciRegisterEvent.notifyOne();
        }
        break;

    case NFA_HCI_ALLOCATE_GATE_EVT:
        {
            ALOGD ("%s: NFA_HCI_ALLOCATE_GATE_EVT; status=0x%X; gate=0x%X", fn, eventData->status, eventData->allocated.gate);
            SyncEventGuard guard (sSecElem.mAllocateGateEvent);
            sSecElem.mCommandStatus = eventData->status;
            sSecElem.mNewSourceGate = (eventData->allocated.status == NFA_STATUS_OK) ? eventData->allocated.gate : 0;
            sSecElem.mAllocateGateEvent.notifyOne();
        }
        break;

    case NFA_HCI_DEALLOCATE_GATE_EVT:
        {
            tNFA_HCI_DEALLOCATE_GATE& deallocated = eventData->deallocated;
            ALOGD ("%s: NFA_HCI_DEALLOCATE_GATE_EVT; status=0x%X; gate=0x%X", fn, deallocated.status, deallocated.gate);
            SyncEventGuard guard (sSecElem.mDeallocateGateEvent);
            sSecElem.mDeallocateGateEvent.notifyOne();
        }
        break;

    case NFA_HCI_GET_GATE_PIPE_LIST_EVT:
        {
            ALOGD ("%s: NFA_HCI_GET_GATE_PIPE_LIST_EVT; status=0x%X; num_pipes: %u  num_gates: %u", fn,
                    eventData->gates_pipes.status, eventData->gates_pipes.num_pipes, eventData->gates_pipes.num_gates);
            SyncEventGuard guard (sSecElem.mPipeListEvent);
            sSecElem.mCommandStatus = eventData->gates_pipes.status;
            sSecElem.mHciCfg = eventData->gates_pipes;
            sSecElem.mPipeListEvent.notifyOne();
        }
        break;

    case NFA_HCI_CREATE_PIPE_EVT:
        {
            ALOGD ("%s: NFA_HCI_CREATE_PIPE_EVT; status=0x%X; pipe=0x%X; src gate=0x%X; dest host=0x%X; dest gate=0x%X", fn,
                    eventData->created.status, eventData->created.pipe, eventData->created.source_gate, eventData->created.dest_host, eventData->created.dest_gate);
            SyncEventGuard guard (sSecElem.mCreatePipeEvent);
            sSecElem.mCommandStatus = eventData->created.status;
#ifdef NXP_EXT
            if (eventData->created.dest_gate == 0xF0)
            {
                ALOGE("Pipe=0x%x created and updated for se transcieve", eventData->created.pipe);
                sSecElem.mNewPipeId = eventData->created.pipe;
            }
#else
            sSecElem.mNewPipeId = eventData->created.pipe;
#endif
            sSecElem.mCreatePipeEvent.notifyOne();
        }
        break;

    case NFA_HCI_OPEN_PIPE_EVT:
        {
            ALOGD ("%s: NFA_HCI_OPEN_PIPE_EVT; status=0x%X; pipe=0x%X", fn, eventData->opened.status, eventData->opened.pipe);
            SyncEventGuard guard (sSecElem.mPipeOpenedEvent);
            sSecElem.mCommandStatus = eventData->opened.status;
            sSecElem.mPipeOpenedEvent.notifyOne();
        }
        break;

    case NFA_HCI_EVENT_SENT_EVT:
        ALOGD ("%s: NFA_HCI_EVENT_SENT_EVT; status=0x%X", fn, eventData->evt_sent.status);
        break;

    case NFA_HCI_RSP_RCVD_EVT: //response received from secure element
        {
            tNFA_HCI_RSP_RCVD& rsp_rcvd = eventData->rsp_rcvd;
            ALOGD ("%s: NFA_HCI_RSP_RCVD_EVT; status: 0x%X; code: 0x%X; pipe: 0x%X; len: %u", fn,
                    rsp_rcvd.status, rsp_rcvd.rsp_code, rsp_rcvd.pipe, rsp_rcvd.rsp_len);
        }
        break;

    case NFA_HCI_GET_REG_RSP_EVT :
        ALOGD ("%s: NFA_HCI_GET_REG_RSP_EVT; status: 0x%X; pipe: 0x%X, len: %d", fn,
                eventData->registry.status, eventData->registry.pipe, eventData->registry.data_len);
        if (eventData->registry.data_len >= 19 && ((eventData->registry.pipe == STATIC_PIPE_0x70) || (eventData->registry.pipe == STATIC_PIPE_0x71)))
        {
            SyncEventGuard guard (sSecElem.mVerInfoEvent);
            // Oberthur OS version is in bytes 16,17, and 18
            sSecElem.mVerInfo[0] = eventData->registry.reg_data[16];
            sSecElem.mVerInfo[1] = eventData->registry.reg_data[17];
            sSecElem.mVerInfo[2] = eventData->registry.reg_data[18];
            sSecElem.mVerInfoEvent.notifyOne ();
        }
        break;

    case NFA_HCI_EVENT_RCVD_EVT:
        ALOGD ("%s: NFA_HCI_EVENT_RCVD_EVT; code: 0x%X; pipe: 0x%X; data len: %u", fn,
                eventData->rcvd_evt.evt_code, eventData->rcvd_evt.pipe,
                eventData->rcvd_evt.evt_len);
#ifdef NXP_EXT
        if (eventData->rcvd_evt.pipe == 0x0A) // UICC
        {
            ALOGD ("%s: NFA_HCI_EVENT_RCVD_EVT; source UICC",fn);
            evtSrc = SecureElement::getInstance().getGenericEseId(EE_HANDLE_0xF4 &
                    ~NFA_HANDLE_GROUP_EE);
        }
        else if (eventData->rcvd_evt.pipe == 0x16) // ESE
        {
            ALOGD ("%s: NFA_HCI_EVENT_RCVD_EVT; source ESE",fn);
            evtSrc = SecureElement::getInstance().getGenericEseId(EE_HANDLE_0xF3 &
                    ~NFA_HANDLE_GROUP_EE);
        }

        ALOGD ("%s: NFA_HCI_EVENT_RCVD_EVT; ################################### ", fn);
#endif
        if ((eventData->rcvd_evt.pipe == STATIC_PIPE_0x70) || (eventData->rcvd_evt.pipe == STATIC_PIPE_0x71))
        {
            ALOGD ("%s: NFA_HCI_EVENT_RCVD_EVT; data from static pipe", fn);
            SyncEventGuard guard (sSecElem.mTransceiveEvent);
            sSecElem.mActualResponseSize = (eventData->rcvd_evt.evt_len > MAX_RESPONSE_SIZE) ? MAX_RESPONSE_SIZE : eventData->rcvd_evt.evt_len;
            sSecElem.mTransceiveEvent.notifyOne ();
        }
        else if (eventData->rcvd_evt.evt_code == NFA_HCI_EVT_POST_DATA)
        {
            ALOGD ("%s: NFA_HCI_EVENT_RCVD_EVT; NFA_HCI_EVT_POST_DATA", fn);
            SyncEventGuard guard (sSecElem.mTransceiveEvent);
            sSecElem.mActualResponseSize = (eventData->rcvd_evt.evt_len > MAX_RESPONSE_SIZE) ? MAX_RESPONSE_SIZE : eventData->rcvd_evt.evt_len;
            sSecElem.mTransceiveEvent.notifyOne ();
        }
        else if (eventData->rcvd_evt.evt_code == NFA_HCI_EVT_TRANSACTION)
        {
            ALOGD ("%s: NFA_HCI_EVENT_RCVD_EVT; NFA_HCI_EVT_TRANSACTION", fn);
            // If we got an AID, notify any listeners
            if ((eventData->rcvd_evt.evt_len > 3) && (eventData->rcvd_evt.p_evt_buf[0] == 0x81))
#ifdef NXP_EXT
            {
                int aidlen = eventData->rcvd_evt.p_evt_buf[1];
                UINT8* data = NULL;
                UINT8 datalen = 0;
                if ((eventData->rcvd_evt.evt_len > 2+aidlen) &&
                        (eventData->rcvd_evt.p_evt_buf[2+aidlen] == 0x82))
                {
                    datalen = eventData->rcvd_evt.p_evt_buf[2+aidlen+1];
                    data  = &eventData->rcvd_evt.p_evt_buf[2+aidlen+2];
                }
                sSecElem.notifyTransactionListenersOfAid (&eventData->rcvd_evt.p_evt_buf[2],
                        aidlen,data,datalen,evtSrc);
            }
        }
        else if (eventData->rcvd_evt.evt_code == NFA_HCI_EVT_CONNECTIVITY)
        {
            ALOGD ("%s: NFA_HCI_EVENT_RCVD_EVT; NFA_HCI_EVT_CONNECTIVITY", fn);
            int pipe = (eventData->rcvd_evt.pipe);
            sSecElem.notifyConnectivityListeners (evtSrc);
        }
        else
        {
            ALOGD ("%s: NFA_HCI_EVENT_RCVD_EVT; ################ eventData->rcvd_evt.evt_code \
                    = 0x%x , NFA_HCI_EVT_CONNECTIVITY = 0x%x", fn, eventData->rcvd_evt.evt_code,
                    NFA_HCI_EVT_CONNECTIVITY);

            ALOGD ("%s: NFA_HCI_EVENT_RCVD_EVT; ################################### ", fn);

        }
#else
                sSecElem.notifyTransactionListenersOfAid (&eventData->rcvd_evt.p_evt_buf[2],
                        eventData->rcvd_evt.p_evt_buf[1]);
        }
#endif
        break;

    case NFA_HCI_SET_REG_RSP_EVT: //received response to write registry command
        {
            tNFA_HCI_REGISTRY& registry = eventData->registry;
            ALOGD ("%s: NFA_HCI_SET_REG_RSP_EVT; status=0x%X; pipe=0x%X", fn,registry.status, registry.pipe);
            SyncEventGuard guard (sSecElem.mRegistryEvent);
            sSecElem.mRegistryEvent.notifyOne ();
            break;
        }

    default:
        ALOGE ("%s: unknown event code=0x%X ????", fn, event);
        break;
    }
}


/*******************************************************************************
**
** Function:        findEeByHandle
**
** Description:     Find information about an execution environment.
**                  eeHandle: Handle to execution environment.
**
** Returns:         Information about an execution environment.
**
*******************************************************************************/
tNFA_EE_INFO *SecureElement::findEeByHandle (tNFA_HANDLE eeHandle)
{
    for (UINT8 xx = 0; xx < mActualNumEe; xx++)
    {
        if (mEeInfo[xx].ee_handle == eeHandle)
            return (&mEeInfo[xx]);
    }
    return (NULL);
}


/*******************************************************************************
**
** Function:        getDefaultEeHandle
**
** Description:     Get the handle to the execution environment.
**
** Returns:         Handle to the execution environment.
**
*******************************************************************************/
tNFA_HANDLE SecureElement::getDefaultEeHandle ()
{
    UINT16 overrideEeHandle = NFA_HANDLE_GROUP_EE | mActiveSeOverride;
    // Find the first EE that is not the HCI Access i/f.
    for (UINT8 xx = 0; xx < mActualNumEe; xx++)
    {
        if (mActiveSeOverride && (overrideEeHandle != mEeInfo[xx].ee_handle))
            continue; //skip all the EE's that are ignored
        if ((mEeInfo[xx].num_interface != 0)
#ifndef NXP_EXT
             &&
            (mEeInfo[xx].ee_interface[0] != NCI_NFCEE_INTERFACE_HCI_ACCESS)
#else
            &&
            (mEeInfo[xx].ee_handle == EE_HANDLE_0xF3 || mEeInfo[xx].ee_handle == EE_HANDLE_0xF4)
#endif
            &&
            (mEeInfo[xx].ee_status != NFC_NFCEE_STATUS_INACTIVE))
            return (mEeInfo[xx].ee_handle);
    }
    return NFA_HANDLE_INVALID;
}


/*******************************************************************************
**
** Function:        findUiccByHandle
**
** Description:     Find information about an execution environment.
**                  eeHandle: Handle of the execution environment.
**
** Returns:         Information about the execution environment.
**
*******************************************************************************/
tNFA_EE_DISCOVER_INFO *SecureElement::findUiccByHandle (tNFA_HANDLE eeHandle)
{
    for (UINT8 index = 0; index < mUiccInfo.num_ee; index++)
    {
        if (mUiccInfo.ee_disc_info[index].ee_handle == eeHandle)
        {
            return (&mUiccInfo.ee_disc_info[index]);
        }
    }
    ALOGE ("SecureElement::findUiccByHandle:  ee h=0x%4x not found", eeHandle);
    return NULL;
}


/*******************************************************************************
**
** Function:        eeStatusToString
**
** Description:     Convert status code to status text.
**                  status: Status code
**
** Returns:         None
**
*******************************************************************************/
const char* SecureElement::eeStatusToString (UINT8 status)
{
    switch (status)
    {
    case NFC_NFCEE_STATUS_ACTIVE:
        return("Connected/Active");
    case NFC_NFCEE_STATUS_INACTIVE:
        return("Connected/Inactive");
    case NFC_NFCEE_STATUS_REMOVED:
        return("Removed");
    }
    return("?? Unknown ??");
}


/*******************************************************************************
**
** Function:        connectionEventHandler
**
** Description:     Receive card-emulation related events from stack.
**                  event: Event code.
**                  eventData: Event data.
**
** Returns:         None
**
*******************************************************************************/
void SecureElement::connectionEventHandler (UINT8 event, tNFA_CONN_EVT_DATA* /*eventData*/)
{
    switch (event)
    {
    case NFA_CE_UICC_LISTEN_CONFIGURED_EVT:
        {
            SyncEventGuard guard (mUiccListenEvent);
            mUiccListenEvent.notifyOne ();
        }
        break;
    }
}


/*******************************************************************************
**
** Function:        routeToSecureElement
**
** Description:     Adjust controller's listen-mode routing table so transactions
**                  are routed to the secure elements.
**
** Returns:         True if ok.
**
*******************************************************************************/
bool SecureElement::routeToSecureElement ()
{
    static const char fn [] = "SecureElement::routeToSecureElement";
    ALOGD ("%s: enter", fn);
    tNFA_STATUS nfaStat = NFA_STATUS_FAILED;
    tNFA_TECHNOLOGY_MASK tech_mask = NFA_TECHNOLOGY_MASK_A | NFA_TECHNOLOGY_MASK_B;
    bool retval = false;

    if (! mIsInit)
    {
        ALOGE ("%s: not init", fn);
        return false;
    }

    if (mCurrentRouteSelection == SecElemRoute)
    {
        ALOGE ("%s: already sec elem route", fn);
        return true;
    }

    if (mActiveEeHandle == NFA_HANDLE_INVALID)
    {
        ALOGE ("%s: invalid EE handle", fn);
        return false;
    }

    adjustRoutes (SecElemRoute);

    {
        unsigned long num = 0;
        if (GetNumValue("UICC_LISTEN_TECH_MASK", &num, sizeof(num)))
            tech_mask = num;
        ALOGD ("%s: start UICC listen; h=0x%X; tech mask=0x%X", fn, mActiveEeHandle, tech_mask);
        SyncEventGuard guard (mUiccListenEvent);
        nfaStat = NFA_CeConfigureUiccListenTech (mActiveEeHandle, tech_mask);
        if (nfaStat == NFA_STATUS_OK)
        {
            mUiccListenEvent.wait ();
            retval = true;
        }
        else
            ALOGE ("%s: fail to start UICC listen", fn);
    }
#ifdef NXP_EXT
    tNFA_EE_INFO* eeinfo = findEeByHandle(mActiveEeHandle);

    if(eeinfo != NULL && eeinfo->la_protocol == 0x00 && eeinfo->lb_protocol != 0x00)
    {
        UINT8 val[] = {0x00};

        ALOGD ("%s: No tech A on EE ", fn);

        ALOGD ("%s: Configure TypeB", __FUNCTION__);
        {
            SyncEventGuard guard (android::sNfaSetConfigEvent);
            nfaStat = NFA_SetConfig(NCI_PARAM_ID_LA_SEL_INFO, sizeof(UINT8), val);
            if (nfaStat == NFA_STATUS_OK)
                android::sNfaSetConfigEvent.wait ();
        }
    }
    else
    {
        UINT8 val[] = {0x60};

        ALOGD ("%s: tech A on EE ", fn);
        {
            SyncEventGuard guard (android::sNfaSetConfigEvent);
            nfaStat = NFA_SetConfig(NCI_PARAM_ID_LA_SEL_INFO, sizeof(UINT8), val);
            if (nfaStat == NFA_STATUS_OK)
                android::sNfaSetConfigEvent.wait ();
        }

    }
#endif
    ALOGD ("%s: exit; ok=%u", fn, retval);
    return retval;
}


/*******************************************************************************
**
** Function:        routeToDefault
**
** Description:     Adjust controller's listen-mode routing table so transactions
**                  are routed to the default destination.
**
** Returns:         True if ok.
**
*******************************************************************************/
bool SecureElement::routeToDefault ()
{
    static const char fn [] = "SecureElement::routeToDefault";
    tNFA_STATUS nfaStat = NFA_STATUS_FAILED;
    bool retval = false;

    ALOGD ("%s: enter", fn);
    if (! mIsInit)
    {
        ALOGE ("%s: not init", fn);
        return false;
    }

    if (mCurrentRouteSelection == DefaultRoute)
    {
        ALOGD ("%s: already default route", fn);
        return true;
    }

#ifdef NXP_EXT
    if (mActiveEeHandle != NFA_HANDLE_INVALID && CEFromHost::getInstance().isEnabled() != true)
#else
    if (mActiveEeHandle != NFA_HANDLE_INVALID)
#endif
    {
        ALOGD ("%s: stop UICC listen; EE h=0x%X", fn, mActiveEeHandle);
        SyncEventGuard guard (mUiccListenEvent);
        nfaStat = NFA_CeConfigureUiccListenTech (mActiveEeHandle, 0);
        if (nfaStat == NFA_STATUS_OK)
        {
            mUiccListenEvent.wait ();
            retval = true;
        }
        else
            ALOGE ("%s: fail to stop UICC listen", fn);
    }
    else
        retval = true;

    adjustRoutes (DefaultRoute);

    ALOGD ("%s: exit; ok=%u", fn, retval);
    return retval;
}


/*******************************************************************************
**
** Function:        isBusy
**
** Description:     Whether controller is routing listen-mode events to
**                  secure elements or a pipe is connected.
**
** Returns:         True if either case is true.
**
*******************************************************************************/
bool SecureElement::isBusy ()
{
    bool retval = (mCurrentRouteSelection == SecElemRoute) || mIsPiping;
    ALOGD ("SecureElement::isBusy: %u", retval);
    return retval;
}

#ifdef NXP_EXT
jint SecureElement::getGenericEseId(tNFA_HANDLE handle)
{
    jint ret = 0xFF;

    //Map the actual handle to generic id
    if(handle == (EE_HANDLE_0xF3 & ~NFA_HANDLE_GROUP_EE) ) //ESE - 0xC0
    {
        ret = 0x01;
    }
    else if(handle ==  (EE_HANDLE_0xF4 & ~NFA_HANDLE_GROUP_EE) ) //UICC - 0x02
    {
        ret = 0x02;
    }

    return ret;
}

tNFA_HANDLE SecureElement::getEseHandleFromGenericId(jint eseId)
{
    UINT16 handle = NFA_HANDLE_INVALID;


    //Map the generic id to actual handle
    if(eseId == 0x01) //ESE
    {
        handle = EE_HANDLE_0xF3; //0x4C0;
    }
    else if(eseId == 0x02) //UICC
    {
        handle = EE_HANDLE_0xF4; //0x402;
    }
    else if(eseId == 0x04)
    {
        handle = NFA_EE_HANDLE_DH; //0x400;
    }
    return handle;
}
bool SecureElement::SecEle_Modeset(UINT8 type)
{
    tNFA_STATUS nfaStat = NFA_STATUS_FAILED;
    bool retval = true;

    SyncEventGuard guard (mEeSetModeEvent);
    ALOGD ("set EE mode = 0x%X", type);
    if ((nfaStat = NFA_EeModeSet (0x4C0, type)) == NFA_STATUS_OK)
    {
        mEeSetModeEvent.wait ();
    }
    else
    {
        retval = false;
        ALOGE ("NFA_EeModeSet failed; error=0x%X",nfaStat);
    }
    return retval;
}
#endif

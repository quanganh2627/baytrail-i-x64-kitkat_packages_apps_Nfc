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

extern "C"
{
    #include "nfa_api.h"
    #include "nfa_ee_api.h"
    #include "ce_api.h"
}/*namespace android*/

class CEFromHost
{
public:

    static uint8_t CE_TYPE_A;
    static uint8_t CE_TYPE_B;
    /*******************************************************************************
    **
    ** Function:        getInstance
    **
    ** Description:     Get the CEFromHost singleton object.
    **
    ** Returns:         SecureElement object.
    **
    *******************************************************************************/
    static CEFromHost& getInstance ();


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
    bool initialize (nfc_jni_native_data* native);


    /*******************************************************************************
    **
    ** Function:        finalize
    **
    ** Description:     Release all resources.
    **
    ** Returns:         None
    **
    *******************************************************************************/
    void finalize ();

    /*******************************************************************************
    **
    ** Function:        notifyListenModeState
    **
    ** Description:     Notify the NFC service about whether the CEFH link is active
    **                  in listen mode.
    **                  isActive: Whether the CEFH link is activated/deactivated.
    **
    ** Returns:         None
    **
    *******************************************************************************/
    void notifyCEFHLinkState (bool isActive);

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
    bool send(uint8_t data[],uint16_t len);

    bool receive(uint8_t** data, uint16_t* len);

    bool isEnabled();
    bool isEnabled(uint8_t type);

    void setEnabled(uint8_t type, bool enabled);

    void connectionEventHandler (UINT8 event, tNFA_CONN_EVT_DATA* eventData);

    bool enableCEType(uint8_t tech);

    bool registerCEEventsListener(tNFA_CONN_CBACK *cback);
    bool deRegisterCEEventsListener();

    bool configLocalTag(uint8_t* appdata);

private:


    nfc_jni_native_data* mNativeData;
    static CEFromHost sCefh;
    bool mIsInit;                // whether CEFH is initialized
    Mutex mMutex; // protects fields below
    bool mIsLinkActive; // last known CEFH link state
    bool mIsEnabledA;
    bool mIsEnabledB;
    SyncEvent mUiccListenEvent;
    SyncEvent mAidRegisterEvent;
    SyncEvent mAidDegisterEvent;
    SyncEvent mSetConfigTagEvent;
    SyncEvent mDataRecvEvent;

    uint8_t* mRecvData;
    uint16_t mRecvLen;
    uint8_t mTechMask;
    tNFA_HANDLE mAidHandle;
    Mutex mDataMutex; // protects fields below
    bool mIsNewData;

    /*******************************************************************************
    **
    ** Function:        CEFromHost
    **
    ** Description:     Initialize member variables.
    **
    ** Returns:         None
    **
    *******************************************************************************/
    CEFromHost ();


    /*******************************************************************************
    **
    ** Function:        ~CEFromHost
    **
    ** Description:     Release all resources.
    **
    ** Returns:         None
    **
    *******************************************************************************/
    ~CEFromHost ();



};

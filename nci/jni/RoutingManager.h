/*
 * Copyright (C) 2013 The Android Open Source Project
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

/*
 *  Manage the listen-mode routing table.
 */
#pragma once
#include "SyncEvent.h"
#include "NfcJniUtil.h"
#include "RouteDataSet.h"
#include "SecureElement.h"
#include <vector>
extern "C"
{
    #include "nfa_api.h"
    #include "nfa_ee_api.h"
}

class RoutingManager
{
public:
    static const int ROUTE_HOST = 0;
    static const int ROUTE_ESE = 1;

    static RoutingManager& getInstance ();
    bool initialize(nfc_jni_native_data* native);
#if (NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
    void setDefaultTechRouting (int seId, int tech_switchon,int tech_switchoff);
    void setDefaultProtoRouting (int seId, int proto_switchon,int proto_switchoff);
#endif
#if(NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
    bool addAidRouting(const UINT8* aid, UINT8 aidLen, int route, int power);
    void clearAidRouting();
#else
    bool addAidRouting(const UINT8* aid, UINT8 aidLen, int route);
    bool removeAidRouting(const UINT8* aid, UINT8 aidLen);
#endif
    bool commitRouting();
#if (NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
    void setRouting(bool);
#endif
private:
    RoutingManager();
    ~RoutingManager();
    RoutingManager(const RoutingManager&);
    RoutingManager& operator=(const RoutingManager&);

#if (NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
    void cleanRouting();
#else
    void setDefaultRouting();
#endif
    void handleData (const UINT8* data, UINT16 dataLen);
    void notifyActivated ();
    void notifyDeactivated ();
    static void nfaEeCallback (tNFA_EE_EVT event, tNFA_EE_CBACK_DATA* eventData);
    static void stackCallback (UINT8 event, tNFA_CONN_EVT_DATA* eventData);

    // Fields below are final after initialize()
    nfc_jni_native_data* mNativeData;
    int mDefaultEe;
#if (NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
    bool mIsDirty;
#endif
    SyncEvent mEeRegisterEvent;
    SyncEvent mRoutingEvent;
};


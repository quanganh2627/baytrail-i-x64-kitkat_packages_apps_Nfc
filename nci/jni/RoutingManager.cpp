
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

#include <cutils/log.h>
#include <ScopedLocalRef.h>
#include "config.h"
#include "JavaClassConstants.h"
#include "RoutingManager.h"

#if (NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
extern "C"
{
    #include "phNxpConfig.h"
}

namespace android
{
extern  void  checkforTranscation(UINT8 connEvent, void* eventData );
}

void *stop_reader_event_handler_async(void *data);
void *reader_event_handler_async(void *data);
#endif

RoutingManager::RoutingManager ()
:   mNativeData(NULL)
{
}

RoutingManager::~RoutingManager ()
{
    NFA_EeDeregister (nfaEeCallback);
}

bool RoutingManager::initialize (nfc_jni_native_data* native)
{
    static const char fn [] = "RoutingManager::initialize()";
    unsigned long num = 0;
    mNativeData = native;

#if (NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
     mIsDirty = true;

    if (GetNxpNumValue (NAME_NXP_DEFAULT_SE, (void*)&num, sizeof(num)))
    {
        ALOGD ("%d: nfcManager_GetDefaultSE", num);
        mDefaultEe = num;
    }
    else
    {
        mDefaultEe = 0;
    }
#endif

    tNFA_STATUS nfaStat;
    {
        SyncEventGuard guard (mEeRegisterEvent);
        ALOGD ("%s: try ee register", fn);
        nfaStat = NFA_EeRegister (nfaEeCallback);
        if (nfaStat != NFA_STATUS_OK)
        {
            ALOGE ("%s: fail ee register; error=0x%X", fn, nfaStat);
            return false;
        }
        mEeRegisterEvent.wait ();
    }

#if (NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
    // Tell the host-routing to only listen on Nfc-A/Nfc-B
    nfaStat = NFA_CeSetIsoDepListenTech(NFA_TECHNOLOGY_MASK_A | NFA_TECHNOLOGY_MASK_B);
    if (nfaStat != NFA_STATUS_OK)
        ALOGE ("Failed to configure CE IsoDep technologies");
        // Register a wild-card for AIDs routed to the host
        nfaStat = NFA_CeRegisterAidOnDH (NULL, 0, stackCallback);
        if (nfaStat != NFA_STATUS_OK)
            ALOGE("Failed to register wildcard AID for DH");
#else
    // Get the "default" route
    if (GetNumValue("DEFAULT_ISODEP_ROUTE", &num, sizeof(num)))
        mDefaultEe = num;
    else
        mDefaultEe = 0x00;

    ALOGD("%s: default route is 0x%02X", fn, mDefaultEe);
    setDefaultRouting();
#endif
    return true;
}

RoutingManager& RoutingManager::getInstance ()
{
    static RoutingManager manager;
    return manager;
}

#if (NFC_NXP_NOT_OPEN_INCLUDED == TRUE)

void RoutingManager::cleanRouting()
{
    tNFA_STATUS nfaStat;
    tNFA_HANDLE seHandle = NFA_HANDLE_INVALID;
    tNFA_HANDLE ee_handleList[SecureElement::MAX_NUM_EE];
    UINT8 i, count;
    static const char fn [] = "SecureElement::cleanRouting";

    SecureElement::getInstance().getEeHandleList(ee_handleList, &count);

    if(count > (SecureElement::MAX_NUM_EE))
    {
        count =  SecureElement::MAX_NUM_EE;
        ALOGW("%s: Number of ee_HandleList limited to %d", fn, count);
    }

    for ( i = 0; i < count; i++)
    {
        nfaStat =  NFA_EeSetDefaultTechRouting(ee_handleList[i],0,0,0);
        if (nfaStat == NFA_STATUS_OK)
        {
            mRoutingEvent.wait ();
        }
        nfaStat =  NFA_EeSetDefaultProtoRouting(ee_handleList[i],0,0,0);
        if (nfaStat == NFA_STATUS_OK)
        {
            mRoutingEvent.wait ();
        }
    }
    //clean HOST
    nfaStat =  NFA_EeSetDefaultTechRouting(NFA_EE_HANDLE_DH,0,0,0);
    if (nfaStat == NFA_STATUS_OK)
    {
        mRoutingEvent.wait ();
    }
    nfaStat =  NFA_EeSetDefaultProtoRouting(NFA_EE_HANDLE_DH,0,0,0);
    if (nfaStat == NFA_STATUS_OK)
    {
        mRoutingEvent.wait ();
    }
    nfaStat = NFA_EeUpdateNow();
    if (nfaStat != NFA_STATUS_OK)
        ALOGE("Failed to commit routing configuration");

}
#endif

#if(NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
void RoutingManager::setRouting(bool isHCEEnabled)
{
    tNFA_STATUS nfaStat;
    tNFA_HANDLE ee_handle = NFA_EE_HANDLE_DH;
    UINT8 i, count;
    static const char fn [] = "SecureElement::setRouting";
    unsigned long num = 0;

    if (!mIsDirty)
    {
        return;
    }
    mIsDirty = false;
    SyncEventGuard guard (mRoutingEvent);
    ALOGE("Inside %s mDefaultEe %d isHCEEnabled %d ee_handle :0x%x", fn, mDefaultEe, isHCEEnabled,ee_handle);
    if (mDefaultEe == 0x01) //eSE
    {
        ee_handle = 0x4C0;
        num = NFA_TECHNOLOGY_MASK_A;
        ALOGD ("%s:ESE_LISTEN_MASK=0x0%d;", __FUNCTION__, num);
    }
    else if (mDefaultEe == 0x02) //UICC
    {
        ee_handle = 0x402;
        if ((GetNumValue(NAME_UICC_LISTEN_TECH_MASK, &num, sizeof(num))))
        {
            ALOGD ("%s:UICC_LISTEN_MASK=0x0%d;", __FUNCTION__, num);
            }
/*
                SyncEventGuard guard (SecureElement::getInstance().mUiccListenEvent);
        nfaStat = NFA_CeConfigureUiccListenTech (ee_handle, (num & 0x07));
                if (nfaStat == NFA_STATUS_OK)
                {
                    SecureElement::getInstance().mUiccListenEvent.wait ();
                }
                else
                    ALOGE ("fail to start UICC listen");
*/
            }

    {
        // Default routing for IsoDep protocol
        nfaStat = NFA_EeSetDefaultProtoRouting(NFA_EE_HANDLE_DH, NFA_PROTOCOL_MASK_ISO_DEP, 0, 0);
        if (nfaStat == NFA_STATUS_OK)
            mRoutingEvent.wait ();
        else
        {
            ALOGE ("Fail to set default proto routing");
        }

        nfaStat =  NFA_EeSetDefaultTechRouting(ee_handle, num & 0x07, num & 0x07, num & 0x07);
        if (nfaStat == NFA_STATUS_OK)
        {
            mRoutingEvent.wait ();
        }

        SyncEventGuard guard (SecureElement::getInstance().mUiccListenEvent);
        nfaStat = NFA_CeConfigureUiccListenTech (ee_handle, (num & 0x07));
        if (nfaStat == NFA_STATUS_OK)
        {
            SecureElement::getInstance().mUiccListenEvent.wait ();
        }
        else
            ALOGE ("fail to start UICC listen");
    }

    // Commit the routing configuration
    nfaStat = NFA_EeUpdateNow();
    if (nfaStat != NFA_STATUS_OK)
        ALOGE("Failed to commit routing configuration");
}
#else
void RoutingManager::setDefaultRouting()
{
    tNFA_STATUS nfaStat;
    SyncEventGuard guard (mRoutingEvent);
    // Default routing for NFC-A technology
    nfaStat = NFA_EeSetDefaultTechRouting (mDefaultEe, 0x01, 0, 0);
    if (nfaStat == NFA_STATUS_OK)
        mRoutingEvent.wait ();
    else
        ALOGE ("Fail to set default tech routing");

    // Default routing for IsoDep protocol
    nfaStat = NFA_EeSetDefaultProtoRouting(mDefaultEe, NFA_PROTOCOL_MASK_ISO_DEP, 0, 0);
    if (nfaStat == NFA_STATUS_OK)
        mRoutingEvent.wait ();
    else
        ALOGE ("Fail to set default proto routing");

    // Tell the UICC to only listen on Nfc-A
    nfaStat = NFA_CeConfigureUiccListenTech (mDefaultEe, 0x01);
    if (nfaStat != NFA_STATUS_OK)
        ALOGE ("Failed to configure UICC listen technologies");

    // Tell the host-routing to only listen on Nfc-A
    nfaStat = NFA_CeSetIsoDepListenTech(0x01);
    if (nfaStat != NFA_STATUS_OK)
        ALOGE ("Failed to configure CE IsoDep technologies");

    // Register a wild-card for AIDs routed to the host
    nfaStat = NFA_CeRegisterAidOnDH (NULL, 0, stackCallback);
    if (nfaStat != NFA_STATUS_OK)
        ALOGE("Failed to register wildcard AID for DH");

    // Commit the routing configuration
    nfaStat = NFA_EeUpdateNow();
    if (nfaStat != NFA_STATUS_OK)
        ALOGE("Failed to commit routing configuration");
}
#endif

#if (NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
bool RoutingManager::addAidRouting(const UINT8* aid, UINT8 aidLen, int route, int power)
#else
bool RoutingManager::addAidRouting(const UINT8* aid, UINT8 aidLen, int route)
#endif
{
    static const char fn [] = "RoutingManager::addAidRouting";
    ALOGD ("%s: enter", fn);
#if (NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
    tNFA_HANDLE handle;
    tNFA_HANDLE current_handle;
    ALOGD ("%s: enter, route:%x power:0x%x", fn, route,power);
    if (route == 0)
    {
        handle = NFA_EE_HANDLE_DH;
    }
    else
    {
        handle = SecureElement::getInstance().getEseHandleFromGenericId(route);
    }

    ALOGD ("%s: enter, route:%x", fn, handle);
    if (handle  == NFA_HANDLE_INVALID)
    {
        return false;
    }

    current_handle = ((handle == 0x4C0) ? 0x01 : 0x02);
    if (handle == 0x400)
        current_handle = 0x00;

    ALOGD ("%s: enter, mDefaultEe:%x", fn, current_handle);
    SecureElement::getInstance().activate(current_handle);
    if (power == NFA_EE_PWR_STATE_NONE)
    {
        power = NFA_EE_PWR_STATE_ON;
    }
    SyncEventGuard guard(SecureElement::getInstance().mAidAddRemoveEvent);
    tNFA_STATUS nfaStat = NFA_EeAddAidRouting(handle, aidLen, (UINT8*) aid, power);
#else
    tNFA_STATUS nfaStat = NFA_EeAddAidRouting(route, aidLen, (UINT8*) aid, 0x01);
#endif
    if (nfaStat == NFA_STATUS_OK)
    {
        ALOGD ("%s: routed AID", fn);
#if (NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
        mIsDirty = true;
        SecureElement::getInstance().mAidAddRemoveEvent.wait();
#endif
        return true;
    } else
    {
        ALOGE ("%s: failed to route AID", fn);
        return false;
    }
}

#if(NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
void RoutingManager::clearAidRouting()
{
	static const char fn [] = "RoutingManager::clearAidRouting";
    ALOGD ("%s: enter", fn);
    SyncEventGuard guard(SecureElement::getInstance().mAidAddRemoveEvent);
    tNFA_STATUS nfaStat = NFA_EeRemoveAidRouting(NFA_REMOVE_ALL_AID_LEN, NFA_REMOVE_ALL_AID);
    if (nfaStat == NFA_STATUS_OK)
    {
        SecureElement::getInstance().mAidAddRemoveEvent.wait();
    }
    else
     {
        ALOGE ("%s: failed to clear AID");
    }
    mIsDirty = true;
}

#else
bool RoutingManager::removeAidRouting(const UINT8* aid, UINT8 aidLen)
{
    static const char fn [] = "RoutingManager::removeAidRouting";
    ALOGD ("%s: enter", fn);
    tNFA_STATUS nfaStat = NFA_EeRemoveAidRouting(aidLen, (UINT8*) aid);
    if (nfaStat == NFA_STATUS_OK)
    {
        ALOGD ("%s: removed AID", fn);
        return true;
    } else
    {
        ALOGE ("%s: failed to remove AID");
        return false;
    }
}
#endif

#if(NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
void RoutingManager::setDefaultTechRouting (int seId, int tech_switchon,int tech_switchoff)
{
    ALOGD ("ENTER setDefaultTechRouting");
    tNFA_STATUS nfaStat;
    SyncEventGuard guard (mRoutingEvent);

    nfaStat = NFA_EeSetDefaultTechRouting (seId, tech_switchon, tech_switchoff, 0);
    if(nfaStat == NFA_STATUS_OK){
        mRoutingEvent.wait ();
        ALOGD ("tech routing SUCCESS");
    }
    else{
        ALOGE ("Fail to set default tech routing");
    }
    nfaStat = NFA_EeUpdateNow();
    if (nfaStat != NFA_STATUS_OK){
        ALOGE("Failed to commit routing configuration");
    }
}

void RoutingManager::setDefaultProtoRouting (int seId, int proto_switchon,int proto_switchoff)
{
    tNFA_STATUS nfaStat;
    ALOGD ("ENTER setDefaultProtoRouting");
    SyncEventGuard guard (mRoutingEvent);

    nfaStat = NFA_EeSetDefaultProtoRouting (seId, proto_switchon, proto_switchoff, 0);
    if(nfaStat == NFA_STATUS_OK){
        mRoutingEvent.wait ();
        ALOGD ("proto routing SUCCESS");
    }
    else{
        ALOGE ("Fail to set default proto routing");
    }
    nfaStat = NFA_EeUpdateNow();
    if (nfaStat != NFA_STATUS_OK){
        ALOGE("Failed to commit routing configuration");
    }
}
#endif

bool RoutingManager::commitRouting()
{
    tNFA_STATUS nfaStat = NFA_EeUpdateNow();
    return (nfaStat == NFA_STATUS_OK);
}

void RoutingManager::notifyActivated ()
{
    JNIEnv* e = NULL;
    ScopedAttach attach(mNativeData->vm, &e);
    if (e == NULL)
    {
        ALOGE ("jni env is null");
        return;
    }

    e->CallVoidMethod (mNativeData->manager, android::gCachedNfcManagerNotifyHostEmuActivated);
    if (e->ExceptionCheck())
    {
        e->ExceptionClear();
        ALOGE ("fail notify");
    }
}

void RoutingManager::notifyDeactivated ()
{
    SecureElement::getInstance().notifyListenModeState (false);

    JNIEnv* e = NULL;
    ScopedAttach attach(mNativeData->vm, &e);
    if (e == NULL)
    {
        ALOGE ("jni env is null");
        return;
    }

    e->CallVoidMethod (mNativeData->manager, android::gCachedNfcManagerNotifyHostEmuDeactivated);
    if (e->ExceptionCheck())
    {
        e->ExceptionClear();
        ALOGE ("fail notify");
    }
}

void RoutingManager::handleData (const UINT8* data, UINT16 dataLen)
{
    if (dataLen <= 0)
    {
        ALOGE("no data");
        return;
    }

    JNIEnv* e = NULL;
    ScopedAttach attach(mNativeData->vm, &e);
    if (e == NULL)
    {
        ALOGE ("jni env is null");
        return;
    }

    ScopedLocalRef<jobject> dataJavaArray(e, e->NewByteArray(dataLen));
    if (dataJavaArray.get() == NULL)
    {
        ALOGE ("fail allocate array");
        return;
    }

    e->SetByteArrayRegion ((jbyteArray)dataJavaArray.get(), 0, dataLen, (jbyte *)data);
    if (e->ExceptionCheck())
    {
        e->ExceptionClear();
        ALOGE ("fail fill array");
        return;
    }

    e->CallVoidMethod (mNativeData->manager, android::gCachedNfcManagerNotifyHostEmuData, dataJavaArray.get());
    if (e->ExceptionCheck())
    {
        e->ExceptionClear();
        ALOGE ("fail notify");
    }
}

void RoutingManager::stackCallback (UINT8 event, tNFA_CONN_EVT_DATA* eventData)
{
    static const char fn [] = "RoutingManager::stackCallback";
    ALOGD("%s: event=0x%X", fn, event);
    RoutingManager& routingManager = RoutingManager::getInstance();

    switch (event)
    {
    case NFA_CE_REGISTERED_EVT:
        {
            tNFA_CE_REGISTERED& ce_registered = eventData->ce_registered;
            ALOGD("%s: NFA_CE_REGISTERED_EVT; status=0x%X; h=0x%X", fn, ce_registered.status, ce_registered.handle);
        }
        break;

    case NFA_CE_DEREGISTERED_EVT:
        {
            tNFA_CE_DEREGISTERED& ce_deregistered = eventData->ce_deregistered;
            ALOGD("%s: NFA_CE_DEREGISTERED_EVT; h=0x%X", fn, ce_deregistered.handle);
        }
        break;

    case NFA_CE_ACTIVATED_EVT:
        {
            routingManager.notifyActivated();
        }
        break;
    case NFA_DEACTIVATED_EVT:
    case NFA_CE_DEACTIVATED_EVT:
        {
            routingManager.notifyDeactivated();
        }
        break;
    case NFA_CE_DATA_EVT:
        {
            tNFA_CE_DATA& ce_data = eventData->ce_data;
            ALOGD("%s: NFA_CE_DATA_EVT; h=0x%X; data len=%u", fn, ce_data.handle, ce_data.len);
            getInstance().handleData(ce_data.p_data, ce_data.len);
        }
        break;
    }

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
void RoutingManager::nfaEeCallback (tNFA_EE_EVT event, tNFA_EE_CBACK_DATA* eventData)
{
    static const char fn [] = "RoutingManager::nfaEeCallback";

    SecureElement& se = SecureElement::getInstance();
    RoutingManager& routingManager = RoutingManager::getInstance();
#if (NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
    tNFA_EE_DISCOVER_REQ info = eventData->discover_req;
#endif

    switch (event)
    {
    case NFA_EE_REGISTER_EVT:
        {
            SyncEventGuard guard (routingManager.mEeRegisterEvent);
            ALOGD ("%s: NFA_EE_REGISTER_EVT; status=%u", fn, eventData->ee_register);
            routingManager.mEeRegisterEvent.notifyOne();
        }
        break;

    case NFA_EE_MODE_SET_EVT:
        {
            ALOGD ("%s: NFA_EE_MODE_SET_EVT; status: 0x%04X  handle: 0x%04X  mActiveEeHandle: 0x%04X", fn,
                    eventData->mode_set.status, eventData->mode_set.ee_handle, se.mActiveEeHandle);
#if (NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
            se.notifyModeSet(eventData->mode_set.ee_handle, !(eventData->mode_set.status),eventData->mode_set.ee_status );
#else
            se.notifyModeSet(eventData->mode_set.ee_handle, eventData->mode_set.status);
#endif
        }
        break;

    case NFA_EE_SET_TECH_CFG_EVT:
        {
            ALOGD ("%s: NFA_EE_SET_TECH_CFG_EVT; status=0x%X", fn, eventData->status);
            SyncEventGuard guard(routingManager.mRoutingEvent);
            routingManager.mRoutingEvent.notifyOne();
        }
        break;

    case NFA_EE_SET_PROTO_CFG_EVT:
        {
            ALOGD ("%s: NFA_EE_SET_PROTO_CFG_EVT; status=0x%X", fn, eventData->status);
            SyncEventGuard guard(routingManager.mRoutingEvent);
            routingManager.mRoutingEvent.notifyOne();
        }
        break;

    case NFA_EE_ACTION_EVT:
        {
            tNFA_EE_ACTION& action = eventData->action;
#if (NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
            android::checkforTranscation(NFA_EE_ACTION_EVT, (void *)eventData);
#endif
            if (action.trigger == NFC_EE_TRIG_SELECT)
                ALOGD ("%s: NFA_EE_ACTION_EVT; h=0x%X; trigger=select (0x%X)", fn, action.ee_handle, action.trigger);
            else if (action.trigger == NFC_EE_TRIG_APP_INIT)
            {
                tNFC_APP_INIT& app_init = action.param.app_init;
                ALOGD ("%s: NFA_EE_ACTION_EVT; h=0x%X; trigger=app-init (0x%X); aid len=%u; data len=%u", fn,
                        action.ee_handle, action.trigger, app_init.len_aid, app_init.len_data);
                // if app-init operation is successful;
                // app_init.data[] contains two bytes, which are the status codes of the event;
                // app_init.data[] does not contain an APDU response;
                // see EMV Contactless Specification for Payment Systems; Book B; Entry Point Specification;
                // version 2.1; March 2011; section 3.3.3.5;
                if ( (app_init.len_data > 1) &&
                     (app_init.data[0] == 0x90) &&
                     (app_init.data[1] == 0x00) )
                {
#if (NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
                    se.notifyTransactionListenersOfAid (app_init.aid, app_init.len_aid, app_init.data, app_init.len_data, SecureElement::getInstance().getGenericEseId(action.ee_handle));
#else
                    se.notifyTransactionListenersOfAid (app_init.aid, app_init.len_aid);
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
#if (NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
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
            // for each technology (A, B, F, B'), print the bit field that shows
            // what protocol(s) is support by that technology
            ALOGD ("%s   EE[%u] Handle: 0x%04x  PA: 0x%02x  PB: 0x%02x",
                    fn, xx, info.ee_disc_info[xx].ee_handle,
                    info.ee_disc_info[xx].pa_protocol,
                    info.ee_disc_info[xx].pb_protocol);

            if (info.ee_disc_info[xx].pa_protocol ==  0x04 || info.ee_disc_info[xx].pb_protocol == 0x04)
            {
                ALOGD ("%s NFA_RD_SWP_READER_REQUESTED  EE[%u] Handle: 0x%04x  PA: 0x%02x  PB: 0x%02x",
                        fn, xx, info.ee_disc_info[xx].ee_handle,
                        info.ee_disc_info[xx].pa_protocol,
                        info.ee_disc_info[xx].pb_protocol);

                rd_swp_req_t *data = (rd_swp_req_t*)malloc(sizeof(rd_swp_req_t));

                if(data == NULL)
                {
                     ALOGE ("%s: NFA_EE_DISCOVER_REQ_EVT; no memory for rd_swp_req_t data*", __FUNCTION__);
                     break;
                }

                data->tech_mask = 0x00;
                if (info.ee_disc_info[xx].pa_protocol !=  0)
                    data->tech_mask |= NFA_TECHNOLOGY_MASK_A;
                if (info.ee_disc_info[xx].pb_protocol !=  0)
                    data->tech_mask |= NFA_TECHNOLOGY_MASK_B;

                data->src = info.ee_disc_info[xx].ee_handle;
                pthread_t thread;
                pthread_create (&thread, NULL,  &reader_event_handler_async, (void*)data);
                // Reader over SWP - Reader Requested.
                // se.handleEEReaderEvent(NFA_RD_SWP_READER_REQUESTED, tech, info.ee_disc_info[xx].ee_handle);
                break;
            }
            else if (info.ee_disc_info[xx].pa_protocol ==  0xFF || info.ee_disc_info[xx].pb_protocol == 0xFF)
            {
                ALOGD ("%s NFA_RD_SWP_READER_STOP  EE[%u] Handle: 0x%04x  PA: 0x%02x  PB: 0x%02x",
                        fn, xx, info.ee_disc_info[xx].ee_handle,
                        info.ee_disc_info[xx].pa_protocol,
                        info.ee_disc_info[xx].pb_protocol);
                rd_swp_req_t *data = (rd_swp_req_t*)malloc(sizeof(rd_swp_req_t));

                if(data == NULL)
                {
                     ALOGE ("%s: NFA_EE_DISCOVER_REQ_EVT; no memory for rd_swp_req_t data*", __FUNCTION__);
                     break;
                }

                data->tech_mask = 0x00;
                data->src = info.ee_disc_info[xx].ee_handle;

                // Reader over SWP - Stop Reader Requested.
                // se.handleEEReaderEvent(NFA_RD_SWP_READER_STOP, 0x00,info.ee_disc_info[xx].ee_handle);
                pthread_t thread;
                pthread_create (&thread, NULL,  &stop_reader_event_handler_async, (void*)data);
                break;
            }
        }
        /* Set the configuration for UICC/ESE */
        se.storeUiccInfo (eventData->discover_req);
#endif
        break;

    case NFA_EE_NO_CB_ERR_EVT:
        ALOGD ("%s: NFA_EE_NO_CB_ERR_EVT  status=%u", fn, eventData->status);
        break;

    case NFA_EE_ADD_AID_EVT:
        {
            ALOGD ("%s: NFA_EE_ADD_AID_EVT  status=%u", fn, eventData->status);
#if (NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
            SyncEventGuard guard(se.mAidAddRemoveEvent);
            se.mAidAddRemoveEvent.notifyOne();
#endif
        }
        break;

    case NFA_EE_REMOVE_AID_EVT:
        {
            ALOGD ("%s: NFA_EE_REMOVE_AID_EVT  status=%u", fn, eventData->status);
#if (NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
            SyncEventGuard guard(se.mAidAddRemoveEvent);
            se.mAidAddRemoveEvent.notifyOne();
#endif
        }
        break;

    case NFA_EE_NEW_EE_EVT:
        {
            ALOGD ("%s: NFA_EE_NEW_EE_EVT  h=0x%X; status=%u", fn,
                eventData->new_ee.ee_handle, eventData->new_ee.ee_status);
        }
        break;
#if (NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
    case NFA_EE_ROUT_ERR_EVT:
        {
            ALOGD ("%s: NFA_EE_ROUT_ERR_EVT  status=%u", fn,eventData->status);
        }
        break;
#endif
    default:
        ALOGE ("%s: unknown event=%u ????", fn, event);
        break;
    }
}
#if (NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
void *reader_event_handler_async(void *data)
{
    rd_swp_req_t *cbData = (rd_swp_req_t *) data;
    SecureElement::getInstance().handleEEReaderEvent(NFA_RD_SWP_READER_REQUESTED, cbData->tech_mask, cbData->src);
    free(cbData);

    return NULL;
}

void *stop_reader_event_handler_async(void *data)
{
    rd_swp_req_t *cbData = (rd_swp_req_t *) data;
    SecureElement::getInstance().handleEEReaderEvent(NFA_RD_SWP_READER_STOP, cbData->tech_mask, cbData->src);
    free(cbData);
    return NULL;
}
#endif


/*
 * Copyright (C) 2011 The Android Open Source Project
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
package com.android.nfc;

import java.lang.reflect.Proxy;
import java.lang.reflect.Method;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.InvocationTargetException;
import java.util.List;

import android.content.Context;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.content.Intent;
import android.util.Log;

public class SmartcardProxy {
    final static public String READER_UICC = "SIM - UICC";
    final static public String READER_ESE  = "eSE - SmartMX";

    final static private String TAG = "SmartcardProxy";
    final static private String SMARTCARD_LIBRARY = "org.simalliance.openmobileapi";

    final private Context mContext;

    private Object  mSEService = null;
    private boolean mIsConnected = false;
    private Method  mSEService_isNFCEventAllowed = null;

    public SmartcardProxy(Context context) {
        mContext = context;
        try {
            ClassLoader cl  = mContext.getClassLoader();
            Class<?>    cSEService = cl.loadClass(SMARTCARD_LIBRARY + ".SEService");
            Class<?>    cCallback  = cl.loadClass(SMARTCARD_LIBRARY + ".SEService$CallBack");
            Object      oCallback  = Proxy.newProxyInstance(cl, new Class[] {cCallback} , new CallbackProxyListener());

            mSEService = cSEService.getConstructor(new Class[] {Context.class, cCallback })
                 .newInstance(new Object[] {context, oCallback });

            mSEService_isNFCEventAllowed = cSEService.getDeclaredMethod("isNFCEventAllowed",
                 new Class[] {String.class, byte[].class, String[].class});

            Log.i(TAG, "Smartcard proxy sucessfully initialized");
        } catch (ClassNotFoundException e) {
            Log.i(TAG, "Package " + SMARTCARD_LIBRARY + " is not installed: NFC event enforcer is disbaled");
            Log.i(TAG, "Exception:" + e);
        } catch (NoSuchMethodException e) {
            Log.e(TAG, "No such method exception: " + e);
        } catch (InstantiationException e) {
            Log.e(TAG, "Instantiation exception: " + e);
        } catch (IllegalAccessException e) {
            Log.e(TAG, "Illegal access exception: " + e);
        } catch (InvocationTargetException e) {
            Log.e(TAG, "Invocation Target Exception: " + e);
        }
    }

    public boolean isSmartcardServiceAvailable() {
        return (mSEService != null);
    }

    public boolean[] checkForNfcEvent(String se, String[] packages, byte[] aid) {
        if((mSEService == null) || !mIsConnected || mSEService_isNFCEventAllowed == null)
                throw new RuntimeException("Cannot check for NFC event: SEService is not available");
        try {
            Log.i(TAG, "Invoking isNFCEventAllowed()");
            return (boolean[]) mSEService_isNFCEventAllowed.invoke(mSEService, new Object[] {se, aid, packages});
        } catch (IllegalAccessException e) {
            Log.e(TAG, "Illegal access exception: " + e);
            throw new RuntimeException("Cannot check for NFC event: unexpected error:" + e );
        } catch (InvocationTargetException e) {
            Log.e(TAG, "Invocation Target Exception: " + e);
            throw new RuntimeException("Cannot check for NFC event: unexpected error:" + e );
        }
    }

    public void sendNfcEventBroadcast(Intent intent, byte[] aid, String reader) {
        // Retreive the list of packages which registered themselves
        // to receive this intent.
        PackageManager pm = mContext.getPackageManager();
        List<ResolveInfo> list =
            pm.queryBroadcastReceivers(intent, PackageManager.MATCH_DEFAULT_ONLY);

            if (list.size() == 0) {
                Log.i(TAG, "NO application to handle this NFC event");
            }
            else {
                // Build the list of packages interrested in receiving this intent
                String[] packages = new String[list.size()];

                for (int i = 0; i < list.size(); i++) {
                    ResolveInfo resolveInfo = list.get(i);
                    packages[i] = resolveInfo.activityInfo.applicationInfo.packageName;
                }

                try {
                    // Get access rigths for all the packages of the list
                    boolean[] access = checkForNfcEvent(reader, packages, aid);

                    // Send the intent only to the packages which have been authorized
                    for(int i = 0; i < packages.length; i++) {
                    if(access[i]) {
                        Log.i(TAG, "Send NFC event to: " + packages[i]);
                        intent.setPackage(packages[i]);
                        mContext.sendBroadcast(intent);
                    }
                    else {
                        Log.i(TAG, "Permission denied, NFC event is NOT sent to: " + packages[i]);
                    }
                }
            }
            catch(SecurityException e) {
                Log.e(TAG, "Got SecurityException while trying to send NFC event: " + e);
            }
            catch(RuntimeException e) {
                Log.e(TAG, "Got RuntimeException while trying to send NFC event: " + e);
            }
        }
    }

    class CallbackProxyListener implements InvocationHandler {
        public CallbackProxyListener() {
        }

        public Object invoke(Object proxy, Method m, Object[] args) throws Throwable {
           if (m.getName().equals("serviceConnected")) {
               mIsConnected = true;
           }
           return null;
        }
    }
}

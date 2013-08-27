/*
 * Copyright (C) 2010 The Android Open Source Project
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

package  com.android.nfc.dhimpl;



/**
* Native interface to the NFC Host Card Emulation functions
*
* {@hide}
*/
public class NativeNfcCEFromHost {

    public native boolean dosetNfcCEFromHostTypeA(byte sak, byte[] atqa, byte[] app_data);
    public native boolean dosetNfcCEFromHostTypeB(byte[] atqb, byte[] hi_layer_resp, int afi);
    public native void doresetNfcCEFromHostType();
    public native boolean dosendNfcCEFromHost(byte[] data);
    public native byte[] doreceiveNfcCEFromHost();

}

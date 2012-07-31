/*****************************************************************************
**
**  Name:           PowerSwitch.h
**
**  Description:    Adjust the controller's power states.
**
**  Copyright (c) 2012, Broadcom Corp., All Rights Reserved.
**  Proprietary and confidential.
**
*****************************************************************************/
#pragma once
#include "nfa_api.h"
#include "nfa_brcm_api.h"
#include "SyncEvent.h"


/*****************************************************************************
**
**  Name:           PowerSwitch
**
**  Description:    Adjust the controller's power states.
**
*****************************************************************************/
class PowerSwitch
{
public:


    /*******************************************************************************
    **
    ** Description:     UNKNOWN_LEVEL: power level is unknown because the stack is off.
    **                  FULL_POWER: controller is in full-power state.
    **                  LOW_POWER: controller is in lower-power state.
    **
    *******************************************************************************/
    enum PowerLevel {UNKNOWN_LEVEL, FULL_POWER, LOW_POWER, POWER_OFF};

    /*******************************************************************************
    **
    ** Description:     Platform Power Level, copied from NativeNfcBrcmPowerMode.java.
    **                  UNKNOWN_LEVEL: power level is unknown.
    **                  POWER_OFF: The phone is turned OFF
    **                  SCREEN_OFF: The phone is turned ON but screen is OFF
    **                  SCREEN_ON_LOCKED: The phone is turned ON, screen is ON but user locked
    **                  SCREEN_ON_UNLOCKED: The phone is turned ON, screen is ON, and user unlocked
    **
    *******************************************************************************/
    static const int PLATFORM_UNKNOWN_LEVEL = 0;
    static const int PLATFORM_POWER_OFF = 1;
    static const int PLATFORM_SCREEN_OFF = 2;
    static const int PLATFORM_SCREEN_ON_LOCKED = 3;
    static const int PLATFORM_SCREEN_ON_UNLOCKED = 4;

    static const int VBAT_MONITOR_ENABLED = 1;
    static const int VBAT_MONITOR_PRIMARY_THRESHOLD = 5;
    static const int VBAT_MONITOR_SECONDARY_THRESHOLD = 8;
    /*******************************************************************************
    **
    ** Function:        PowerSwitch
    **
    ** Description:     Initialize member variables.
    **                  
    ** Returns:         None
    **
    *******************************************************************************/
    PowerSwitch ();
  
    
    /*******************************************************************************
    **
    ** Function:        ~PowerSwitch
    **
    ** Description:     Release all resources.
    **                  
    ** Returns:         None
    **
    *******************************************************************************/
    ~PowerSwitch ();
  
    
    /*******************************************************************************
    **
    ** Function:        getInstance
    **
    ** Description:     Get the singleton of this object.
    **                  
    ** Returns:         Reference to this object.
    **
    *******************************************************************************/
    static PowerSwitch& getInstance ();
  
    /*******************************************************************************
    **
    ** Function:        initialize
    **
    ** Description:     Initialize member variables.
    **                  
    ** Returns:         None
    **
    *******************************************************************************/
    void initialize (PowerLevel level);
  
    
    /*******************************************************************************
    **
    ** Function:        getLevel 
    **
    ** Description:     Get the current power level of the controller.
    **                  
    ** Returns:         Power level.
    **
    *******************************************************************************/
    PowerLevel getLevel ();


    /*******************************************************************************
    **
    ** Function:        isScreenOn
    **
    ** Description:     Get the current screen state of the platform host.
    **
    ** Returns:         true if screen if on, (locked or unlocked).
    **
    *******************************************************************************/
    bool isScreenOn ();

    
    /*******************************************************************************
    **
    ** Function:        setLevel
    **
    ** Description:     Set the controller's power level.
    **                  level: power level.
    **                  
    ** Returns:         True if ok.
    **
    *******************************************************************************/
    bool setLevel (PowerLevel level);
  
    /*******************************************************************************
    **
    ** Function:        setScreenState
    **
    ** Description:     Set the Platform's screen state
    **                  state: true for screen on, flase for screem off
    **
    ** Returns:         True if ok.
    **
    *******************************************************************************/
    bool setScreenState (bool state);

    /*******************************************************************************
    **
    ** Function:        abort
    **
    ** Description:     Abort and unblock currrent operation.
    **                  
    ** Returns:         None
    **
    *******************************************************************************/
    void abort ();
  
    
    /*******************************************************************************
    **
    ** Function:        deviceManagementCallback
    **
    ** Description:     Callback function for the stack.
    **                  event: event ID.
    **                  eventData: event's data.
    **                  
    ** Returns:         None
    **
    *******************************************************************************/
    static void deviceManagementCallback (UINT8 event, tNFA_DM_CBACK_DATA* eventData);

    
    /*******************************************************************************
    **
    ** Function:        isPowerOffSleepFeatureEnabled
    **
    ** Description:     Whether power-off-sleep feature is enabled in .conf file.
    **                  
    ** Returns:         True if feature is enabled.
    **
    *******************************************************************************/
    bool isPowerOffSleepFeatureEnabled ();

private:
    PowerLevel mCurrLevel;
    bool mScreenState;
    UINT8 mCurrDeviceMgtPowerState; //device management power state; such as NFA_DM_PWR_STATE_???
    int mDesiredScreenOffPowerState; //read from .conf file; 0=power-off-sleep; 1=full power; 2=CE4 power
    static PowerSwitch sPowerSwitch; //singleton object
    static const UINT8 NFA_DM_PWR_STATE_UNKNOWN = -1; //device management power state power state is unknown
    SyncEvent mPowerStateEvent;
  
    
    /*******************************************************************************
    **
    ** Function:        setPowerOffSleepState
    **
    ** Description:     Adjust controller's power-off-sleep state.
    **                  sleep: whether to enter sleep state.
    **                  
    ** Returns:         True if ok.
    **
    *******************************************************************************/
    bool setPowerOffSleepState (bool sleep);
  
    
    /*******************************************************************************
    **
    ** Function:        deviceMgtPowerStateToString
    **
    ** Description:     Decode power level to a string. 
    **                  deviceMgtPowerState: power level.
    **                  
    ** Returns:         Text representation of power level.
    **
    *******************************************************************************/
    const char* deviceMgtPowerStateToString (UINT8 deviceMgtPowerState);
  
    
    /*******************************************************************************
    **
    ** Function:        powerLevelToString
    **
    ** Description:     Decode power level to a string.
    **                  level: power level.
    **                  
    ** Returns:         Text representation of power level.
    **
    *******************************************************************************/
    const char* powerLevelToString (PowerLevel level);
};
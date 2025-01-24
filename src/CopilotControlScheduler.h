#pragma once

#include "CopilotControlJavaScript.h"
#include "CopilotControlMessageDefinition.h"
#include "CopilotControlUtl.h"
#include "Evm.h"
#include "GPS.h"
#include "Log.h"
#include "Shell.h"
#include "TimeClass.h"
#include "Timeline.h"
#include "Utl.h"

#include <algorithm>
#include <functional>
#include <string>
#include <unordered_map>
using namespace std;


class CopilotControlScheduler
{
private:

    struct SlotBehavior
    {
        bool   runJs   = true;
        string msgSend = "default";

        bool                     hasDefault     = false;
        bool                     canSendDefault = false;
        function<void(uint64_t)> fnSendDefault  = [](uint64_t){};
    };

    struct SlotState
    {
        uint8_t slot;

        SlotBehavior slotBehavior;

        bool jsRanOk = false;
    };


public:

    CopilotControlScheduler()
    {
        SetupShell();
        ResetTimers();
        t_.SetMaxEvents(50);
    }


    /////////////////////////////////////////////////////////////////
    // Callback Setting - GPS Operation
    /////////////////////////////////////////////////////////////////

private:

    bool reqGpsActive_ = false;

    function<void()> fnCbRequestNewGpsLock_       = []{};
    function<void()> fnCbCancelRequestNewGpsLock_ = []{};

    void RequestNewGpsLock()
    {
        Mark("REQ_NEW_GPS_LOCK");

        reqGpsActive_ = true;

        if (IsTesting() == false)
        {
            fnCbRequestNewGpsLock_();
        }
    }

    void CancelRequestNewGpsLock()
    {
        Mark("CANCEL_REQ_NEW_GPS_LOCK");

        reqGpsActive_ = false;

        if (IsTesting() == false)
        {
            fnCbCancelRequestNewGpsLock_();
        }
    }

public:

    void SetCallbackRequestNewGpsLock(function<void()> fn)
    {
        fnCbRequestNewGpsLock_ = fn;
    }

    void SetCallbackCancelRequestNewGpsLock(function<void()> fn)
    {
        fnCbCancelRequestNewGpsLock_ = fn;
    }


    /////////////////////////////////////////////////////////////////
    // Callback Setting - Message Sending
    /////////////////////////////////////////////////////////////////

private:

    function<void()>                                               fnCbSendRegularType1_   = []{};
    function<void()>                                               fnCbSendBasicTelemetry_ = []{};
    function<void(uint8_t slot, MsgUD &msg, uint64_t quitAfterMs)> fnCbSendUserDefined_    = [](uint8_t, MsgUD &, uint64_t){};

    void SendRegularType1()
    {
        Mark("SEND_REGULAR_TYPE1");

        if (IsTesting() == false)
        {
            fnCbSendRegularType1_();
        }
    }

    void SendBasicTelemetry()
    {
        Mark("SEND_BASIC_TELEMETRY");

        if (IsTesting() == false)
        {
            fnCbSendBasicTelemetry_();
        }
    }

    void SendCustomMessage(uint8_t slot, MsgUD &msg, uint64_t quitAfterMs = 0)
    {
        Mark("SEND_CUSTOM_MESSAGE");

        if (IsTesting() == false)
        {
            fnCbSendUserDefined_(slot, msg, quitAfterMs);
        }
    }

public:

    void SetCallbackSendRegularType1(function<void()> fn)
    {
        fnCbSendRegularType1_ = fn;
    }

    void SetCallbackSendBasicTelemetry(function<void()> fn)
    {
        fnCbSendBasicTelemetry_ = fn;
    }

    void SetCallbackSendUserDefined(function<void(uint8_t slot, MsgUD &msg, uint64_t quitAfterMs)> fn)
    {
        fnCbSendUserDefined_ = fn;
    }


    /////////////////////////////////////////////////////////////////
    // Callback Setting - Radio
    /////////////////////////////////////////////////////////////////

private:

    function<bool()> fnCbRadioIsActive_     = []{ return false; };
    function<void()> fnCbStartRadioWarmup_  = []{};
    function<void()> fnCbStopRadio_         = []{};

    bool RadioIsActive()
    {
        if (IsTesting() == false)
        {
            return fnCbRadioIsActive_();
        }
        else
        {
            return false;
        }
    }

    void StartRadioWarmup()
    {
        Mark("ENABLE_RADIO");

        if (IsTesting() == false)
        {
            fnCbStartRadioWarmup_();
        }
    }

    void StopRadio()
    {
        Mark("DISABLE_RADIO");

        if (IsTesting() == false)
        {
            fnCbStopRadio_();
        }
    }

public:

    void SetCallbackRadioIsActive(function<bool()> fn)
    {
        fnCbRadioIsActive_ = fn;
    }

    void SetCallbackStartRadioWarmup(function<void()> fn)
    {
        fnCbStartRadioWarmup_ = fn;
    }

    void SetCallbackStopRadio(function<void()> fn)
    {
        fnCbStopRadio_ = fn;
    }


    /////////////////////////////////////////////////////////////////
    // Callback Setting - Speed Settings
    /////////////////////////////////////////////////////////////////

private:

    function<void()> fnCbGoHighSpeed_ = []{};
    function<void()> fnCbGoLowSpeed_  = []{};

    void GoHighSpeed()
    {
        if (IsTesting() == false)
        {
            fnCbGoHighSpeed_();
        }
    }

    void GoLowSpeed()
    {
        if (IsTesting() == false)
        {
            fnCbGoLowSpeed_();
        }
    }


public:

    void SetCallbackGoHighSpeed(function<void()> fn)
    {
        fnCbGoHighSpeed_ = fn;
    }

    void SetCallbackGoLowSpeed(function<void()> fn)
    {
        fnCbGoLowSpeed_ = fn;
    }


    /////////////////////////////////////////////////////////////////
    // Timing
    /////////////////////////////////////////////////////////////////

private:

    uint8_t startMin_ = 0;


public:

    void SetStartMinute(uint8_t startMin)
    {
        startMin_ = startMin;
    }



    /////////////////////////////////////////////////////////////////
    // Event Handling
    /////////////////////////////////////////////////////////////////

    // The scheduler manages running the the schedule, and accepts
    // two types of inputs from the caller:
    // - Configuration
    // - Events
    //
    // Configuration consists of:
    // - callbacks the scheduler needs to operate
    // - data the scheduler needs to operate
    //
    // Events consist of:
    // - Start / Stop events
    // - GPS events
    //
    //
    // The operational model is:
    // - There are two primary schedule states when running
    //   - unlocked
    //   - locked
    // - The locked state means:
    //   - The schedule is currently running
    //   - No changes to the schedule can be made
    //   - New GPS data is queued to be applied later
    // - The unlocked state means:
    //   - The schedule is not currently running
    //   - Changes to the schedule can be made any number of times
    //   - New GPS data is applied immediately
    //
    // During both the locked and unlocked states, GPS events can be handled.
    //
    // During the unlocked state
    // - GPS Time event
    //   - time synchronized
    //   - Coast eligible
    //   - if no current 3DFix, schedule a no-gps-fix window
    // - GPS 3DFix event
    //   - time synchronized
    //   - schedule a gps-fix window
    //
    // During the locked state
    // - GPS Time event
    //   - cached
    // - GPS 3DFix event
    //   - cached
    //
    //
    // OnWindowLockoutEnd
    // - destroy current gps data
    // - if any cached data
    //   - apply best cached GPS data
    //     - need to retroactively apply time sync (timestamp at time of reception I guess)
    // - if no cached data
    //   - schedule a no-gps-fix window
    //
    //
    // OnStop
    // - cancel all async events
    // - destroy all data (except coast flag)
    // - end lockout
    //
    // OnStart
    // - if coast eligible, schedule a no-gps-fix window
    //
    //
    //
    // Coasting
    // - if a gps time lock has ever once been acquired, coasting is possible.
    // - take notes on the implications of coasting
    //   - tries to wait as long as possible to have any kind of schedule
    //     - because scheduler tries to reach back as far as 30 sec
    //       which isn't good for getting a 3d lock
    //   - as soon as coast is triggered, it's basically time up for getting a lock
    //   - also the pre-window duration to warm up and run javascript is now basically 7 sec
    //





    /////////////////////////////////////////////////////////////////
    // Start / Stop Events
    /////////////////////////////////////////////////////////////////

private:

    bool running_ = false;


public:

    void Start()
    {
        if (running_ == true) { return; }

        Mark("START");

        Stop();
        running_ = true;

        RequestNewGpsLock();

        LogNL();
    }

    void Stop()
    {
        if (running_ == false) { return; }

        Mark("STOP");

        // no longer in running state
        running_ = false;
        
        // end gps request
        reqGpsActive_ = false;

        // reset gps state
        scheduleDataActive_ = ScheduleData{};
        scheduleDataCache_  = ScheduleData{};

        // end schedule lockout
        inLockout_ = false;

        // cancel schedule actions
        ResetTimers();

        LogNL();
    }


    /////////////////////////////////////////////////////////////////
    // GPS Events
    /////////////////////////////////////////////////////////////////

private:

    // data that is used in and out of lockout
    struct ScheduleData
    {
        Fix3DPlus gpsFix3DPlus;
        uint64_t  timeAtGpsFix3DPlusSetUs = 0;

        FixTime  gpsFixTime;
        uint64_t timeAtGpsFixTimeSetUs = 0;
    };

    // cache for data acquired during lockout
    ScheduleData scheduleDataCache_;

    // schedule data being actively used outside lockout
    ScheduleData scheduleDataActive_;


public:

    void OnGps3DPlusLock(const Fix3DPlus &gpsFix3DPlus)
    {
        if (running_ == false) { return; }

        uint64_t timeNowUs = PAL.Micros();

        if (reqGpsActive_ == true && inLockout_ == false)
        {
            Mark("ON_GPS_LOCK_3D_PLUS_APPLIED");

            // set active data
            scheduleDataActive_.gpsFix3DPlus            = gpsFix3DPlus;
            scheduleDataActive_.timeAtGpsFix3DPlusSetUs = timeNowUs;

            // apply
            ScheduleApplyTimeAndUpdateSchedule(scheduleDataActive_.gpsFix3DPlus,
                                               scheduleDataActive_.timeAtGpsFix3DPlusSetUs,
                                               true);
        }
        else if (reqGpsActive_ == true && inLockout_ == true)
        {
            LogNL();
            Mark("ON_GPS_LOCK_3D_PLUS_CACHED");

            // cache
            scheduleDataCache_.gpsFix3DPlus            = gpsFix3DPlus;
            scheduleDataCache_.timeAtGpsFix3DPlusSetUs = timeNowUs;
        }
        else if (reqGpsActive_ == false && inLockout_ == false)
        {
            LogNL();
            Mark("ON_GPS_LOCK_3D_PLUS_REQ_NO_LOCKOUT_NO");

            // ignore
        }
        else // reqGpsActive_ == false && inLockout_ == true
        {
            LogNL();
            Mark("ON_GPS_LOCK_3D_PLUS_REQ_NO_LOCKOUT_ON");

            // ignore
        }

        LogNL();
    }

    void OnGpsTimeLock(const FixTime &gpsFixTime)
    {
        if (running_ == false) { return; }

        uint64_t timeNowUs = PAL.Micros();

        if (reqGpsActive_ == true && inLockout_ == false)
        {
            Mark("ON_GPS_LOCK_TIME_APPLIED");

            // set active data
            scheduleDataActive_.gpsFixTime            = gpsFixTime;
            scheduleDataActive_.timeAtGpsFixTimeSetUs = timeNowUs;

            // apply
            ScheduleApplyTimeAndUpdateSchedule(scheduleDataActive_.gpsFixTime,
                                               scheduleDataActive_.timeAtGpsFixTimeSetUs,
                                               false);
        }
        else if (reqGpsActive_ == true && inLockout_ == true)
        {
            LogNL();
            Mark("ON_GPS_LOCK_TIME_CACHED");

            // cache
            scheduleDataCache_.gpsFixTime            = gpsFixTime;
            scheduleDataCache_.timeAtGpsFixTimeSetUs = timeNowUs;
        }
        else if (reqGpsActive_ == false && inLockout_ == false)
        {
            LogNL();
            Mark("ON_GPS_LOCK_TIME_REQ_NO_LOCKOUT_NO");

            // ignore
        }
        else // reqGpsActive_ == false && inLockout_ == true
        {
            LogNL();
            Mark("ON_GPS_LOCK_TIME_REQ_NO_LOCKOUT_ON");

            // ignore
        }

        LogNL();
    }


    /////////////////////////////////////////////////////////////////
    // Schedule Lockout Events
    /////////////////////////////////////////////////////////////////

private:

    bool inLockout_ = false;

    void OnScheduleLockoutStart()
    {
        Mark("SCHEDULE_LOCK_OUT_START");

        inLockout_ = true;

        // run at 6MHz?

        LogNL();
    }

    void OnScheduleLockoutEnd()
    {
        LogNL();
        Mark("SCHEDULE_LOCK_OUT_END");

        if (IsTesting() == false)
        {
            // report now because new events are going to happen immediately
            t_.ReportNow();
        }

        inLockout_ = false;

        // run at 48MHz?

        // apply cached data
        ScheduleApplyCache();

        LogNL();
    }


    /////////////////////////////////////////////////////////////////
    // Schedule Tasks
    /////////////////////////////////////////////////////////////////

private:

    void ScheduleApplyCache()
    {
        // Update active data with cached data.
        //
        // The cache is applied after a window has completed, meaning there is guaranteed
        // to be valid gps data in the active data.
        //
        // There is no guarantee that there is any data at all in the cache.
        //
        // Only overwrite the active data with new data from the cache.
        bool fix3dPlusFresh = false;
        if (scheduleDataCache_.timeAtGpsFix3DPlusSetUs)
        {
            fix3dPlusFresh = true;
            scheduleDataActive_.timeAtGpsFix3DPlusSetUs = scheduleDataCache_.timeAtGpsFix3DPlusSetUs;
            scheduleDataActive_.gpsFix3DPlus            = scheduleDataCache_.gpsFix3DPlus;
        }

        bool fixTimeFresh = false;
        if (scheduleDataCache_.timeAtGpsFixTimeSetUs)
        {
            fixTimeFresh = true;
            scheduleDataActive_.timeAtGpsFixTimeSetUs = scheduleDataCache_.timeAtGpsFixTimeSetUs;
            scheduleDataActive_.gpsFixTime            = scheduleDataCache_.gpsFixTime;
        }

        // Reset cache
        scheduleDataCache_ = ScheduleData{};

        // Schedule using best data available
        if (fix3dPlusFresh)
        {
            // new 3d lock
            Mark("APPLY_CACHE_NEW_3D_PLUS");
            ScheduleApplyTimeAndUpdateSchedule(scheduleDataActive_.gpsFix3DPlus,
                                               scheduleDataActive_.timeAtGpsFix3DPlusSetUs,
                                               true);
        }
        else if (fixTimeFresh)
        {
            // no lock, but there's an updated time
            Mark("APPLY_CACHE_NEW_TIME");
            ScheduleApplyTimeAndUpdateSchedule(scheduleDataActive_.gpsFixTime,
                                               scheduleDataActive_.timeAtGpsFixTimeSetUs,
                                               false);
        }
        else if (scheduleDataActive_.timeAtGpsFix3DPlusSetUs &&
                 scheduleDataActive_.timeAtGpsFix3DPlusSetUs >= scheduleDataActive_.timeAtGpsFixTimeSetUs)
        {
            // no lock, and old 3dfix has most recent time
            Mark("APPLY_CACHE_OLD_3D_PLUS");
            ScheduleApplyTimeAndUpdateSchedule(scheduleDataActive_.gpsFix3DPlus,
                                               scheduleDataActive_.timeAtGpsFix3DPlusSetUs,
                                               false);
        }
        else
        {
            // no lock, and old time has most recent time
            Mark("APPLY_CACHE_OLD_TIME");
            ScheduleApplyTimeAndUpdateSchedule(scheduleDataActive_.gpsFixTime,
                                               scheduleDataActive_.timeAtGpsFixTimeSetUs,
                                               false);
        }
    }

    void ScheduleApplyTimeAndUpdateSchedule(const FixTime &gpsFixTime, uint64_t timeAtGpsFixTimeSetUs, bool haveGpsLock)
    {
        Mark("APPLY_TIME_AND_UPDATE_SCHEDULE");

        // set the notional time
        SetNotionalTimeFromGpsTime(gpsFixTime, timeAtGpsFixTimeSetUs);

        // schedule
        if (haveGpsLock)
        {
            Mark("COAST_CANCELED");

            // cancel coast timer
            timerCoast_.Cancel();

            // cancel gps request
            CancelRequestNewGpsLock();

            // schedule now
            ScheduleUpdateSchedule(true);
        }
        else
        {
            // coast to be configured.
            // wait to trigger coast for as long as possible to give max time
            // for 3d fix to be acquired before giving up.
            timerCoast_.SetCallback([this]{
                Mark("COAST_TRIGGERED");

                // cancel gps request
                CancelRequestNewGpsLock();

                // schedule now
                ScheduleUpdateSchedule(false);

                LogNL();
            });

            const uint64_t DURATION_SEVEN_SECS_US = 7 * 1'000 * 1'000;
            uint64_t COAST_LEAD_DURATION_US = DURATION_SEVEN_SECS_US;
            if (IsTesting())
            {
                COAST_LEAD_DURATION_US = 400 * 1'000;
            }
            uint64_t timeNowUs;
            uint64_t timeAtNextWindowStartUs = GetTimeAtNextWindowStartUs(&timeNowUs);

            // try to be a given duration earlier than the window, but don't go before timeNowUs
            uint64_t timeAtTriggerCoastUs = timeAtNextWindowStartUs -
                                            min(COAST_LEAD_DURATION_US, timeAtNextWindowStartUs - timeNowUs);
            timerCoast_.TimeoutAtUs(timeAtTriggerCoastUs);

            Mark("COAST_SCHEDULED");
            Log("Time now : ", Time::GetNotionalTimeAtSystemUs(timeNowUs));
            PrintTimeAtDetails("Coast At ", timeNowUs, timeAtTriggerCoastUs);
            Log("  Wanted             ", Time::MakeTimeFromUs(COAST_LEAD_DURATION_US, true));
            Log("  Got                ", Time::MakeTimeFromUs(timeAtNextWindowStartUs - timeAtTriggerCoastUs, true));
            PrintTimeAtDetails("Window At", timeNowUs, timeAtNextWindowStartUs);
        }
    }

    void ScheduleUpdateSchedule(bool haveGpsLock)
    {
        Mark("UPDATE_SCHEDULE");

        // get current time and time of next window
        uint64_t timeNowUs;
        uint64_t timeAtNextWindowStartUs = GetTimeAtNextWindowStartUs(&timeNowUs);

        // logging
        Log("Time now : ", Time::GetNotionalTimeAtSystemUs(timeNowUs));
        PrintTimeAtDetails("Window At", timeNowUs, timeAtNextWindowStartUs);

        // prepare
        ScheduleWindow(timeNowUs, timeAtNextWindowStartUs, haveGpsLock);
    }

    void ScheduleWindow(uint64_t timeNowUs, uint64_t timeAtWindowStartUs, bool haveGpsLock)
    {
        // configure slot behavior knowing we have a gps lock
        // 100ms
        PrepareWindowSlotBehavior(haveGpsLock);

        // schedule actions based on when the next 10-min window is
        // 6ms
        PrepareWindowSchedule(timeNowUs, timeAtWindowStartUs);
    }



    // eyeballing the speed of scheduling at 6MHz, it's bad. like really bad.
        // more testing required
    // but yeah, if we're ok to run JS at 48MHz, why not run that way all the time?
        // or, at least run that way when doing scheduling?
            // I mean, how long does it take to switch freqs vs
            // the time savings of operating at 48MHz during scheduling?



private:

    uint64_t GetTimeAtNextWindowStartUs(uint64_t *timeNowUsRet = nullptr)
    {
        uint64_t timeNowUs = PAL.Micros();

        // we know that the notional time is sync'd to gps.
        // use the current time to get the gps time, and use that to calculate time
        // at next window.
        auto t = Time::ParseDateTime(Time::GetNotionalDateTimeAtSystemUs(timeNowUs));

        // calculate window start
        uint64_t timeAtWindowStartUs =
            CalculateTimeAtWindowStartUs(startMin_, t.minute, t.second, t.us, timeNowUs);

        // return timeNowUs used if requested
        if (timeNowUsRet) { *timeNowUsRet = timeNowUs; }
        
        return timeAtWindowStartUs;
    }

    uint64_t CalculateTimeAtWindowStartUs(uint8_t windowStartMin, uint8_t gpsMin, uint8_t gpsSec, uint32_t gpsUs, uint64_t timeNowUs)
    {
        // calculate how far into the future the start minute is from gps time
        // by modelling a min/sec/ms clock and subtracting gps time from the window time.
        // the window is nominally at <m>:01.000.
        int8_t  minDiff = (int8_t)(windowStartMin - (gpsMin % 10));
        int8_t  secDiff = (int8_t)(1              - gpsSec);
        int32_t usDiff  = (int32_t)               - gpsUs;

        // then, since you know how far into the future the window start is from the
        // gps time, you add that duration onto the gps time and arrive at the window
        // start time.
        int64_t totalDiffUs = 0;
        totalDiffUs += minDiff *      60 * 1'000 * 1'000;
        totalDiffUs +=           secDiff * 1'000 * 1'000;
        totalDiffUs +=                     usDiff;

        // the exception is when the duration is negative, in which case you just add
        // 10 minutes.
        if (totalDiffUs < 0)
        {
            totalDiffUs += 10 * 60 * 1'000 * 1'000;
        }

        // calculate window start time by offset from gps time now
        uint64_t timeAtWindowStartUs = timeNowUs + totalDiffUs;

        return timeAtWindowStartUs;
    }


    /////////////////////////////////////////////////////////////////
    // Internal
    /////////////////////////////////////////////////////////////////

public: // for test running










    /////////////////////////////////////////////////////////////////
    // Slot Behavior
    /////////////////////////////////////////////////////////////////

    // slot1 == 1
    SlotState &GetSlotState(uint8_t slot)
    {
        vector<SlotState *> slotStateList = {
            &slotState1_,
            &slotState2_,
            &slotState3_,
            &slotState4_,
            &slotState5_,
        };

        SlotState &slotState = *slotStateList[slot - 1];

        return slotState;
    }

    bool PeriodWillTransmit(uint8_t period)
    {
        SlotState &slotState = GetSlotState(period);

        return slotState.slotBehavior.msgSend != "none";
    }

    // Periods are always scheduled to be run, because they execute javascript unconditionally.
    //
    // Periods do not always send a message, though.
    //
    // When a slot is supposed to send a custom message, it will only do so if
    // the associated javascript executed successfully.
    //
    // The default is sent in cases where there was a default and a bad custom event.
    void DoPeriodBehavior(SlotState *slotStateThis, uint64_t quitAfterMs, SlotState *slotStateNext = nullptr, const char *slotNameNext = ""){
        if (slotStateThis)
        {
            if (slotStateThis->slotBehavior.msgSend != "none")
            {
                bool sendDefault = false;

                if (slotStateThis->slotBehavior.msgSend == "custom")
                {
                    if (slotStateThis->jsRanOk)
                    {
                        MsgUD &msg = CopilotControlMessageDefinition::GetMsgLastConfigured();

                        SendCustomMessage(slotStateThis->slot, msg, quitAfterMs);
                    }
                    else
                    {
                        sendDefault = true;
                    }
                }
                else    // msgSend == "default"
                {
                    sendDefault = true;
                }

                if (sendDefault)
                {
                    if (slotStateThis->slotBehavior.hasDefault)
                    {
                        if (slotStateThis->slotBehavior.canSendDefault)
                        {
                            slotStateThis->slotBehavior.fnSendDefault(quitAfterMs);
                        }
                        else
                        {
                            // we know this is the outcome because a default function
                            // that relies on gps would not have come through this
                            // branch, it would be msgSend == "none".
                            Mark("SEND_NO_MSG_BAD_JS_NO_ABLE_DEFAULT");
                        }
                    }
                    else
                    {
                        Mark("SEND_NO_MSG_BAD_JS_NO_DEFAULT");
                    }
                }
                else
                {
                    // nothing to do
                }
            }
            else
            {
                Mark("SEND_NO_MSG_NONE");
            }
        }
        else
        {
            // nothing to do
        }

        if (slotStateNext && slotNameNext && slotStateNext->slotBehavior.runJs)
        {
            Mark("JS_EXEC");
            slotStateNext->jsRanOk = RunSlotJavaScript(slotNameNext);
        }
        else
        {
            Mark("JS_NO_EXEC");
            slotStateNext->jsRanOk = false;
        }
    };


    void PrepareWindowSlotBehavior(bool haveGpsLock)
    {
        if (IsTestingCalculateSlotBehaviorDisabled()) { return; }

        Mark("PREPARE_WINDOW_SLOT_BEHAVIOR_START");

        // slot 1
        slotState1_.slotBehavior =
            CalculateSlotBehavior("slot1",
                                  haveGpsLock,
                                  "default",
                                  [this](uint64_t){ SendRegularType1(); });

        // slot 2
        slotState2_.slotBehavior =
            CalculateSlotBehavior("slot2",
                                  haveGpsLock,
                                  "default",
                                  [this](uint64_t){ SendBasicTelemetry(); });

        // slot 3
        slotState3_.slotBehavior = CalculateSlotBehavior("slot3", haveGpsLock);

        // slot 4
        slotState4_.slotBehavior = CalculateSlotBehavior("slot4", haveGpsLock);

        // slot 5
        slotState5_.slotBehavior = CalculateSlotBehavior("slot5", haveGpsLock);

        Mark("PREPARE_WINDOW_SLOT_BEHAVIOR_END");
    }

    // Calculates nominally what should happen for a given slot.
    // This function does not think about or care about running the js in advance in prior slot.
    //
    // Assumes that both a message def and javascript exist.
    SlotBehavior CalculateSlotBehavior(const string             &slotName,
                                       bool                      haveGpsLock,
                                       string                    msgSendDefault = "none",
                                       function<void(uint64_t)>  fnSendDefault = [](uint64_t){})
    {
        // check slot javascript dependencies
        auto apiUsage = js_.GetSlotScriptAPIUsage(slotName);
        bool jsUsesGpsApi = apiUsage.gps;
        bool jsUsesMsgApi = apiUsage.msg;

        // determine actions
        bool   runJs   = true;
        string msgSend = msgSendDefault;

        if (haveGpsLock == false && jsUsesGpsApi == false && jsUsesMsgApi == false) { runJs = true;  msgSend = "none";         }
        if (haveGpsLock == false && jsUsesGpsApi == false && jsUsesMsgApi == true)  { runJs = true;  msgSend = "custom";       }
        if (haveGpsLock == false && jsUsesGpsApi == true  && jsUsesMsgApi == false) { runJs = false; msgSend = "none";         }
        if (haveGpsLock == false && jsUsesGpsApi == true  && jsUsesMsgApi == true)  { runJs = false; msgSend = "none";         }
        if (haveGpsLock == true  && jsUsesGpsApi == false && jsUsesMsgApi == false) { runJs = true;  msgSend = msgSendDefault; }
        if (haveGpsLock == true  && jsUsesGpsApi == false && jsUsesMsgApi == true)  { runJs = true;  msgSend = "custom";       }
        if (haveGpsLock == true  && jsUsesGpsApi == true  && jsUsesMsgApi == false) { runJs = true;  msgSend = msgSendDefault; }
        if (haveGpsLock == true  && jsUsesGpsApi == true  && jsUsesMsgApi == true)  { runJs = true;  msgSend = "custom";       }

        // Actually check if there is a msg def.
        // If there isn't one, then revert behavior to sending the default (if any).
        // Not possible to use the msg api successfully when there isn't a msg def,
        // but who knows, could slip through, run anyway, just no message will be sent.
        string msgSendOrig = msgSend;
        bool hasMsgDef = CopilotControlMessageDefinition::SlotHasMsgDef(slotName);
        if (hasMsgDef == false)
        {
            if (msgSendDefault == "none")
            {
                // if the default is to do nothing, do nothing
                msgSend = "none";
            }
            else    // msgSendDefault == "default"
            {
                // if the default is to send a default message, we have to
                // confirm if there is a gps lock in order to do so
                if (haveGpsLock)
                {
                    msgSend = "default";
                }
                else
                {
                    msgSend = "none";
                }
            }
        }

        Log("Calculating Slot Behavior for ", slotName);
        Log("- gpsLock       : ", haveGpsLock);
        Log("- msgSendDefault: ", msgSendDefault);
        Log("- usesGpsApi    : ", jsUsesGpsApi);
        Log("- usesMsgApi    : ", jsUsesMsgApi);
        Log("- runJs         : ", runJs);
        Log("- hasMsgDef     : ", hasMsgDef);
        LogNNL("- msgSend       : ", msgSend);
        if (msgSend != msgSendOrig)
        {
            LogNNL(" (changed, was ", msgSendOrig, ")");
        }
        LogNL();

        // return
        SlotBehavior retVal = {
            .runJs   = runJs,
            .msgSend = msgSend,

            .hasDefault     = msgSendDefault != "none",
            .canSendDefault = haveGpsLock,
            .fnSendDefault  = fnSendDefault,
        };

        return retVal;
    }


    /////////////////////////////////////////////////////////////////
    // Window Schedule
    /////////////////////////////////////////////////////////////////


    // also, under what conditions should this window consider its start time
    // too soon?
        // like once running, it has lockout protection from rescheduling change
        // too close, but not on the very first run.
            // ie, first gps lock ever happens 1ms before window start time.
                // now no time to execute js and will definitely be off?
                // hmm, actually can't you be off by a fair amount?
                    // yeah, just let it happen




    // I don't like turning off the radio right before using it (from JS)
        // this is only because power savings
        // is this necessary?
            // didn't I measure power on this thing and it wasn't that bad?
    // oh, I also have to consider time cost of calculation
        // it's slow, like very slow (is it? why should it be so slow?)
            // re-calculation can happen during warmup, so should be running
            // at low power (6MHz) then, so is that a material delay?
        // it's the slot behavior, it looks up (flash) and parses, very slow
            // cache values during flight
                // maybe just don't re-calculate every time, not needed. do it on Start()?
        





    // have to change window lockout start
        // come after warmup and before JS
    // need to be able to get a good gps lock until as late as possible
        // the way it works currently leaves no room
            // how bad does it get in slot 5? pretty bad I think.
                // figure it out for now and what happens later when fixed





    void PrepareWindowSchedule(uint64_t timeNowUs, uint64_t timeAtWindowStartUs)
    {
        Mark("PREPARE_WINDOW_SCHEDULE_START");
        Log("PrepareWindowSchedule for ", TimeAt(timeAtWindowStartUs));

        // named durations
        const uint64_t DURATION_ONE_SECOND_US     =      1 * 1'000 * 1'000;
        const uint64_t DURATION_THIRTY_SECONDS_US =     30 * 1'000 * 1'000;
        const uint64_t DURATION_TWO_MINUTES_US    = 2 * 60 * 1'000 * 1'000;

        uint64_t DURATION_AVAIL_PRE_WINDOW_US = timeAtWindowStartUs - timeNowUs;

        if (IsTesting())
        {
            DURATION_AVAIL_PRE_WINDOW_US = 0;
        }


        // warmup start time
        //
        // schedule to start outside the protection of the window lockout.
        // we don't need to protect against this getting interrupted or
        // restarted way outside the window.
        //
        // the lockout period will protect more sensitive activities.
        //
        // No need to schedule if no transmissions will occur.
        const uint64_t DURATION_WANT_WARMUP_US = DURATION_THIRTY_SECONDS_US;
        const uint64_t DURATION_USE_WARMUP_US  = min(DURATION_WANT_WARMUP_US, DURATION_AVAIL_PRE_WINDOW_US);
        const uint64_t TIME_AT_WARMUP_US       = timeAtWindowStartUs - DURATION_USE_WARMUP_US;
        bool DO_WARMUP = false;
        if (PeriodWillTransmit(1)) { DO_WARMUP = true; }
        if (PeriodWillTransmit(2)) { DO_WARMUP = true; }
        if (PeriodWillTransmit(3)) { DO_WARMUP = true; }
        if (PeriodWillTransmit(4)) { DO_WARMUP = true; }
        if (PeriodWillTransmit(5)) { DO_WARMUP = true; }


        // duration required for initial JS
        const uint64_t DURATION_JS_NOMINAL_US       = js_.GetScriptTimeLimitMs() * 1'000;
        const uint64_t DURATION_JS_NOMINAL_FUDGE_US = DURATION_ONE_SECOND_US;
        const uint64_t DURATION_JS_US               = DURATION_JS_NOMINAL_US + DURATION_JS_NOMINAL_FUDGE_US;

        // duration lockout start
        //
        // This relates to protecting all window activities, starting first with
        // running the initial js.
        //
        // Running the initial js needs to complete before the start of the window, so
        // allocate time for that.
        const uint64_t DURATION_WANT_PRE_WINDOW_US = DURATION_JS_US;
        const uint64_t DURATION_USE_PRE_WINDOW_US  = min(DURATION_WANT_PRE_WINDOW_US, DURATION_AVAIL_PRE_WINDOW_US);

        // Schedule Schedule Lock Out Start
        const uint64_t TIME_AT_SCHEDULE_LOCK_OUT_START_US = timeAtWindowStartUs - DURATION_USE_PRE_WINDOW_US;

        // js start time
        // (run immediately after lockout starts)
        const uint64_t TIME_AT_JS_RUN_US = TIME_AT_SCHEDULE_LOCK_OUT_START_US;


        // Period start times
        // There is no advantage to skipping scheduling period 5 when no TX will occur.
        //
        // The question arises because there's no JS to run, so if no TX, why even
        // schedule it. It holds up window end event.
        //
        // Holding up the window end doesn't stop us getting a GPS lock.
        // Holding up the window end also doesn't stop us warming up.
        //
        // Warmup Reasoning:
        // - If there's tx in period 5, you have to wait, and warmup waits too.
        // - If there's no tx, you wait until the period ends, but that's way before
        //   the warmup period, so no savings.
        uint64_t TIME_AT_PERIOD0_START_US = TIME_AT_JS_RUN_US;
        uint64_t TIME_AT_PERIOD1_START_US = timeAtWindowStartUs;
        uint64_t TIME_AT_PERIOD2_START_US = TIME_AT_PERIOD1_START_US + DURATION_TWO_MINUTES_US;
        uint64_t TIME_AT_PERIOD3_START_US = TIME_AT_PERIOD2_START_US + DURATION_TWO_MINUTES_US;
        uint64_t TIME_AT_PERIOD4_START_US = TIME_AT_PERIOD3_START_US + DURATION_TWO_MINUTES_US;
        uint64_t TIME_AT_PERIOD5_START_US = TIME_AT_PERIOD4_START_US + DURATION_TWO_MINUTES_US;

        if (IsTesting())
        {
            // Cause events all to fire in order quickly but with a unique
            // time so that gps can be enabled after a specific period.
            const uint64_t DURATION_GAP_US = 1;

            // PERIOD 0, 1 are not required to override, they end up at window start
            TIME_AT_PERIOD2_START_US = TIME_AT_PERIOD1_START_US + DURATION_GAP_US;
            TIME_AT_PERIOD3_START_US = TIME_AT_PERIOD2_START_US + DURATION_GAP_US;
            TIME_AT_PERIOD4_START_US = TIME_AT_PERIOD3_START_US + DURATION_GAP_US;
            TIME_AT_PERIOD5_START_US = TIME_AT_PERIOD4_START_US + DURATION_GAP_US;
        }

        // Schedule GPS Req (and tx disable).
        //
        // GPS Req should happen after:
        // - Final transmission period (GPS can't run at same time as TX).
        //
        // This is safe because:
        // - GPS operation does not interfere with running js.
        // - GPS new locks won't affect this window's data.
        //
        // Schedule event for the same start moment as the final
        // transmission period itself, knowing that this event,
        // which is scheduled second, will execute directly
        // after, which is as early as possible, and what we want.
        uint64_t TIME_AT_GPS_REQ_US = timeAtWindowStartUs;
        bool TIME_AT_GPS_REQ_RESCHEDULED = false;
        if (PeriodWillTransmit(1)) { TIME_AT_GPS_REQ_US = TIME_AT_PERIOD1_START_US; TIME_AT_GPS_REQ_RESCHEDULED = true; }
        if (PeriodWillTransmit(2)) { TIME_AT_GPS_REQ_US = TIME_AT_PERIOD2_START_US; TIME_AT_GPS_REQ_RESCHEDULED = true; }
        if (PeriodWillTransmit(3)) { TIME_AT_GPS_REQ_US = TIME_AT_PERIOD3_START_US; TIME_AT_GPS_REQ_RESCHEDULED = true; }
        if (PeriodWillTransmit(4)) { TIME_AT_GPS_REQ_US = TIME_AT_PERIOD4_START_US; TIME_AT_GPS_REQ_RESCHEDULED = true; }
        if (PeriodWillTransmit(5)) { TIME_AT_GPS_REQ_US = TIME_AT_PERIOD5_START_US; TIME_AT_GPS_REQ_RESCHEDULED = true; }

        // Schedule Lock Out End.
        //
        // This event should come after the final period of work.
        //
        // It is no harm to end the lock out period after the 5th period
        // even if no work gets done.
        const uint64_t TIME_AT_SCHEDULE_LOCK_OUT_END_US = TIME_AT_PERIOD5_START_US;





        // Setup warmup.
        if (DO_WARMUP)
        {
            timerTxWarmup_.SetCallback([this]{
                Mark("TX_WARMUP");
                StartRadioWarmup();
                LogNL();
            });
            timerTxWarmup_.TimeoutAtUs(TIME_AT_WARMUP_US);
            Log("Scheduled ", TimeAt(TIME_AT_WARMUP_US), " for TX_WARMUP");
            Log("    ", Time::MakeDurationFromUs(DURATION_WANT_WARMUP_US), " early wanted");
            Log("    ", Time::MakeDurationFromUs(DURATION_AVAIL_PRE_WINDOW_US), " early was possible");
            Log("    ", Time::MakeDurationFromUs(DURATION_USE_WARMUP_US), " early used");
        }
        else
        {
            Log("Did NOT schedule TX_WARMUP, no transmissions scheduled");
        }



        // Setup Schedule Lock Out Start.
        timerScheduleLockOutStart_.SetCallback([this]{
            OnScheduleLockoutStart();
        });
        timerScheduleLockOutStart_.TimeoutAtUs(TIME_AT_SCHEDULE_LOCK_OUT_START_US);
        Log("Scheduled ", TimeAt(TIME_AT_SCHEDULE_LOCK_OUT_START_US), " for SCHEDULE_LOCK_OUT_START");
        Log("    ", Time::MakeDurationFromUs(DURATION_WANT_PRE_WINDOW_US), " early wanted");
        Log("    ", Time::MakeDurationFromUs(DURATION_AVAIL_PRE_WINDOW_US), " early was possible");
        Log("    ", Time::MakeDurationFromUs(DURATION_USE_PRE_WINDOW_US), " early used");



        // Setup GPS Req for start of window, initially.
        //
        // This lets the GPS Req beat Period1 to be executed in the event
        // that no periods are transmitters (which hold up GPS Req).
        //
        // This is adjusted later.
        // (this could all be done right now, but trying to keep this code
        //  in the same order as execution)
        timerTxDisableGpsEnable_.TimeoutAtUs(timeAtWindowStartUs);
        Log("Scheduled ", TimeAt(timeAtWindowStartUs), " for TX_DISABLE_GPS_ENABLE (initial)");



        // Setup Periods.
        timerPeriod0_.SetCallback([this]{
            Mark("PERIOD0_START");
            DoPeriodBehavior(nullptr, 0, &slotState1_, "slot1");
            Mark("PERIOD0_END");
        });
        timerPeriod0_.TimeoutAtUs(TIME_AT_PERIOD0_START_US);
        Log("Scheduled ", TimeAt(TIME_AT_PERIOD0_START_US), " for PERIOD0_START");

        timerPeriod1_.SetCallback([this]{
            Mark("PERIOD1_START");
            DoPeriodBehavior(&slotState1_, 0, &slotState2_, "slot2");
            Mark("PERIOD1_END");
        });
        timerPeriod1_.TimeoutAtUs(TIME_AT_PERIOD1_START_US);
        Log("Scheduled ", TimeAt(TIME_AT_PERIOD1_START_US), " for PERIOD1_START");

        timerPeriod2_.SetCallback([this]{
            Mark("PERIOD2_START");
            DoPeriodBehavior(&slotState2_, 0, &slotState3_, "slot3");
            Mark("PERIOD2_END");
        });
        timerPeriod2_.TimeoutAtUs(TIME_AT_PERIOD2_START_US);
        Log("Scheduled ", TimeAt(TIME_AT_PERIOD2_START_US), " for PERIOD2_START");

        timerPeriod3_.SetCallback([this]{
            Mark("PERIOD3_START");
            DoPeriodBehavior(&slotState3_, 0, &slotState4_, "slot4");
            Mark("PERIOD3_END");
        });
        timerPeriod3_.TimeoutAtUs(TIME_AT_PERIOD3_START_US);
        Log("Scheduled ", TimeAt(TIME_AT_PERIOD3_START_US), " for PERIOD3_START");

        timerPeriod4_.SetCallback([this]{
            Mark("PERIOD4_START");
            DoPeriodBehavior(&slotState4_, 0, &slotState5_, "slot5");
            Mark("PERIOD4_END");
        });
        timerPeriod4_.TimeoutAtUs(TIME_AT_PERIOD4_START_US);
        Log("Scheduled ", TimeAt(TIME_AT_PERIOD4_START_US), " for PERIOD4_START");

        timerPeriod5_.SetCallback([this]{
            Mark("PERIOD5_START");
            // tell sender to quit early
            const uint64_t ONE_MINUTE_MS = 1 * 60 * 1'000;
            DoPeriodBehavior(&slotState5_, ONE_MINUTE_MS);
            Mark("PERIOD5_END");
        });
        timerPeriod5_.TimeoutAtUs(TIME_AT_PERIOD5_START_US);
        Log("Scheduled ", TimeAt(TIME_AT_PERIOD5_START_US), " for PERIOD5_START");



        // Setup GPS Req (and tx disable).
        timerTxDisableGpsEnable_.SetCallback([this]{
            Mark("TX_DISABLE_GPS_ENABLE");

            // disable transmitter
            StopRadio();

            // enable gps
            RequestNewGpsLock();
        });
        if (TIME_AT_GPS_REQ_RESCHEDULED)
        {
            timerTxDisableGpsEnable_.TimeoutAtUs(TIME_AT_GPS_REQ_US);
            Log("Scheduled ", TimeAt(TIME_AT_GPS_REQ_US), " for TX_DISABLE_GPS_ENABLE (reschedule)");
        }



        // Setup Schedule Lock Out End.
        timerScheduleLockOutEnd_.SetCallback([this]{
            OnScheduleLockoutEnd();
        });
        timerScheduleLockOutEnd_.TimeoutAtUs(TIME_AT_SCHEDULE_LOCK_OUT_END_US);
        Log("Scheduled ", TimeAt(TIME_AT_SCHEDULE_LOCK_OUT_END_US), " for SCHEDULE_LOCK_OUT_END");





        Mark("PREPARE_WINDOW_SCHEDULE_END");

        t_.Reset();
    }


    /////////////////////////////////////////////////////////////////
    // JavaScript Execution
    /////////////////////////////////////////////////////////////////

    bool RunSlotJavaScript(const string &slotName)
    {
        bool retVal = true;

        // cache whether radio enabled to know if to disable/re-enable
        bool radioActive = RadioIsActive();

        if (radioActive)
        {
            StopRadio();
        }

        // change to 48MHz
        GoHighSpeed();

        // invoke js
        if (IsTestingJsDisabled() == false)
        {
            auto jsResult = js_.RunSlotJavaScript(slotName, &scheduleDataActive_.gpsFix3DPlus);
            retVal = jsResult.runOk;
        }
        else
        {
            retVal = true;
        }

        // change to 6MHz
        GoLowSpeed();

        if (radioActive)
        {
            StartRadioWarmup();
        }

        return retVal;
    }


    /////////////////////////////////////////////////////////////////
    // Testing
    /////////////////////////////////////////////////////////////////

    void TestGpsEventInterface(vector<string> testNameList);
    void TestPrepareWindowSchedule();
    void TestConfigureWindowSlotBehavior();
    void TestCalculateTimeAtWindowStartUs(bool fullSweep = false);



    /////////////////////////////////////////////////////////////////
    // Utility
    /////////////////////////////////////////////////////////////////

    void ResetTimers()
    {
        timerCoast_.Cancel();
        timerCoast_.SetVisibleInTimeline(false);
        timerScheduleLockOutStart_.Cancel();
        timerScheduleLockOutStart_.SetVisibleInTimeline(false);
        timerPeriod0_.Cancel();
        timerPeriod0_.SetVisibleInTimeline(false);
        timerTxWarmup_.Cancel();
        timerTxWarmup_.SetVisibleInTimeline(false);
        timerPeriod1_.Cancel();
        timerPeriod1_.SetVisibleInTimeline(false);
        timerPeriod2_.Cancel();
        timerPeriod2_.SetVisibleInTimeline(false);
        timerPeriod3_.Cancel();
        timerPeriod3_.SetVisibleInTimeline(false);
        timerPeriod4_.Cancel();
        timerPeriod4_.SetVisibleInTimeline(false);
        timerPeriod5_.Cancel();
        timerPeriod5_.SetVisibleInTimeline(false);
        timerTxDisableGpsEnable_.Cancel();
        timerTxDisableGpsEnable_.SetVisibleInTimeline(false);
        timerScheduleLockOutEnd_.Cancel();
        timerScheduleLockOutEnd_.SetVisibleInTimeline(false);
    }

    // a positive shift means move the current time forward, which will
    // cause the timers to expire sooner.
    void ShiftTime(int64_t durationUs)
    {
        Mark("SHIFT_TIME");

        // calculate
        uint64_t timeNowUs         = PAL.Micros();
        uint64_t notionalTimeWasUs = Time::GetNotionalUsAtSystemUs(timeNowUs);
        uint64_t notionalTimeNowUs = notionalTimeWasUs + durationUs;

        Log("Notional time was:  ", Time::MakeTimeFromUs(notionalTimeWasUs));
        Log("Shifting time by : ",  Time::MakeTimeRelativeFromUs(notionalTimeNowUs, notionalTimeWasUs));
        Log("Notional time now:  ", Time::MakeTimeFromUs(notionalTimeNowUs));

        // change notional time
        Time::SetNotionalUs(notionalTimeNowUs, timeNowUs);

        // get list of timers in mostly-correct order, such that if they were
        // stable sorted by timeout they'd be in timeout order (which takes
        // into consideration the purposeful matching of expiry times that
        // some timers do).
        vector<Timer *> timerList = {
            &timerCoast_,
            &timerTxWarmup_,
            &timerScheduleLockOutStart_,
            &timerPeriod0_,
            &timerPeriod1_,
            &timerPeriod2_,
            &timerPeriod3_,
            &timerPeriod4_,
            &timerPeriod5_,
            &timerTxDisableGpsEnable_,
            &timerScheduleLockOutEnd_,
        };

        // take a copy of the original order for reporting
        vector<Timer *> timerListOriginalOrder = timerList;

        // create helper to extract the timeouts into a list
        auto GetTimeoutAtUsList = [](const vector<Timer *> &timerList){
            vector<uint64_t> timeoutAtUsList;

            for (const Timer *timer : timerList)
            {
                timeoutAtUsList.push_back(timer->GetTimeoutAtUs());
            }

            return timeoutAtUsList;
        };

        // get a list of the original timeouts
        vector<uint64_t> timeoutAtUsListOrig = GetTimeoutAtUsList(timerListOriginalOrder);

        // sort while retaining order of equal items
        stable_sort(timerList.begin(), timerList.end(), [](auto t1, auto t2){
            return t1->GetTimeoutAtUs() < t2->GetTimeoutAtUs();
        });

        // iterate sorted-by-expiry timer list, if pending, change timer by offset.
        // by iterating in sorted order, we ensure that timers that had the same expiry
        // end up on the same expiry again, in the same first-setter-wins order.
        // if time is moving forward, expiry should happen sooner, so deduct from expiry.
        for (Timer *timer : timerList)
        {
            if (timer->IsPending())
            {
                if (durationUs > 0)
                {
                    // ensure that the timer doesn't wrap around when subtracted
                    timer->TimeoutAtUs(timer->GetTimeoutAtUs() - min((uint64_t)durationUs, timer->GetTimeoutAtUs()));
                }
                else
                {
                    timer->TimeoutAtUs(timer->GetTimeoutAtUs() - durationUs);
                }
            }
        }

        // get a list of the modified timeouts
        vector<uint64_t> timeoutAtUsListNew = GetTimeoutAtUsList(timerListOriginalOrder);

        // report on change to timers
        auto Report = [](string name, uint8_t nameWidthTotal, uint64_t timeAtWasUs, uint64_t timeAtNowUs, uint64_t timeNowUs){
            if (timeAtWasUs == timeAtNowUs)
            {
                Log(StrUtl::PadRight(name, ' ', nameWidthTotal), ": No Change");
            }
            else
            {
                // purposefully show the "changed by" as a positive number when adjusting timers to expire sooner.
                // this means a positive shift results in a "positive" time change, even though we're actually
                // deducting time from the timer.
                Log(StrUtl::PadRight(name, ' ', nameWidthTotal), ": Changed by ", Time::MakeTimeRelativeFromUs(timeAtWasUs, timeAtNowUs));
                Log("  Was: ", Time::MakeTimeRelativeFromUs(timeAtWasUs, timeNowUs));
                Log("  Now: ", Time::MakeTimeRelativeFromUs(timeAtNowUs, timeNowUs));
            }
        };

        LogNL();
        Log("Shift Time Report");
        uint8_t nameWidthTotal = strlen(timerScheduleLockOutStart_.GetName());
        for (size_t i = 0; i < timerListOriginalOrder.size(); ++i)
        {
            Timer *timer = timerListOriginalOrder[i];
            Report(timer->GetName(), nameWidthTotal, timeoutAtUsListOrig[i], timeoutAtUsListNew[i], timeNowUs);
        }
    }

    void PrintTimeAtDetails(string title, uint64_t timeNowUs, uint64_t timeAtUs)
    {
        LogNNL(StrUtl::PadRight(title, ' ', title.size()));
        LogNNL(": ", Time::GetNotionalTimeAtSystemUs(timeAtUs));
        LogNNL(" in: ", Time::MakeTimeRelativeFromUs(timeAtUs, timeNowUs));
        LogNL();
    }

    void PrintStatus()
    {
        uint64_t timeNowUs;
        uint64_t timeAtNextWindowStartUs = GetTimeAtNextWindowStartUs(&timeNowUs);

        // get the time of the upcoming or current window
        uint64_t timeAtUpcomingOrCurrentWindowStartUs = timeAtNextWindowStartUs;
        if (inLockout_)
        {
            const uint64_t TEN_MINUTES_US = 10 * 60 * 1'000 * 1'000;

            timeAtUpcomingOrCurrentWindowStartUs -= TEN_MINUTES_US;
        }

        LogNL();
        Log("Scheduler Status");
        Log("---------------------------------------------");
        Log("Time Now         : ", Time::GetNotionalDateTimeAtSystemUs(timeNowUs));
        Log("Start/Stop Status: ", running_ ? "Started" : "Stopped");
        if (running_)
        {
            bool coastScheduled = timerCoast_.IsPending();
            Log("Coast            : ", coastScheduled ? "Scheduled" : "Not Scheduled");
            if (coastScheduled)
            {
                PrintTimeAtDetails("Coast At         ", timeNowUs, timerCoast_.GetTimeoutAtUs());
            }
            
            PrintTimeAtDetails("Window At        ", timeNowUs, timeAtUpcomingOrCurrentWindowStartUs);

            bool windowScheduled = inLockout_ || timerScheduleLockOutStart_.IsPending();
            Log("Window Scheduled : ", windowScheduled ? "Yes" : "No");
            if (windowScheduled)
            {
                uint8_t titleWidth = 2 + strlen(timerScheduleLockOutStart_.GetName());
                Log("In Window        : ", inLockout_ ? "Yes" : "No");

                vector<Timer *> timerList = {
                    &timerTxWarmup_,
                    &timerScheduleLockOutStart_,
                    &timerPeriod0_,
                    &timerPeriod1_,
                    &timerPeriod2_,
                    &timerPeriod3_,
                    &timerPeriod4_,
                    &timerPeriod5_,
                    &timerTxDisableGpsEnable_,
                    &timerScheduleLockOutEnd_,
                };

                for (Timer *timer : timerList)
                {
                    string status = timer->IsPending() ? "pend" : "done";

                    PrintTimeAtDetails(StrUtl::PadRight(string{"  "} + timer->GetName(), ' ', titleWidth) + " (" + status + ")", timeNowUs, timer->GetTimeoutAtUs());
                }
            }
        }

        // t_.ReportNow();
    }
    
    bool testing_ = false;
    void SetTesting(bool tf)
    {
        testing_ = tf;
    }

    bool IsTesting()
    {
        return testing_;
    }

    bool calculateSlotBehaviorDisabled_ = false;
    void SetTestingCalculateSlotBehaviorDisabled(bool tf)
    {
        calculateSlotBehaviorDisabled_ = tf;
    }

    bool IsTestingCalculateSlotBehaviorDisabled()
    {
        return calculateSlotBehaviorDisabled_;
    }

    bool jsDisabled_ = false;
    void SetTestingJsDisabled(bool tf)
    {
        jsDisabled_ = tf;
    }

    bool IsTestingJsDisabled()
    {
        return jsDisabled_;
    }

    void BackupFiles()
    {
        for (int i = 1; i <= 5; ++i)
        {
            FilesystemLittleFS::Move(string{"slot"} + to_string(i) + ".js", string{"slot"} + to_string(i) + ".js.bak");
            FilesystemLittleFS::Move(string{"slot"} + to_string(i) + ".json", string{"slot"} + to_string(i) + ".json.bak");
        }
    }

    void RestoreFiles()
    {
        for (int i = 1; i <= 5; ++i)
        {
            FilesystemLittleFS::Remove(string{"slot"} + to_string(i) + ".js");
            FilesystemLittleFS::Remove(string{"slot"} + to_string(i) + ".json");

            FilesystemLittleFS::Move(string{"slot"} + to_string(i) + ".js.bak", string{"slot"} + to_string(i) + ".js");
            FilesystemLittleFS::Move(string{"slot"} + to_string(i) + ".json.bak", string{"slot"} + to_string(i) + ".json");
        }
    }


    bool useMarkList_ = false;

    void SetUseMarkList(bool tf)
    {
        useMarkList_ = tf;

        if (useMarkList_ == false)
        {
            id__markList_.clear();
        }
    }

    bool UseMarkList()
    {
        return useMarkList_;
    }

    int id_ = 0;
    unordered_map<int, vector<string>> id__markList_;

    void CreateMarkList(int id)
    {
        id_ = id;
    }

    vector<string> GetMarkList()
    {
        vector<string> retVal;

        if (id__markList_.contains(id_))
        {
            retVal = id__markList_.at(id_);
        }

        return retVal;
    }

    void AddToMarkList(string str)
    {
        if (id__markList_.contains(id_) == false)
        {
            id__markList_.insert({ id_, {} });
        }

        vector<string> &markList = id__markList_.at(id_);

        markList.push_back(str);
    }

    void DestroyMarkList(int id)
    {
        id__markList_.erase(id__markList_.find(id));
    }

    void Mark(const char *str)
    {
        uint64_t timeUs = t_.Event(str);

        Log("[", TimeAt(timeUs), "] ", str);

        if (UseMarkList())
        {
            AddToMarkList(str);
        }
    }

    string TimeAt(uint64_t timeUs)
    {
        return Time::GetNotionalTimeAtSystemUs(timeUs);
    }

    uint64_t MakeUsFromGps(const FixTime &gpsFixTime)
    {
        uint64_t retVal = 0;

        // check if datetime fully fill-out-able, or just the time
        // example observed time lock
        // GPS Time: 0000-00-00 23:10:28.000 UTC

        if (gpsFixTime.year != 0)
        {
            // datetime available
            retVal = Time::MakeUsFromDateTime(gpsFixTime.dateTime);
        }
        else
        {
            // just time available
            string dt =
                Time::MakeDateTime(gpsFixTime.hour,
                                   gpsFixTime.minute,
                                   gpsFixTime.second,
                                   gpsFixTime.millisecond * 1'000);

            retVal = Time::MakeUsFromDateTime(dt);
        }

        return retVal;
    }

    void SetNotionalTimeFromGpsTime(const FixTime &gpsFixTime, uint64_t timeAtGpsFixTimeSetUs)
    {
        uint64_t notionalTimeUs = MakeUsFromGps(gpsFixTime);
        int64_t  offsetUs       = Time::SetNotionalUs(notionalTimeUs, timeAtGpsFixTimeSetUs);

        Mark("TIME_SYNC");
        Log("Time sync'd to GPS time: now ", Time::MakeDateTimeFromUs(notionalTimeUs));

        static bool didOnce = false;
        if (didOnce == false)
        {
            didOnce = true;

            Log("    (this is the first time change)");
        }
        else
        {
            if (offsetUs < 0)
            {
                Log("    Prior time was running fast by ", Time::MakeDurationFromUs((uint64_t)-offsetUs));
            }
            else if (offsetUs == 0)
            {
                Log("    Time was unchanged");
            }
            else // offsetUs > 0
            {
                Log("    Prior time was running slow by ", Time::MakeDurationFromUs((uint64_t)offsetUs));
            }
        }
    }


    /////////////////////////////////////////////////////////////////
    // Init
    /////////////////////////////////////////////////////////////////

    void SetupShell()
    {
        Shell::AddCommand("start", [this](vector<string> argList){
            Start();
        }, { .argCount = 0, .help = ""});

        Shell::AddCommand("stop", [this](vector<string> argList){
            Stop();
        }, { .argCount = 0, .help = ""});

        Shell::AddCommand("show", [this](vector<string> argList){
            PrintStatus();
        }, { .argCount = 0, .help = ""});

        Shell::AddCommand("gps", [this](vector<string> argList){
            if (argList.size() == 0)
            {
                argList.push_back("all");
            }

            TestGpsEventInterface(argList);
        }, { .argCount = -1, .help = "test gps events [<type>] [<type>] ..."});

        Shell::AddCommand("sched", [this](vector<string> argList){
            TestPrepareWindowSchedule();
        }, { .argCount = 0, .help = ""});

        Shell::AddCommand("cfg", [this](vector<string> argList){
            TestConfigureWindowSlotBehavior();
        }, { .argCount = 0, .help = "run test suite for slot behavior"});

        Shell::AddCommand("calc", [this](vector<string> argList){
            bool fullSweep = false;
            if (argList.size() == 1)
            {
                fullSweep = (bool)atoi(argList[0].c_str());
            }
            TestCalculateTimeAtWindowStartUs(fullSweep);
        }, { .argCount = -1, .help = "run test suite for window start time [fullSweep=0]"});

        Shell::AddCommand("lock", [this](vector<string> argList){
            string type = argList[0];

            if (type == "gps")
            {
                // set a time which triggers fast action.
                // we set start minute to 0, so craft a lock time
                // which triggers desired logic.
                string dateTime = "2025-01-01 12:09:50.000";
                auto tp = Time::ParseDateTime(dateTime);

                Fix3DPlus gpsFix3DPlus;
                gpsFix3DPlus.year        = tp.year;
                gpsFix3DPlus.hour        = tp.hour;
                gpsFix3DPlus.minute      = tp.minute;
                gpsFix3DPlus.second      = tp.second;
                gpsFix3DPlus.millisecond = tp.us / 1'000;
                gpsFix3DPlus.dateTime    = dateTime;

                OnGps3DPlusLock(gpsFix3DPlus);
            }
            else if (type == "time")
            {
                // set a time which triggers fast action.
                // we set start minute to 0, so craft a lock time
                // which triggers desired logic.
                string dateTime = "2025-01-01 12:09:50.000";
                auto tp = Time::ParseDateTime(dateTime);

                FixTime gpsFixTime;
                gpsFixTime.year        = tp.year;
                gpsFixTime.hour        = tp.hour;
                gpsFixTime.minute      = tp.minute;
                gpsFixTime.second      = tp.second;
                gpsFixTime.millisecond = tp.us / 1'000;
                gpsFixTime.dateTime    = dateTime;

                OnGpsTimeLock(gpsFixTime);
            }
            else
            {
                Log("Invalid type - gps or time");
            }
        }, { .argCount = 1, .help = "test gps lock <type=gps|time>"});

        Shell::AddCommand("shift", [this](vector<string> argList){
            int64_t duration = stoll(argList[0].c_str());
            string  unit     = argList[1];

            if (unit == "us")
            {
                Log("Shifting time by ", duration, " us");
                LogNL();
                ShiftTime(duration);
            }
            else if (unit == "ms")
            {
                Log("Shifting time by ", duration, " ms");
                LogNL();
                ShiftTime(duration * 1'000);
            }
            else if (unit == "sec")
            {
                Log("Shifting time by ", duration, " second", duration == 1 ? "" : "s");
                LogNL();
                ShiftTime(duration * 1'000 * 1'000);
            }
            else if (unit == "min")
            {
                Log("Shifting time by ", duration, " minute", duration == 1 ? "" : "s");
                LogNL();
                ShiftTime(duration * 60 * 1'000 * 1'000);
            }
            else
            {
                Log("ERR: Unknown unit ", unit, ", no action taken.");
            }
        }, { .argCount = 2, .help = "shift time by <duration(signed)> [unit=us|ms|sec|min]"});
    }


// private:

    Timer timerCoast_ = {"TIMER_COAST"};

    SlotState slotState1_ = { 1 };
    SlotState slotState2_ = { 2 };
    SlotState slotState3_ = { 3 };
    SlotState slotState4_ = { 4 };
    SlotState slotState5_ = { 5 };

    Timer timerTxWarmup_             = {"TIMER_TX_WARMUP"};
    Timer timerScheduleLockOutStart_ = {"TIMER_SCHEDULE_LOCK_OUT_START"};
    Timer timerPeriod0_              = {"TIMER_PERIOD0_START"};
    Timer timerPeriod1_              = {"TIMER_PERIOD1_START"};
    Timer timerPeriod2_              = {"TIMER_PERIOD2_START"};
    Timer timerPeriod3_              = {"TIMER_PERIOD3_START"};
    Timer timerPeriod4_              = {"TIMER_PERIOD4_START"};
    Timer timerPeriod5_              = {"TIMER_PERIOD5_START"};
    Timer timerTxDisableGpsEnable_   = {"TIMER_TX_DISABLE_GPS_ENABLE"};
    Timer timerScheduleLockOutEnd_   = {"TIMER_SCHEDULE_LOCK_OUT_END"};

    Timeline t_;

    CopilotControlJavaScript js_;
};
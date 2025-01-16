#pragma once

#include "CopilotControlJavaScript.h"
#include "CopilotControlMessageDefinition.h"
#include "Evm.h"
#include "GPS.h"
#include "Log.h"
#include "Shell.h"
#include "TimeClass.h"
#include "Timeline.h"

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
        SlotBehavior slotBehavior;

        bool jsRanOk = false;
    };


public:

    CopilotControlScheduler()
    {
        SetupShell();
        t_.SetMaxEvents(50);
    }


    /////////////////////////////////////////////////////////////////
    // Callback Setting - GPS Operation
    /////////////////////////////////////////////////////////////////

private:

    function<void()> fnCbRequestNewGpsLock_ = []{};

    void RequestNewGpsLock()
    {
        Mark("REQ_NEW_GPS_LOCK");
        fnCbRequestNewGpsLock_();
    }

public:

    void SetCallbackRequestNewGpsLock(function<void()> fn)
    {
        fnCbRequestNewGpsLock_ = fn;
    }


    /////////////////////////////////////////////////////////////////
    // Callback Setting - Message Sending
    /////////////////////////////////////////////////////////////////

private:

    function<void(uint64_t quitAfterMs)> fnCbSendRegularType1_   = [](uint64_t){};
    function<void(uint64_t quitAfterMs)> fnCbSendBasicTelemetry_ = [](uint64_t){};
    function<void(uint64_t quitAfterMs)> fnCbSendUserDefined_    = [](uint64_t){};

    void SendRegularType1(uint64_t quitAfterMs = 0)
    {
        Mark("SEND_REGULAR_TYPE1");
        fnCbSendRegularType1_(quitAfterMs);
    }

    void SendBasicTelemetry(uint64_t quitAfterMs = 0)
    {
        Mark("SEND_BASIC_TELEMETRY");
        fnCbSendBasicTelemetry_(quitAfterMs);
    }

    void SendCustomMessage(uint64_t quitAfterMs = 0)
    {
        Mark("SEND_CUSTOM_MESSAGE");
        fnCbSendUserDefined_(quitAfterMs);
    }

public:

    void SetCallbackSendRegularType1(function<void(uint64_t quitAfterMs)> fn)
    {
        fnCbSendRegularType1_ = fn;
    }

    void SetCallbackSendBasicTelemetry(function<void(uint64_t quitAfterMs)> fn)
    {
        fnCbSendBasicTelemetry_ = fn;
    }

    void SetCallbackSendUserDefined(function<void(uint64_t quitAfterMs)> fn)
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
        return fnCbRadioIsActive_();
    }

    void StartRadioWarmup()
    {
        Mark("ENABLE_RADIO");
        fnCbStartRadioWarmup_();
    }

    void StopRadio()
    {
        Mark("DISABLE_RADIO");
        fnCbStopRadio_();
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
        fnCbGoHighSpeed_();
    }

    void GoLowSpeed()
    {
        fnCbGoLowSpeed_();
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
    // Kickoff
    /////////////////////////////////////////////////////////////////

    void Start()
    {
        RequestNewGpsLock();
    }


    /////////////////////////////////////////////////////////////////
    // Event Handling
    /////////////////////////////////////////////////////////////////



    // A window can be in progress, or not.
        // We can accept GPS time and GPS locks in both.




    // there will sometimes be a gps lock that happens while the window is
    // still processing. this is ok.
        // this would be during js-only execution of slots that don't want to tx.
        // we have to let those run.
    // we cache these events
    // when window ends, schedule based on newly-cached data (or coast).
    // should be fine to receive multiple gps locks at any time at all
        // we keep state to know whether and when to apply
        // this would allow time-only gps and 3dplus locks to accumulate
        // and we don't care, just cache
    // when window time ends, if no cache, schedule coast








public:


private:
    Fix3DPlus gpsFix_;
public:

    void OnGpsLock(const Fix3DPlus &gpsFix)
    {
        // set the system time
        SetTimeFromGpsTime(gpsFix);
        LogNL();

        Mark("ON_GPS_LOCK");
        LogNL();

        // cache gps fix
        gpsFix_ = gpsFix;

        // learn what the system clock was the moment we aligned to gps time.
        // we want to work in system time, but display everything in gps time.
        uint64_t timeAtGpsFixUs = Time::GetSystemUsAtLastTimeChange();

        // calculate window start
        uint64_t timeAtWindowStartUs = CalculateTimeAtWindowStartUs(startMin_, gpsFix, timeAtGpsFixUs);

        Log("Duration before window start: ", Time::MakeDurationFromUs(timeAtWindowStartUs - timeAtGpsFixUs));
        LogNL();

        // prepare
        PrepareWindow(timeAtGpsFixUs, timeAtWindowStartUs, true);
    }







    // Handle scenario where only the GPS time lock happens.
    // Helps in scenario where no GPS lock ever happened yet, so we can coast on this.
    // Also will correct any drift in current coasting.
    // Make sure to wait for good non-ms time.
        // Unsure whether this even occurs in the sky when I can't get a lock anyway.
        // Can implement the feature, though.
    void OnGpsLockTimeOnly()
    {
        // adjust local wall clock time?
        // re-schedule everything?
            // when not already in a window presumably
    }




private:

    bool inWindowCurrently_ = false;

    // coast handler
    void OnNextWindowNoGpsLock()
    {
        inWindowCurrently_ = true;
        // ...
    }




    uint64_t CalculateTimeAtWindowStartUs(uint8_t windowStartMin, const FixTime &gpsFix, uint64_t timeAtGpsFixUs)
    {
        // calculate how far into the future the start minute is from gps time
        // by modelling a min/sec/ms clock and subtracting gps time from the window time.
        // the window is nominally at <m>:01.000.
        int8_t  minDiff = (int8_t)(windowStartMin - (gpsFix.minute % 10));
        int8_t  secDiff = (int8_t)(1              - gpsFix.second);
        int16_t msDiff  = (int16_t)               - gpsFix.millisecond;

        // then, since you know how far into the future the window start is from the
        // gps time, you add that duration onto the gps time and arrive at the window
        // start time.
        int64_t totalDiffMs = 0;
        totalDiffMs += minDiff *      60 * 1'000;
        totalDiffMs +=           secDiff * 1'000;
        totalDiffMs +=                     msDiff;

        // the exception is when the duration is negative, in which case you just add
        // 10 minutes.
        if (totalDiffMs < 0)
        {
            totalDiffMs += 10 * 60 * 1'000;
        }

        // calculate window start time by offset from gps time now
        uint64_t timeAtWindowStartUs = timeAtGpsFixUs + (totalDiffMs * 1'000);

        return timeAtWindowStartUs;
    }

    void TestCalculateTimeAtWindowStartUs(bool fullSweep = false);


    void PrepareWindow(uint64_t timeAtGpsFixUs, uint64_t timeAtWindowStartUs, bool haveGpsLock)
    {
        if (inWindowCurrently_)
        {
            // coasting
        }
        else
        {
            // not coasting

            // if time available to execute javascript for slot1 (required)
                // taking into account warmup period
            {
                // configure slot behavior knowing we have a gps lock
                // 100ms
                PrepareWindowSlotBehavior(haveGpsLock);

                // schedule actions based on when the next 10-min window is
                // 6ms
                PrepareWindowSchedule(timeAtGpsFixUs, timeAtWindowStartUs);
            }
            // else
            {
                // trigger coast
                // this is exactly the same as getting a gps lock within a
                // window which has started already
            }
        }
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

    bool PeriodWillRunJS(uint8_t period)
    {
        bool retVal;

        if (period == 5)
        {
            retVal = false;
        }
        else
        {
            SlotState &slotState = GetSlotState(period + 1);

            bool retVal = slotState.slotBehavior.runJs;
        }

        return retVal;
    }


    // Periods are always scheduled to be run, because they execute javascript unconditionally.
    //
    // Periods do not always send a message, though.
    //
    // When a slot is supposed to send a custom message, it will only do so if
    // the associated javascript executed successfully.
    //
    // The default is sent in cases where there was a default and a bad custom event.
    void DoPeriodBehavior(SlotState &slotStateThis, uint64_t quitAfterMs, SlotState *slotStateNext = nullptr, const char *slotNameNext = ""){
        if (slotStateThis.slotBehavior.msgSend != "none")
        {
            bool sendDefault = false;

            if (slotStateThis.slotBehavior.msgSend == "custom")
            {
                if (slotStateThis.jsRanOk)
                {
                    SendCustomMessage(quitAfterMs);
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
                if (slotStateThis.slotBehavior.hasDefault)
                {
                    if (slotStateThis.slotBehavior.canSendDefault)
                    {
                        slotStateThis.slotBehavior.fnSendDefault(quitAfterMs);
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

        if (slotStateNext && slotNameNext && slotStateNext->slotBehavior.runJs)
        {
            Mark("JS_EXEC");
            slotStateNext->jsRanOk = RunSlotJavaScript(slotNameNext, &gpsFix_);
        }
        else
        {
            Mark("JS_NO_EXEC");
        }
    };


    void PrepareWindowSlotBehavior(bool haveGpsLock)
    {
        // reset state
        slotState1_.jsRanOk = false;
        slotState2_.jsRanOk = false;
        slotState3_.jsRanOk = false;
        slotState4_.jsRanOk = false;
        slotState5_.jsRanOk = false;

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
        bool jsUsesGpsApi = js_.SlotScriptUsesAPIGPS(slotName);
        bool jsUsesMsgApi = js_.SlotScriptUsesAPIMsg(slotName);

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

    void TestConfigureWindowSlotBehavior();


    /////////////////////////////////////////////////////////////////
    // Window Schedule
    /////////////////////////////////////////////////////////////////

    // called when there is known to be enough time to do the work
    // required (eg run some js and schedule stuff)
    void PrepareWindowSchedule(uint64_t timeAtGpsFixUs, uint64_t timeAtWindowStartUs)
    {
        const uint64_t MINUTES_2_US = 2 * 60 * 1'000 * 1'000;
        
        uint64_t PERIOD1_START_TIME_US = timeAtWindowStartUs;
        uint64_t PERIOD2_START_TIME_US = PERIOD1_START_TIME_US + MINUTES_2_US;
        uint64_t PERIOD3_START_TIME_US = PERIOD2_START_TIME_US + MINUTES_2_US;
        uint64_t PERIOD4_START_TIME_US = PERIOD3_START_TIME_US + MINUTES_2_US;
        uint64_t PERIOD5_START_TIME_US = PERIOD4_START_TIME_US + MINUTES_2_US;

        if (IsTesting())
        {
            // Cause events all to fire in order quickly but with a unique
            // time so that gps can be enabled after a specific period.
            const uint64_t GAP_US = 1;

            PERIOD1_START_TIME_US = timeAtWindowStartUs;
            PERIOD2_START_TIME_US = PERIOD1_START_TIME_US + GAP_US;
            PERIOD3_START_TIME_US = PERIOD2_START_TIME_US + GAP_US;
            PERIOD4_START_TIME_US = PERIOD3_START_TIME_US + GAP_US;
            PERIOD5_START_TIME_US = PERIOD4_START_TIME_US + GAP_US;
        }

        Log("PrepareWindowSchedule for ", TimeAt(timeAtWindowStartUs));

        t_.Reset();
        Mark("PREPARE_WINDOW_SCHEDULE_START");



        // Schedule warmup.
        //
        // Set as far back as you can from the window start time, based on when it is now.
        // Possibly 0, but schedule it unconditionally.
        //
        // No need to schedule if no transmissions will occur.
        {
            uint64_t THIRTY_SECONDS_US = 30 * 1'000 * 1'000;
            uint64_t durationAvailPreWindowUs = timeAtWindowStartUs - timeAtGpsFixUs;
            uint64_t durationEarlyUs = min(durationAvailPreWindowUs, THIRTY_SECONDS_US);
            uint64_t timeAtTxWarmup = timeAtWindowStartUs - durationEarlyUs;
            tedTxWarmup_.SetCallback([this]{
                Mark("TX_WARMUP");
                StartRadioWarmup();
            }, "TX_WARMUP");
            bool doWarmup = false;
            if (PeriodWillTransmit(1)) { doWarmup = true; }
            if (PeriodWillTransmit(2)) { doWarmup = true; }
            if (PeriodWillTransmit(3)) { doWarmup = true; }
            if (PeriodWillTransmit(4)) { doWarmup = true; }
            if (PeriodWillTransmit(5)) { doWarmup = true; }
            if (doWarmup)
            {
                tedTxWarmup_.RegisterForTimedEventAt(Micros{timeAtTxWarmup});
                Log("Scheduled TX_WARMUP for ", TimeAt(timeAtTxWarmup));
                Log("    ", Time::MakeDurationFromUs(durationEarlyUs), " early");
                Log("    ", Time::MakeDurationFromUs(durationAvailPreWindowUs), " early was possible");
            }
            else
            {
                Log("Did NOT schedule TX_WARMUP, no transmissions scheduled");
            }
        }



        // Schedule GPS Req for start of window, initially.
        //
        // This lets the GPS Req beat Period1 to be executed in the event
        // that no periods are transmitters (which hold up GPS Req).
        //
        // This is adjusted later.
        // (this could all be done right now, but trying to keep this code
        //  in the same order as execution)
        {
            tedTxDisableGpsEnable_.RegisterForTimedEventAt(Micros{timeAtWindowStartUs});
            Log("Scheduled TX_DISABLE_GPS_ENABLE initially for ", TimeAt(timeAtWindowStartUs));
        }



        // Schedule Periods.
        //
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
        {
            // Execute js for slot 1 right now, starting the window events
            {
                if (slotState1_.slotBehavior.runJs)
                {
                    Mark("JS_EXEC");
                    slotState1_.jsRanOk = RunSlotJavaScript("slot1", false);
                }
                else
                {
                    Mark("JS_NO_EXEC");
                }
            }

            tedPeriod1_.SetCallback([this]{
                Mark("PERIOD1_START");
                DoPeriodBehavior(slotState1_, 0, &slotState2_, "slot2");
                Mark("PERIOD1_END");
            }, "PERIOD1_START");
            tedPeriod1_.RegisterForTimedEventAt(Micros{PERIOD1_START_TIME_US});
            Log("Scheduled PERIOD1_START for ", TimeAt(PERIOD1_START_TIME_US));

            tedPeriod2_.SetCallback([this]{
                Mark("PERIOD2_START");
                DoPeriodBehavior(slotState2_, 0, &slotState3_, "slot3");
                Mark("PERIOD2_END");
            }, "PERIOD2_START");
            tedPeriod2_.RegisterForTimedEventAt(Micros{PERIOD2_START_TIME_US});
            Log("Scheduled PERIOD2_START for ", TimeAt(PERIOD2_START_TIME_US));

            tedPeriod3_.SetCallback([this]{
                Mark("PERIOD3_START");
                DoPeriodBehavior(slotState3_, 0, &slotState4_, "slot4");
                Mark("PERIOD3_END");
            }, "PERIOD3_START");
            tedPeriod3_.RegisterForTimedEventAt(Micros{PERIOD3_START_TIME_US});
            Log("Scheduled PERIOD3_START for ", TimeAt(PERIOD3_START_TIME_US));

            tedPeriod4_.SetCallback([this]{
                Mark("PERIOD4_START");
                DoPeriodBehavior(slotState4_, 0, &slotState5_, "slot5");
                Mark("PERIOD4_END");
            }, "PERIOD4_START");
            tedPeriod4_.RegisterForTimedEventAt(Micros{PERIOD4_START_TIME_US});
            Log("Scheduled PERIOD4_START for ", TimeAt(PERIOD4_START_TIME_US));

            tedPeriod5_.SetCallback([this]{
                Mark("PERIOD5_START");
                // tell sender to quit early
                const uint64_t ONE_MINUTE_MS = 1 * 60 * 1'000;
                DoPeriodBehavior(slotState5_, ONE_MINUTE_MS);
                Mark("PERIOD5_END");
            }, "PERIOD5_START");
            tedPeriod5_.RegisterForTimedEventAt(Micros{PERIOD5_START_TIME_US});
            Log("Scheduled PERIOD5_START for ", TimeAt(PERIOD5_START_TIME_US));
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
        //
        uint64_t timeAtChangeUs = timeAtWindowStartUs;
        bool rescheduled = false;
        if (PeriodWillTransmit(1)) { timeAtChangeUs = PERIOD1_START_TIME_US; rescheduled = true; }
        if (PeriodWillTransmit(2)) { timeAtChangeUs = PERIOD2_START_TIME_US; rescheduled = true; }
        if (PeriodWillTransmit(3)) { timeAtChangeUs = PERIOD3_START_TIME_US; rescheduled = true; }
        if (PeriodWillTransmit(4)) { timeAtChangeUs = PERIOD4_START_TIME_US; rescheduled = true; }
        if (PeriodWillTransmit(5)) { timeAtChangeUs = PERIOD5_START_TIME_US; rescheduled = true; }
        tedTxDisableGpsEnable_.SetCallback([this]{
            Mark("TX_DISABLE_GPS_ENABLE");

            // disable transmitter
            StopRadio();

            // enable gps
            RequestNewGpsLock();
        }, "TX_DISABLE_GPS_ENABLE");
        if (rescheduled)
        {
            tedTxDisableGpsEnable_.RegisterForTimedEventAt(Micros{timeAtChangeUs});
            Log("Re-Scheduled TX_DISABLE_GPS_ENABLE for ", TimeAt(timeAtChangeUs));
        }



        // Schedule Window End.
        //
        // This event should come after the final period of work.
        //
        // It is no harm to end the window period after the 5th period
        // even if no work gets done.
        tedWindowEnd_.SetCallback([this]{
            Mark("WINDOW_END");

            if (IsTesting() == false)
            {
                t_.Report();
            }
        }, "WINDOW_END");
        uint64_t timeAtWindowEndUs = PERIOD5_START_TIME_US;
        tedWindowEnd_.RegisterForTimedEventAt(Micros{timeAtWindowEndUs});
        Log("Scheduled Window End for ", TimeAt(timeAtWindowEndUs));



        Mark("PREPARE_WINDOW_SCHEDULE_END");
    }

    void TestPrepareWindowSchedule();


    /////////////////////////////////////////////////////////////////
    // JavaScript Execution
    /////////////////////////////////////////////////////////////////

    bool RunSlotJavaScript(const string &slotName, bool operateRadio = true)
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
        auto jsResult = js_.RunSlotJavaScript(slotName, &gpsFix_);
        retVal = jsResult.runOk;

        // change to 6MHz
        GoLowSpeed();

        if (radioActive)
        {
            StartRadioWarmup();
        }

        return retVal;
    }


    /////////////////////////////////////////////////////////////////
    // Utility
    /////////////////////////////////////////////////////////////////
    
    bool testing_ = false;
    void SetTesting(bool tf)
    {
        testing_ = tf;
    }

    bool IsTesting()
    {
        return testing_;
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

        if (IsTesting())
        {
            AddToMarkList(str);
        }
    }

    string TimeAt(uint64_t timeUs)
    {
        return Time::GetNotionalTimeFromSystemUs(timeUs);
    }


    uint64_t MakeUsFromGps(const FixTime &gpsFix)
    {
        uint64_t retVal = 0;

        // check if datetime fully fill-out-able, or just the time
        // example observed time lock
        // GPS Time: 0000-00-00 23:10:28.000 UTC

        if (gpsFix.year != 0)
        {
            // datetime available
            retVal = Time::MakeUsFromDateTime(gpsFix.dateTime);
        }
        else
        {
            // just time available
            string dt =
                Time::MakeDateTime(gpsFix.hour,
                                   gpsFix.minute,
                                   gpsFix.second,
                                   gpsFix.millisecond * 1'000);

            retVal = Time::MakeUsFromDateTime(dt);
        }

        return retVal;
    }

    void SetTimeFromGpsTime(const FixTime &gpsFix)
    {
        uint64_t notionalTimeUs = MakeUsFromGps(gpsFix);

        int64_t offsetUs = Time::SetNotionalDateTimeUs(notionalTimeUs);

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
            else
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
        Shell::AddCommand("test.sched", [this](vector<string> argList){
            TestPrepareWindowSchedule();
        }, { .argCount = 0, .help = ""});

        Shell::AddCommand("test.l", [this](vector<string> argList){
        }, { .argCount = 0, .help = ""});

        Shell::AddCommand("test.cfg", [this](vector<string> argList){
            TestConfigureWindowSlotBehavior();
        }, { .argCount = 0, .help = "run test suite for slot behavior"});

        Shell::AddCommand("test.calc", [this](vector<string> argList){
            bool fullSweep = false;
            if (argList.size() == 1)
            {
                fullSweep = (bool)atoi(argList[0].c_str());
            }
            TestCalculateTimeAtWindowStartUs(fullSweep);
        }, { .argCount = -1, .help = "run test suite for window start time [fullSweep=0]"});

        Shell::AddCommand("test.gps", [this](vector<string> argList){
            // set a time which triggers fast action.
            // we set start minute to 0, so craft a lock time
            // which triggers desired logic.
            string dateTime = "2025-01-01 12:09:50.000";
            auto tp = Time::ParseDateTime(dateTime);

            Fix3DPlus gpsFix;
            gpsFix.year        = tp.year;
            gpsFix.hour        = tp.hour;
            gpsFix.minute      = tp.minute;
            gpsFix.second      = tp.second;
            gpsFix.millisecond = tp.us / 1'000;
            gpsFix.dateTime    = dateTime;

            SetStartMinute(0);
            OnGpsLock(gpsFix);
        }, { .argCount = 0, .help = "test gps lock"});
    }


private:

    SlotState slotState1_;
    SlotState slotState2_;
    SlotState slotState3_;
    SlotState slotState4_;
    SlotState slotState5_;

    TimedEventHandlerDelegate tedTxWarmup_;
    TimedEventHandlerDelegate tedPeriod1_;
    TimedEventHandlerDelegate tedPeriod2_;
    TimedEventHandlerDelegate tedPeriod3_;
    TimedEventHandlerDelegate tedPeriod4_;
    TimedEventHandlerDelegate tedPeriod5_;
    TimedEventHandlerDelegate tedTxDisableGpsEnable_;
    TimedEventHandlerDelegate tedWindowEnd_;

    Timeline t_;

    CopilotControlJavaScript js_;
};
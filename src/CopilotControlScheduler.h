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
    }


    /////////////////////////////////////////////////////////////////
    // Callback Setting - GPS Operation
    /////////////////////////////////////////////////////////////////

private:

    function<void()> fnCbRequestNewGpsLock_ = []{};

    void RequestNewGpsLock()
    {
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
        Mark("GO_HIGH_SPEED");
        fnCbGoHighSpeed_();
    }

    void GoLowSpeed()
    {
        Mark("GO_LOW_SPEED");
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

public:


private:
    Fix3DPlus gpsFix_;
public:

    void OnGpsLock(const Fix3DPlus &gpsFix)
    {
        // set the system time
        SetTimeFromGpsTime(gpsFix);

        Mark("ON_GPS_LOCK");

        // cache gps fix
        gpsFix_ = gpsFix;

        // learn what the system clock was the moment we aligned to gps time.
        // we want to work in system time, but display everything in gps time.
        uint64_t timeAtGpsFixUs = Time::GetSystemUsAtLastTimeChange();

        // calculate window start
        uint64_t timeAtWindowStartMs = CalculateTimeAtWindowStartMs(startMin_, gpsFix, timeAtGpsFixUs);

        // prepare
        PrepareWindow(timeAtWindowStartMs, true);
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




    uint64_t CalculateTimeAtWindowStartMs(uint8_t windowStartMin, const FixTime &gpsFix, uint64_t timeAtGpsFixUs)
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
        uint64_t timeAtWindowStartMs = (timeAtGpsFixUs / 1'000) + totalDiffMs;

        return timeAtWindowStartMs;
    }

    void TestCalculateTimeAtWindowStartMs();


    void PrepareWindow(uint64_t timeAtWindowStartMs, bool haveGpsLock)
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
                PrepareWindowSchedule(timeAtWindowStartMs);
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

    // Slots are always scheduled to be run, because they execute javascript unconditionally.
    //
    // Slots do not always send a message, though.
    //
    // When a slot is supposed to send a custom message, it will only do so if
    // the associated javascript executed successfully.
    //
    // The default is sent in cases where there was a default and a bad custom event.
    void DoSlotBehavior(SlotState &slotStateThis, uint64_t quitAfterMs, SlotState *slotStateNext = nullptr, const char *slotNameNext = ""){
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
    void PrepareWindowSchedule(uint64_t timeAtWindowStartMs)
    {
        const uint64_t MINUTES_2_MS = 2 * 60 * 1'000;
        
        uint64_t SLOT1_START_TIME_MS = timeAtWindowStartMs + 1; // allow gps room to start first
        uint64_t SLOT2_START_TIME_MS = SLOT1_START_TIME_MS + MINUTES_2_MS;
        uint64_t SLOT3_START_TIME_MS = SLOT2_START_TIME_MS + MINUTES_2_MS;
        uint64_t SLOT4_START_TIME_MS = SLOT3_START_TIME_MS + MINUTES_2_MS;
        uint64_t SLOT5_START_TIME_MS = SLOT4_START_TIME_MS + MINUTES_2_MS;

        if (IsTesting())
        {
            // cause events all to fire in order quickly but with a unique
            // time so that gps can be enabled after a specific slot
            const uint64_t GAP_MS = 1;

            SLOT1_START_TIME_MS = timeAtWindowStartMs + GAP_MS;
            SLOT2_START_TIME_MS = SLOT1_START_TIME_MS + GAP_MS;
            SLOT3_START_TIME_MS = SLOT2_START_TIME_MS + GAP_MS;
            SLOT4_START_TIME_MS = SLOT3_START_TIME_MS + GAP_MS;
            SLOT5_START_TIME_MS = SLOT4_START_TIME_MS + GAP_MS;
        }

        Log("PrepareWindowSchedule at ", TimeAt(timeAtWindowStartMs));

        t_.Reset();
        Mark("PREPARE_SLOT_BEHAVIOR");


        // execute js for slot 1 right now
        if (slotState1_.slotBehavior.runJs)
        {
            Mark("JS_EXEC");
            slotState1_.jsRanOk = RunSlotJavaScript("slot1", false);
        }
        else
        {
            Mark("JS_NO_EXEC");
        }


        // Set timer for warmup.
        //
        // Set as far back as you can from the window start time, based on when it is now.
        // Possibly 0, but schedule it unconditionally.
        uint64_t timeAtTxWarmup = 0;
        tedTxWarmup_.SetCallback([this]{
            Mark("TX_WARMUP");
            StartRadioWarmup();
        }, "TX_WARMUP");
        tedTxWarmup_.RegisterForTimedEventAt(timeAtTxWarmup);
        Log("Scheduled TX_WARMUP for ", TimeAt(timeAtTxWarmup));


        // schedule slots

        tedSlot1_.SetCallback([this]{
            Mark("SLOT1_START");
            DoSlotBehavior(slotState1_, 0, &slotState2_, "slot2");
            Mark("SLOT1_END");
        }, "SLOT1_START");
        tedSlot1_.RegisterForTimedEventAt(SLOT1_START_TIME_MS);
        Log("Scheduled SLOT1_START for ", TimeAt(SLOT1_START_TIME_MS));


        tedSlot2_.SetCallback([this]{
            Mark("SLOT2_START");
            DoSlotBehavior(slotState2_, 0, &slotState3_, "slot3");
            Mark("SLOT2_END");
        }, "SLOT2_START");
        tedSlot2_.RegisterForTimedEventAt(SLOT2_START_TIME_MS);
        Log("Scheduled SLOT2_START for ", TimeAt(SLOT2_START_TIME_MS));


        tedSlot3_.SetCallback([this]{
            Mark("SLOT3_START");
            DoSlotBehavior(slotState3_, 0, &slotState4_, "slot4");
            Mark("SLOT3_END");
        }, "SLOT3_START");
        tedSlot3_.RegisterForTimedEventAt(SLOT3_START_TIME_MS);
        Log("Scheduled SLOT3_START for ", TimeAt(SLOT3_START_TIME_MS));


        tedSlot4_.SetCallback([this]{
            Mark("SLOT4_START");
            DoSlotBehavior(slotState4_, 0, &slotState5_, "slot5");
            Mark("SLOT4_END");
        }, "SLOT4_START");
        tedSlot4_.RegisterForTimedEventAt(SLOT4_START_TIME_MS);
        Log("Scheduled SLOT4_START for ", TimeAt(SLOT4_START_TIME_MS));


        tedSlot5_.SetCallback([this]{
            Mark("SLOT5_START");

            // tell sender to quit early
            const uint64_t ONE_MINUTE_MS = 1 * 60 * 1'000;
            DoSlotBehavior(slotState5_, ONE_MINUTE_MS);


            // execute js for slot 1
                // ohh, shit, that can't work, no new gps lock!
                // this will have to be part of what happens on new gps lock
                    // which I have to work out the bootstrapping there anyway
                // need to update the docs

            Mark("SLOT5_END");
        }, "SLOT5_START");
        tedSlot5_.RegisterForTimedEventAt(SLOT5_START_TIME_MS);
        Log("Scheduled SLOT5_START for ", TimeAt(SLOT5_START_TIME_MS));


        // Determine when to enable the gps.
        //
        // Enable the moment the last transmitting slot is finished.
        //
        // Schedule for the same start moment as the slot itself, knowing that
        // this event, scheduled second, will execute directly after,
        // (which is as early as possible, and what we want).
        //
        // Optimization would be for slot behavior to detect that:
        // - it is the last slot before gps
        // - trigger gps early if no message to send (eg bad js and no default)
        //
        uint64_t timeAtChangeMs = timeAtWindowStartMs;
        if (slotState1_.slotBehavior.msgSend != "none") { timeAtChangeMs = SLOT1_START_TIME_MS; }
        if (slotState2_.slotBehavior.msgSend != "none") { timeAtChangeMs = SLOT2_START_TIME_MS; }
        if (slotState3_.slotBehavior.msgSend != "none") { timeAtChangeMs = SLOT3_START_TIME_MS; }
        if (slotState4_.slotBehavior.msgSend != "none") { timeAtChangeMs = SLOT4_START_TIME_MS; }
        if (slotState5_.slotBehavior.msgSend != "none") { timeAtChangeMs = SLOT5_START_TIME_MS; }

        tedTxDisableGpsEnable_.SetCallback([this]{
            Mark("TX_DISABLE_GPS_ENABLE");

            // disable transmitter
            StopRadio();

            // enable gps
            RequestNewGpsLock();


            // setup coast directly from here?


            if (IsTesting() == false)
            {
                t_.Report();
            }
        }, "TX_DISABLE_GPS_ENABLE");
        tedTxDisableGpsEnable_.RegisterForTimedEventAt(timeAtChangeMs);
        Log("Scheduled TX_DISABLE_GPS_ENABLE for ", TimeAt(timeAtChangeMs));
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

        Log("[", TimeAt(timeUs / 1'000), "] ", str);

        if (IsTesting())
        {
            AddToMarkList(str);
        }
    }

    string TimeAt(uint64_t timeMs)
    {
        return Time::GetNotionalTimeShortFromSystemMs(timeMs);
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
        int64_t offsetUs = Time::SetNotionalDateTimeUs(MakeUsFromGps(gpsFix));

        Log("Time sync'd to GPS time: now ", Time::GetNotionalDateTime());

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
                Log("    Prior time was running fast by ", Time::MakeTimeFromUs((uint64_t)-offsetUs));
            }
            else
            {
                Log("    Prior time was running slow by ", Time::MakeTimeFromUs((uint64_t)offsetUs));
            }
        }
    }


    /////////////////////////////////////////////////////////////////
    // Init
    /////////////////////////////////////////////////////////////////

    void SetupShell()
    {
        Shell::AddCommand("test.s", [this](vector<string> argList){
            TestPrepareWindowSchedule();
        }, { .argCount = 0, .help = ""});

        Shell::AddCommand("test.l", [this](vector<string> argList){
        }, { .argCount = 0, .help = ""});

        Shell::AddCommand("test.cfg", [this](vector<string> argList){
            TestConfigureWindowSlotBehavior();
        }, { .argCount = 0, .help = "run test suite for slot behavior"});

        Shell::AddCommand("test.calc", [this](vector<string> argList){
            TestCalculateTimeAtWindowStartMs();
        }, { .argCount = 0, .help = "run test suite for window start time"});

        Shell::AddCommand("test.gps", [this](vector<string> argList){
            // craft a fix which causes our desired execution flow
            Fix3DPlus gpsFix;
            
            // ensure the dateTime parameter is used
            gpsFix.year = 1;

            // set a time which triggers fast action.
            // default start minute is 0, so give it a head start.
            gpsFix.dateTime = "2025-01-01 12:09:20.000000";

            SetTesting(true);
            OnGpsLock(gpsFix);
            SetTesting(false);
        }, { .argCount = 0, .help = "test gps lock"});
    }


private:

    SlotState slotState1_;
    SlotState slotState2_;
    SlotState slotState3_;
    SlotState slotState4_;
    SlotState slotState5_;

    TimedEventHandlerDelegate tedTxWarmup_;
    TimedEventHandlerDelegate tedSlot1_;
    TimedEventHandlerDelegate tedSlot2_;
    TimedEventHandlerDelegate tedSlot3_;
    TimedEventHandlerDelegate tedSlot4_;
    TimedEventHandlerDelegate tedSlot5_;
    TimedEventHandlerDelegate tedTxDisableGpsEnable_;

    Timeline t_;

    CopilotControlJavaScript js_;
};
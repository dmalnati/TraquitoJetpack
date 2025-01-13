#pragma once

#include "CopilotControlJavaScript.h"
#include "CopilotControlMessageDefinition.h"
#include "Evm.h"
#include "Log.h"
#include "Shell.h"
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

        bool             canSendDefault = false;
        function<void()> fnSendDefault  = []{};
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


private:



    /////////////////////////////////////////////////////////////////
    // Event Handling
    /////////////////////////////////////////////////////////////////


    // Handle scenario where only the GPS time lock happens.
    // Helps in scenario where no GPS lock ever happened yet, so we can coast on this.
    // Also will correct any drift in current coasting.
    // Make sure to wait for good non-ms time.
        // Unsure whether this even occurs in the sky when I can't get a lock anyway.
        // Can implement the feature, though.
    void OnGpsTimeLockOnly()
    {

    }



    bool inWindowCurrently_ = false;

    // coast handler
    void OnNextWindowNoGpsLock()
    {
        inWindowCurrently_ = true;
        // ...
    }






    bool gpsHasLock_ = false;

    void OnGpsLock(uint8_t gpsTimeMin, uint8_t gpsTimeSec, uint16_t gpsTimeMs)
    {
        // you get the lock and now know exactly what time it is

        // calculate the time of the next starting window as a moment in the future
            // an absolute time
        uint64_t timeAtWindowStartMs = 0;



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
                ConfigureWindowSlotBehavior(gpsHasLock_);

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
    // Slot Behavior
    /////////////////////////////////////////////////////////////////

    void ConfigureWindowSlotBehavior(bool haveGpsLock)
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
                                  [this]{ SendRegularType1(); });

        // slot 2
        slotState2_.slotBehavior =
            CalculateSlotBehavior("slot2",
                                  haveGpsLock,
                                  "default",
                                  [this]{ SendBasicTelemetry(); });

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
    SlotBehavior CalculateSlotBehavior(const string     &slotName,
                                       bool              haveGpsLock,
                                       string            msgSendDefault = "none",
                                       function<void()>  fnSendDefault = []{})
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

            .canSendDefault = haveGpsLock,
            .fnSendDefault  = fnSendDefault,
        };

        return retVal;
    }

    void TestConfigureWindowSlotBehavior()
    {
        BackupFiles();

        // set up test case primitives
        string msgDefBlank = "";
        string msgDefSet = "{ \"name\": \"Altitude\", \"unit\": \"Meters\", \"lowValue\": 0, \"highValue\": 21340, \"stepSize\": 20 },";

        string jsUsesNeither = "";
        string jsUsesGps = "gps.GetAltitudeMeters();";
        string jsUsesMsg = "msg.SetAltitudeMeters(1);";
        string jsUsesBoth = jsUsesGps + jsUsesMsg;

        auto SetSlot = [](const string &slotName, const string &msgDef, const string &js){
            FilesystemLittleFS::Write(slotName + ".json", msgDef);
            FilesystemLittleFS::Write(slotName + ".js", js);
        };

        // set up checking outcome
        auto Assert = [](string slotName, const SlotBehavior &slotBehavior, bool runJs, string msgSend, bool canSendDefault){
            bool retVal = true;

            if (slotBehavior.runJs != runJs || slotBehavior.msgSend != msgSend || slotBehavior.canSendDefault != canSendDefault)
            {
                retVal = false;

                Log("Assert ERR: ", slotName);

                if (slotBehavior.runJs != runJs)
                {
                    Log("- runJs expected(", runJs, ") != actual(", slotBehavior.runJs, ")");
                }

                if (slotBehavior.msgSend != msgSend)
                {
                    Log("- msgSend expected(", msgSend, ") != actual(", slotBehavior.msgSend, ")");
                }

                if (slotBehavior.canSendDefault != canSendDefault)
                {
                    Log("- canSendDefault expected(", canSendDefault, ") != actual(", slotBehavior.canSendDefault, ")");
                }
            }

            return retVal;
        };


        // run test cases
        LogNL();

        bool ok = true;

        {
            Log("=== Testing GPS = False, w/ Msg Def ===");
            LogNL();

            bool haveGpsLock = false;
            SetSlot("slot1", msgDefSet, jsUsesNeither);
            SetSlot("slot2", msgDefSet, jsUsesMsg);
            SetSlot("slot3", msgDefSet, jsUsesGps);
            SetSlot("slot4", msgDefSet, jsUsesBoth);

            ConfigureWindowSlotBehavior(haveGpsLock);

            bool testsOk = true;
            testsOk &= Assert("slot1", slotState1_.slotBehavior, true,  "none",   haveGpsLock);
            testsOk &= Assert("slot2", slotState2_.slotBehavior, true,  "custom", haveGpsLock);
            testsOk &= Assert("slot3", slotState3_.slotBehavior, false, "none",   haveGpsLock);
            testsOk &= Assert("slot4", slotState4_.slotBehavior, false, "none",   haveGpsLock);

            ok &= testsOk;

            Log("=== Tests ", testsOk ? "" : "NOT ", "ok ===");
            LogNL();
        }

        {
            Log("=== Testing GPS = False, w/ no Msg Def ===");
            LogNL();

            bool haveGpsLock = false;
            SetSlot("slot1", msgDefBlank, jsUsesNeither);
            SetSlot("slot2", msgDefBlank, jsUsesMsg);
            SetSlot("slot3", msgDefBlank, jsUsesGps);
            SetSlot("slot4", msgDefBlank, jsUsesBoth);

            ConfigureWindowSlotBehavior(haveGpsLock);

            bool testsOk = true;
            testsOk &= Assert("slot1", slotState1_.slotBehavior, true,  "none", haveGpsLock);
            testsOk &= Assert("slot2", slotState2_.slotBehavior, true,  "none", haveGpsLock);
            testsOk &= Assert("slot3", slotState3_.slotBehavior, false, "none", haveGpsLock);
            testsOk &= Assert("slot4", slotState4_.slotBehavior, false, "none", haveGpsLock);

            ok &= testsOk;

            Log("=== Tests ", testsOk ? "" : "NOT ", "ok ===");
            LogNL();
        }

        {
            Log("=== Testing GPS = True, w/ Msg Def ===");
            LogNL();

            bool haveGpsLock = true;
            SetSlot("slot1", msgDefSet, jsUsesNeither);
            SetSlot("slot2", msgDefSet, jsUsesMsg);
            SetSlot("slot3", msgDefSet, jsUsesGps);
            SetSlot("slot4", msgDefSet, jsUsesBoth);

            ConfigureWindowSlotBehavior(haveGpsLock);

            bool testsOk = true;
            testsOk &= Assert("slot1", slotState1_.slotBehavior, true, "default", haveGpsLock);
            testsOk &= Assert("slot2", slotState2_.slotBehavior, true, "custom",  haveGpsLock);
            // here it's "none" because there is no actual default function, but if there was
            // a default function, it would fire here
            testsOk &= Assert("slot3", slotState3_.slotBehavior, true, "none",    haveGpsLock);
            testsOk &= Assert("slot4", slotState4_.slotBehavior, true, "custom",  haveGpsLock);

            ok &= testsOk;

            Log("=== Tests ", testsOk ? "" : "NOT ", "ok ===");
            LogNL();
        }

        {
            Log("=== Testing GPS = True, w/ no Msg Def ===");
            LogNL();

            bool haveGpsLock = true;
            SetSlot("slot1", msgDefBlank, jsUsesNeither);
            SetSlot("slot2", msgDefBlank, jsUsesMsg);
            SetSlot("slot3", msgDefBlank, jsUsesGps);
            SetSlot("slot4", msgDefBlank, jsUsesBoth);

            ConfigureWindowSlotBehavior(haveGpsLock);

            bool testsOk = true;
            testsOk &= Assert("slot1", slotState1_.slotBehavior, true, "default", haveGpsLock);
            testsOk &= Assert("slot2", slotState2_.slotBehavior, true, "default", haveGpsLock);
            testsOk &= Assert("slot3", slotState3_.slotBehavior, true, "none",    haveGpsLock);
            testsOk &= Assert("slot4", slotState4_.slotBehavior, true, "none",    haveGpsLock);

            ok &= testsOk;

            Log("=== Tests ", testsOk ? "" : "NOT ", "ok ===");
            LogNL();
        }

        Log("=== ALL Tests ", ok ? "" : "NOT ", "ok ===");
        LogNL();

        RestoreFiles();
    }



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


        Log("PrepareWindowSchedule at ", TimestampFromMs(timeAtWindowStartMs));

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
            // enable warmup
        }, "TX_WARMUP");
        tedTxWarmup_.RegisterForTimedEventAt(timeAtTxWarmup);
        Log("Scheduled TX_WARMUP for ", TimestampFromMs(timeAtTxWarmup));





        // Slots are always scheduled to be run, because they execute javascript unconditionally.
        //
        // Slots do not always send a message, though.
        //
        // When a slot is supposed to send a custom message, it will only do so if
        // the associated javascript executed successfully.
        //
        // The default is sent in cases where there was a default and a bad custom event.
        static auto FnSlotBehavior = [this](SlotState &slotStateThis, SlotState *slotStateNext = nullptr, const char *slotNameNext = ""){
            if (slotStateThis.slotBehavior.msgSend != "none")
            {
                if (slotStateThis.jsRanOk && slotStateThis.slotBehavior.msgSend == "custom")
                {
                    SendCustomMessage();
                }
                else if (slotStateThis.slotBehavior.canSendDefault)
                {
                    // send default
                    slotStateThis.slotBehavior.fnSendDefault();
                }
                else
                {
                    Mark("SEND_NO_MSG_NO_GPS");
                }
            }
            else
            {
                Mark("SEND_NO_MSG_NONE");
            }

            if (slotStateNext && slotNameNext && slotStateNext->slotBehavior.runJs)
            {
                Mark("JS_EXEC");
                slotStateNext->jsRanOk = RunSlotJavaScript(slotNameNext);
            }
            else
            {
                Mark("JS_NO_EXEC");
            }
        };




        tedSlot1_.SetCallback([this]{
            Mark("SLOT1_START");
            FnSlotBehavior(slotState1_, &slotState2_, "slot2");
            Mark("SLOT1_END");
        }, "SLOT1_START");
        tedSlot1_.RegisterForTimedEventAt(SLOT1_START_TIME_MS);
        Log("Scheduled SLOT1_START for ", TimestampFromMs(SLOT1_START_TIME_MS));



        tedSlot2_.SetCallback([this]{
            Mark("SLOT2_START");
            FnSlotBehavior(slotState2_, &slotState3_, "slot3");
            Mark("SLOT2_END");
        }, "SLOT2_START");
        tedSlot2_.RegisterForTimedEventAt(SLOT2_START_TIME_MS);
        Log("Scheduled SLOT2_START for ", TimestampFromMs(SLOT2_START_TIME_MS));



        tedSlot3_.SetCallback([this]{
            Mark("SLOT3_START");
            FnSlotBehavior(slotState3_, &slotState4_, "slot4");
            Mark("SLOT3_END");
        }, "SLOT3_START");
        tedSlot3_.RegisterForTimedEventAt(SLOT3_START_TIME_MS);
        Log("Scheduled SLOT3_START for ", TimestampFromMs(SLOT3_START_TIME_MS));



        tedSlot4_.SetCallback([this]{
            Mark("SLOT4_START");
            FnSlotBehavior(slotState4_, &slotState5_, "slot5");
            Mark("SLOT4_END");
        }, "SLOT4_START");
        tedSlot4_.RegisterForTimedEventAt(SLOT4_START_TIME_MS);
        Log("Scheduled SLOT4_START for ", TimestampFromMs(SLOT4_START_TIME_MS));


        tedSlot5_.SetCallback([this]{
            Mark("SLOT5_START");

            // set transmitter to quit early
            FnSlotBehavior(slotState5_);
            // un-set transmitter to quit early




            // execute js for slot 1
                // ohh, shit, that can't work, no new gps lock!
                // this will have to be part of what happens on new gps lock
                    // which I have to work out the bootstrapping there anyway
                // need to update the docs

            Mark("SLOT5_END");
        }, "SLOT5_START");
        tedSlot5_.RegisterForTimedEventAt(SLOT5_START_TIME_MS);
        Log("Scheduled SLOT5_START for ", TimestampFromMs(SLOT5_START_TIME_MS));







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
            // enable gps



            // setup coast directly from here?


            if (IsTesting() == false)
            {
                t_.Report();
            }
        }, "TX_DISABLE_GPS_ENABLE");
        tedTxDisableGpsEnable_.RegisterForTimedEventAt(timeAtChangeMs);
        Log("Scheduled TX_DISABLE_GPS_ENABLE for ", TimestampFromMs(timeAtChangeMs));
    }

    void TestPrepareWindowSchedule()
    {
        BackupFiles();

        Log("TestPrepareWindowSchedule Start");




        // set up test case primitives
        static string msgDefBlank = "";
        static string msgDefSet = "{ \"name\": \"Altitude\", \"unit\": \"Meters\", \"lowValue\": 0, \"highValue\": 21340, \"stepSize\": 20 },";

        static string jsUsesNeither = "";
        static string jsUsesGps = "gps.GetAltitudeMeters();";
        static string jsUsesMsg = "msg.SetAltitudeMeters(1);";
        static string jsUsesBoth = jsUsesGps + jsUsesMsg;

        static auto SetSlot = [](const string &slotName, const string &msgDef, const string &js){
            FilesystemLittleFS::Write(slotName + ".json", msgDef);
            FilesystemLittleFS::Write(slotName + ".js", js);
        };


        // expected is a subset of actual, but all elements need to be found
        // in the order expressed for it to be a match
        static auto Assert = [](int id, vector<string> actualList, vector<string> expectedList){
            bool retVal = true;

            while (expectedList.size())
            {
                // pop first element from expected list
                string expected = *expectedList.begin();
                expectedList.erase(expectedList.begin());

                // remove non-matching elements from actual list
                while (actualList.size() && expected != *actualList.begin())
                {
                    actualList.erase(actualList.begin());
                }

                // remove the element you just compared equal to (if exists)
                if (actualList.size())
                {
                    actualList.erase(actualList.begin());
                }

                // if now empty, the compare is over
                if (actualList.size() == 0)
                {
                    break;
                }
            }

            retVal = expectedList.size() == 0;

            if (retVal == false)
            {
                Log("Assert ERR: test", id);
                Log("Expected items remain:");
                for (const auto &str : expectedList)
                {
                    Log("  ", str);
                }
            }

            return retVal;
        };

        static auto PrintMarkList = [](vector<string> &markList){
            Log("Mark List:");
            for (const auto &mark : markList)
            {
                Log("  ", mark);
            }
        };

        static auto NextTestDuration = [&]{
            static uint64_t durationMs = 0;

            uint64_t retVal = durationMs;
            
            // works at 125 MHz
            durationMs += 1500;

            return retVal;
        };

        static auto IncrAndGetTestId = [&]{
            static int id = 0;
            ++id;
            return id;
        };


        // default behavior, with gps lock
        {
            static TimedEventHandlerDelegate tedTestOuter;
            tedTestOuter.SetCallback([this]{
                static TimedEventHandlerDelegate tedTestInner;

                SetTesting(true);
                int id = IncrAndGetTestId();
                CreateMarkList(id);

                bool haveGpsLock = true;
                SetSlot("slot1", msgDefBlank, jsUsesNeither);
                SetSlot("slot2", msgDefBlank, jsUsesNeither);
                SetSlot("slot3", msgDefBlank, jsUsesNeither);
                SetSlot("slot4", msgDefBlank, jsUsesNeither);
                SetSlot("slot5", msgDefBlank, jsUsesNeither);
                ConfigureWindowSlotBehavior(haveGpsLock);
                PrepareWindowSchedule(0);

                tedTestInner.SetCallback([this, id]{
                    SetTesting(false);

                    vector<string> expectedList = {
                        "JS_EXEC",               "SEND_REGULAR_TYPE1",      // slot 1
                        "JS_EXEC",               "SEND_BASIC_TELEMETRY",    // slot 2
                        "JS_EXEC",                                          // slot 3 js
                        "TX_DISABLE_GPS_ENABLE",
                                                 "SEND_NO_MSG_NONE",        // slot 3 msg
                        "JS_EXEC",               "SEND_NO_MSG_NONE",        // slot 4
                        "JS_EXEC",               "SEND_NO_MSG_NONE",        // slot 5
                    };

                    bool testOk = Assert(id, GetMarkList(), expectedList);
                    DestroyMarkList(id);

                    LogNL();
                    Log("=== Test ", id, " ", testOk ? "" : "NOT ", "ok ===");
                    LogNL();
                });
                tedTestInner.RegisterForTimedEvent(50);
            });
            tedTestOuter.RegisterForTimedEvent(NextTestDuration());
        }

        // default behavior, with no gps lock
        {
            static TimedEventHandlerDelegate tedTestOuter;
            tedTestOuter.SetCallback([this]{
                static TimedEventHandlerDelegate tedTestInner;

                SetTesting(true);
                int id = IncrAndGetTestId();
                CreateMarkList(id);

                bool haveGpsLock = false;
                SetSlot("slot1", msgDefBlank, jsUsesNeither);
                SetSlot("slot2", msgDefBlank, jsUsesNeither);
                SetSlot("slot3", msgDefBlank, jsUsesNeither);
                SetSlot("slot4", msgDefBlank, jsUsesNeither);
                SetSlot("slot5", msgDefBlank, jsUsesNeither);
                ConfigureWindowSlotBehavior(haveGpsLock);
                PrepareWindowSchedule(0);

                tedTestInner.SetCallback([this, id]{
                    SetTesting(false);

                    vector<string> expectedList = {
                        "JS_EXEC",                                      // slot 1 js
                        "TX_DISABLE_GPS_ENABLE",
                                                 "SEND_NO_MSG_NONE",    // slot 1 msg
                        "JS_EXEC",               "SEND_NO_MSG_NONE",    // slot 2
                        "JS_EXEC",               "SEND_NO_MSG_NONE",    // slot 3
                        "JS_EXEC",               "SEND_NO_MSG_NONE",    // slot 4
                        "JS_EXEC",               "SEND_NO_MSG_NONE",    // slot 5
                    };

                    bool testOk = Assert(id, GetMarkList(), expectedList);
                    DestroyMarkList(id);

                    LogNL();
                    Log("=== Test ", id, " ", testOk ? "" : "NOT ", "ok ===");
                    LogNL();
                });
                tedTestInner.RegisterForTimedEvent(50);
            });
            tedTestOuter.RegisterForTimedEvent(NextTestDuration());
        }

        // all override, with gps lock
        {
            static TimedEventHandlerDelegate tedTestOuter;
            tedTestOuter.SetCallback([this]{
                static TimedEventHandlerDelegate tedTestInner;

                SetTesting(true);
                int id = IncrAndGetTestId();
                CreateMarkList(id);

                bool haveGpsLock = true;
                SetSlot("slot1", msgDefSet, jsUsesBoth);
                SetSlot("slot2", msgDefSet, jsUsesBoth);
                SetSlot("slot3", msgDefSet, jsUsesBoth);
                SetSlot("slot4", msgDefSet, jsUsesBoth);
                SetSlot("slot5", msgDefSet, jsUsesBoth);
                ConfigureWindowSlotBehavior(haveGpsLock);
                PrepareWindowSchedule(0);

                tedTestInner.SetCallback([this, id]{
                    SetTesting(false);

                    vector<string> expectedList = {
                        "JS_EXEC",               "SEND_CUSTOM_MESSAGE",    // slot 1
                        "JS_EXEC",               "SEND_CUSTOM_MESSAGE",    // slot 2
                        "JS_EXEC",               "SEND_CUSTOM_MESSAGE",    // slot 3
                        "JS_EXEC",               "SEND_CUSTOM_MESSAGE",    // slot 4
                        "JS_EXEC",               "SEND_CUSTOM_MESSAGE",    // slot 5
                        "TX_DISABLE_GPS_ENABLE",
                    };

                    bool testOk = Assert(id, GetMarkList(), expectedList);
                    DestroyMarkList(id);

                    LogNL();
                    Log("=== Test ", id, " ", testOk ? "" : "NOT ", "ok ===");
                    LogNL();
                });
                tedTestInner.RegisterForTimedEvent(50);
            });
            tedTestOuter.RegisterForTimedEvent(NextTestDuration());
        }

        // all override, with no gps lock
        {
            static TimedEventHandlerDelegate tedTestOuter;
            tedTestOuter.SetCallback([this]{
                static TimedEventHandlerDelegate tedTestInner;

                SetTesting(true);
                int id = IncrAndGetTestId();
                CreateMarkList(id);

                bool haveGpsLock = false;
                SetSlot("slot1", msgDefSet, jsUsesBoth);
                SetSlot("slot2", msgDefSet, jsUsesBoth);
                SetSlot("slot3", msgDefSet, jsUsesBoth);
                SetSlot("slot4", msgDefSet, jsUsesBoth);
                SetSlot("slot5", msgDefSet, jsUsesBoth);
                ConfigureWindowSlotBehavior(haveGpsLock);
                PrepareWindowSchedule(0);

                tedTestInner.SetCallback([this, id]{
                    SetTesting(false);

                    vector<string> expectedList = {
                        "JS_NO_EXEC",                                      // slot 1 js
                        "TX_DISABLE_GPS_ENABLE",
                                                    "SEND_NO_MSG_NONE",    // slot 1 msg
                        "JS_NO_EXEC",               "SEND_NO_MSG_NONE",    // slot 2
                        "JS_NO_EXEC",               "SEND_NO_MSG_NONE",    // slot 3
                        "JS_NO_EXEC",               "SEND_NO_MSG_NONE",    // slot 4
                        "JS_NO_EXEC",               "SEND_NO_MSG_NONE",    // slot 5
                    };

                    bool testOk = Assert(id, GetMarkList(), expectedList);
                    DestroyMarkList(id);

                    LogNL();
                    Log("=== Test ", id, " ", testOk ? "" : "NOT ", "ok ===");
                    LogNL();
                });
                tedTestInner.RegisterForTimedEvent(50);
            });
            tedTestOuter.RegisterForTimedEvent(NextTestDuration());
        }


        Log("TestPrepareWindowSchedule Done");


        RestoreFiles();
    }


    /////////////////////////////////////////////////////////////////
    // JavaScript Execution
    /////////////////////////////////////////////////////////////////

    bool RunSlotJavaScript(const string &slotName, bool operateRadio = true)
    {
        bool retVal = true;


        // actually, detect if the radio is on or not, and disable/enable as appropriate.

        // slots will run this command not knowing whether the radio is on or not


        if (operateRadio)
        {
            // disable transmitter
        }

        // change to 48MHz
        // auto jsResult = js_.RunSlotJavaScript(slotName);
        // retVal = jsResult.runOk;
        // change to 6MHz

        if (operateRadio)
        {
            // enable transmitter
        }

        return retVal;
    }

    /////////////////////////////////////////////////////////////////
    // Message Sending
    /////////////////////////////////////////////////////////////////

    void SendRegularType1()
    {
        Mark("SEND_REGULAR_TYPE1");
    }

    void SendBasicTelemetry()
    {
        Mark("SEND_BASIC_TELEMETRY");
    }

    void SendCustomMessage()
    {
        Mark("SEND_CUSTOM_MESSAGE");
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
        for (int i = 0; i < 5; ++i)
        {
            FilesystemLittleFS::Move(string{"slot"} + to_string(i) + ".js", string{"slot"} + to_string(i) + ".js.bak");
            FilesystemLittleFS::Move(string{"slot"} + to_string(i) + ".json", string{"slot"} + to_string(i) + ".json.bak");
        }
    }

    void RestoreFiles()
    {
        for (int i = 0; i < 5; ++i)
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

        Log("[", TimestampFromUs(timeUs), "] ", str);

        if (IsTesting())
        {
            AddToMarkList(str);
        }
    }


    /////////////////////////////////////////////////////////////////
    // Init
    /////////////////////////////////////////////////////////////////

    void SetupShell()
    {
        Shell::AddCommand("test.s", [&](vector<string> argList){
            TestPrepareWindowSchedule();
        }, { .argCount = 0, .help = ""});

        Shell::AddCommand("test.l", [&](vector<string> argList){
            OnGpsLock(0, 0, 0);
        }, { .argCount = 0, .help = ""});

        Shell::AddCommand("test.cfg", [&](vector<string> argList){
            TestConfigureWindowSlotBehavior();
        }, { .argCount = 0, .help = "run test suite for slot behavior"});

        Shell::AddCommand("test.gps", [&](vector<string> argList){
            gpsHasLock_ = (bool)atoi(argList[0].c_str());
        }, { .argCount = 1, .help = "set whether gps has lock or not (1 or 0)"});
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
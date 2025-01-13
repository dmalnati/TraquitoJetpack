#pragma once

#include "CopilotControlJavaScript.h"
#include "CopilotControlMessageDefinition.h"
#include "Evm.h"
#include "Log.h"
#include "Shell.h"
#include "Timeline.h"

#include <functional>
#include <string>
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



    bool inWindowCurrently_ = false;

    // coast handler
    void OnNextWindowNoGpsLock()
    {
        inWindowCurrently_ = true;
        // ...
    }










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
                ConfigureWindowSlotBehavior(true);

                // schedule actions based on when the next 10-min window is
                PrepareWindowSlotBehavior(timeAtWindowStartMs);
            }
            // else
            {
                // trigger coast
                // this is exactly the same as getting a gps lock within a
                // window which has started already
            }
        }
    }


    // called when there is known to be enough time to do an orderly
    // window operation 
    void PrepareWindowSlotBehavior(uint64_t timeAtWindowStartMs)
    {
        uint64_t MINUTES_2_MS = 2 * 60 * 1'000;
        
        // support debug testing
        if (timeAtWindowStartMs == 0)
        {
            MINUTES_2_MS = 0;
        }

        const uint64_t SLOT1_START_TIME_MS = timeAtWindowStartMs;
        const uint64_t SLOT2_START_TIME_MS = SLOT1_START_TIME_MS + MINUTES_2_MS;
        const uint64_t SLOT3_START_TIME_MS = SLOT2_START_TIME_MS + MINUTES_2_MS;
        const uint64_t SLOT4_START_TIME_MS = SLOT3_START_TIME_MS + MINUTES_2_MS;
        const uint64_t SLOT5_START_TIME_MS = SLOT4_START_TIME_MS + MINUTES_2_MS;

        Log("PrepareWindowSlotBehavior at ", TimestampFromMs(timeAtWindowStartMs));

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
                    Mark("SEND_NO_MSG");
                }
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

            t_.Report();
        }, "TX_DISABLE_GPS_ENABLE");
        tedTxDisableGpsEnable_.RegisterForTimedEventAt(timeAtChangeMs);
        Log("Scheduled TX_DISABLE_GPS_ENABLE for ", TimestampFromMs(timeAtChangeMs));









    }








    bool RunSlotJavaScript(const string &slotName, bool operateRadio = true)
    {
        bool retVal = true;

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


private:


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

        if (haveGpsLock == false && jsUsesGpsApi == false && jsUsesMsgApi == false) { runJs = true;  msgSend = msgSendDefault; }
        if (haveGpsLock == false && jsUsesGpsApi == false && jsUsesMsgApi == true)  { runJs = true;  msgSend = "custom";       }
        if (haveGpsLock == false && jsUsesGpsApi == true  && jsUsesMsgApi == false) { runJs = false; msgSend = msgSendDefault; }
        if (haveGpsLock == false && jsUsesGpsApi == true  && jsUsesMsgApi == true)  { runJs = false; msgSend = "none";         }
        if (haveGpsLock == true  && jsUsesGpsApi == false && jsUsesMsgApi == false) { runJs = true;  msgSend = msgSendDefault; }
        if (haveGpsLock == true  && jsUsesGpsApi == false && jsUsesMsgApi == true)  { runJs = true;  msgSend = "custom";       }
        if (haveGpsLock == true  && jsUsesGpsApi == true  && jsUsesMsgApi == false) { runJs = true;  msgSend = msgSendDefault; }
        if (haveGpsLock == true  && jsUsesGpsApi == true  && jsUsesMsgApi == true)  { runJs = true;  msgSend = "custom";       }

        // Actually check if there is a msg def.
        // If there isn't one, then revert behavior to sending the default (if any).
        bool hasMsgDef = CopilotControlMessageDefinition::SlotHasMsgDef(slotName);
        if (hasMsgDef == false)
        {
            msgSend = msgSendDefault == "none" ? "none" : "default";
        }

        // return
        SlotBehavior retVal = {
            .runJs   = runJs,
            .msgSend = msgSend,

            .canSendDefault = haveGpsLock,
            .fnSendDefault  = fnSendDefault,
        };

        return retVal;
    }

    void Mark(const char *str)
    {
        uint64_t timeUs = t_.Event(str);

        Log("[", TimestampFromUs(timeUs), "] ", str);
    }


private:

    void SetupShell()
    {
        // Shell::AddCommand("app.ss.cc.schedule", [&](vector<string> argList){
        Shell::AddCommand("test", [&](vector<string> argList){
            PrepareWindowSlotBehavior(0);
        }, { .argCount = 0, .help = ""});
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
#pragma once

#include <algorithm>
using namespace std;

#include "App.h"

#include "SubsystemGps.h"
#include "SubsystemTx.h"

#include "JSONMsgRouter.h"
#include "TempSensorInternal.h"
#include "USB.h"

#include "RP2040.h"



struct TestConfiguration
{
    bool enabled = false;

    bool watchdogOn = true;
    bool logAsync = true;
    bool evmOnly = false;
    bool sendEncoded = true;
    bool apiMode = false;
};

TestConfiguration testCfg;


// special convenience setting to switch to separately-released build
static const bool API_MODE_BUILD = false;


// 1ms instead of 700ms of high initial current
// lower speed w/ usb couldn't keep up
inline static int Init()
{
    // 12mA baseline
    RP2040::Clock::SetClock48MHzNew();

    if (testCfg.enabled)
    {
        RP2040::Clock::PrintAll();
    }

    // saves 5mA at 125MHz
    // saves 2mA at  48MHz
    RP2040::Peripheral::DisablePeripheralList({
        RP2040::Peripheral::SPI1,
        RP2040::Peripheral::SPI0,
        RP2040::Peripheral::PWM,
        RP2040::Peripheral::PIO1,
        RP2040::Peripheral::PIO0,
        RP2040::Peripheral::I2C1,
    });

    return 1;
}

#include <zephyr/init.h>
SYS_INIT(Init, APPLICATION, 1);


class Application
: public App
{
public:

    Application()
    {
        ReadDeviceTree();

        // override for special build
        testCfg = !API_MODE_BUILD ? testCfg : TestConfiguration{
            .enabled = true,
            .apiMode = true,
        };
    }

    /////////////////////////////////////////////////////////////////
    // Program start - Decide Configuration or Flight Mode
    /////////////////////////////////////////////////////////////////

    void Run()
    {
        Timeline::Global().Event("Application");

        LogNL(2);
        Log("Module Details");
        Log("Software: ", Version::GetVersionShort());
        LogNL();

        // Watchdog setup
        if (testCfg.enabled == false || (testCfg.enabled && testCfg.watchdogOn == true))
        {
            Watchdog::SetTimeout(5'000);
            Watchdog::Start();
            Log("Watchdog enabled");

            tedWatchdog_.SetCallback([]{
                Watchdog::Feed();
            }, "TIMER_WATCHDOG_FEED");
            tedWatchdog_.RegisterForTimedEventIntervalRigid(2'000, 0);
        }

        // set up blinker
        blinker_.SetPin(pinLedGreen_);

        // Initial blink pattern indicates testing of systems and the
        // sufficiency of the power source (ie solar).

        // Blink 1 - CPU can run on this power
        PAL.Delay(1'000);
        blinker_.Blink(1, 500, 100);
        Watchdog::Feed();

        // Blink 2 - GPS can run on this power
        ssGps_.ModulePowerOnBatteryOn();
        // PAL.Delay(500);  // the power on has a delay of 500 in it already
        PAL.Delay(500);  // add another 500 for a total of 1 sec
        blinker_.Blink(1, 500, 100);
        ssGps_.ModulePowerOff();
        Watchdog::Feed();

        // Blink 3 - Transmitter can run on this power
        ssTx_.Enable();
        ssTx_.RadioOn();
        PAL.Delay(1'000);
        blinker_.Blink(1, 500, 100);
        ssTx_.RadioOff();
        ssTx_.Disable();
        Watchdog::Feed();

        Log("Power test blinking sequence complete");

        // Set up system elements common between modes
        SetupShell();
        SetupJSON();

        if (testCfg.enabled && testCfg.logAsync == false)
        {
            Evm::DisableAutoLogAsync();
            Log("Async logging disabled");
        }

        // Set TX watchdog feeders that also keep the blink going
        ssTx_.SetCallbackOnTxStart([this]{
            Watchdog::Feed();
            blinker_.On();
        });
        ssTx_.SetCallbackOnBitChange([this]{
            Watchdog::Feed();
            blinker_.Toggle();
        });
        ssTx_.SetCallbackOnTxEnd([this]{
            Watchdog::Feed();
            BlinkerIdle();
        });

        // Determine mode of operation
        bool configurationMode = false;
        if (testCfg.enabled == false)
        {
            USB::SetCallbackConnected([&]{
                configurationMode = true;
            });
            USB::SetCallbackDisconnected([]{
                LogModeSync();
                Log("USB DISCONNECTED");
                LogModeAsync();
                PAL.Reset();
            });
        }
        USB::Enable();

        Log("Determining startup mode");
        LogNL();
        Evm::MainLoopRunFor(1'000);

        if (testCfg.enabled && testCfg.evmOnly)
        {
            BlinkerIdle();
            LogNL(2);
            Log("Main Loop Only");
            Evm::MainLoop();
        }

        bool cfgModeEnableBlink = true;
        if (testCfg.enabled)
        {
            // normally the testmode being enabled means you want
            // to test sending messages in flight mode.
            // however, the api mode override lets you enter
            // configuration mode instead
            if (testCfg.apiMode == true)
            {
                Log("API Mode Enabled");
                configurationMode = true;
                cfgModeEnableBlink = false;
            }
            else
            {
                configurationMode = false;
            }
        }
        
        if (configurationMode)
        {
            ConfigurationMode(cfgModeEnableBlink);
        }
        else
        {
            FlightMode();
        }

        // now that mode is established, if you subsequently plug back
        // into USB the device should restart to re-evaluate.
        // In API mode no such switch happens, the devices runs without
        // interruption
        if (testCfg.enabled == false ||
           (testCfg.enabled && testCfg.apiMode == false))
        {
            USB::SetCallbackConnected([&]{
                LogModeSync();
                Log("USB Connected");
                LogModeAsync();
                PAL.Reset();
            });
        }

        // Handle events
        Evm::MainLoop();
    }

    /////////////////////////////////////////////////////////////////
    // Configuration Mode
    /////////////////////////////////////////////////////////////////

    void ConfigurationMode(bool enableBlink = true)
    {
        Log("Configuration Mode");

        ssGps_.EnableConfigurationMode();

        // announce the temperature regularly
        static TimedEventHandlerDelegate tedTemp;
        tedTemp.SetCallback([this]{
            JSONMsgRouter::Send([&](const auto &out){
                out["type"] = "TEMP";
                out["tempC"] = tempSensor_.GetTempC();
                out["tempF"] = tempSensor_.GetTempF();
            });
        }, "APP_TEMP_TIMER");
        tedTemp.RegisterForTimedEventIntervalRigid(1000, 0);

        if (enableBlink)
        {
            // Set on async blinking journey
            BlinkerIdle();
        }
    }



    /////////////////////////////////////////////////////////////////
    // Flight Mode
    /////////////////////////////////////////////////////////////////
    
    void FlightMode()
    {
        LogNL();
        LogNL();
        Log("Flight Mode");
        LogNL();

        if (testCfg.enabled)
        {
            // testing - fudge some configuration
            Configuration &txCfg = ssTx_.GetConfiguration();
            txCfg.band = "20m";
            txCfg.callsign = "KD2KDD";
            txCfg.channel = 414;
            txCfg.correction = 0;
            txCfg.Put();
            txCfg.Get();
        }

        // Load flight configuration -- ensure it exists
        if (ssTx_.ReadyToFly() == false)
        {
            Log("ERR: ==== NOT READY TO FLY - FATAL ====");

            // panic blink - let watchdog kill
            while (true)
            {
                BlinkerBlinkOncePanic();
            }
        }
        else
        {
            ssTx_.SetupTransmitterForFlight();

            Configuration &txCfg = ssTx_.GetConfiguration();
            auto cd = WSPR::GetChannelDetails(txCfg.band, txCfg.channel);

            Log("==== Ok to fly! ====");
            Log("Callsign  : ", txCfg.callsign);
            Log("Band      : ", txCfg.band);
            Log("Channel   : ", txCfg.channel);
            Log("ID13      : ", cd.id13);
            Log("Min       : ", cd.min);
            Log("Lane      : ", cd.lane);
            Log("Freq      : ", Commas(cd.freq));
            Log("Correction: ", txCfg.correction);
            LogNL();

            // Signal ok
            blinker_.Blink(4, 100, 100);

            // give visual space to distinguish these "ok" blinks from
            // upcoming status blinks
            Watchdog::Feed();
            PAL.Delay(1'500);
        }

        // Go to initial state
        OnNoTimeNoFix();
    }

    /////////////////////////////////////////////////////////////////
    // Flight Mode Async Loop
    /////////////////////////////////////////////////////////////////

    void OnNoTimeNoFix()
    {
        t_.Reset();
        t_.SetMaxEvents(50);
        t_.Event("NO_TIME_NO_FIX");

        // ask gps to get a fix
        if (testCfg.enabled == false)
        {
            ssGps_.DisableVerboseLogging();
        }
        ssGps_.EnableFlightMode();
        t_.Event("GpsEnabled");
        Log("Requesting Fix 3DPlus");
        ssGps_.RequestNewFix3DPlus([this](const Fix3DPlus &fix){
            t_.Event("Fix3DPlus");

            // cancel timer
            CancelGpsLockOrDieTimer();

            // capture fix
            fix_ = fix;

            // shut off gps
            ssGps_.Disable();

            // go to next state
            OnYesTimeYesFix_Early();
        });
        t_.Event("FixRequested");
        StartGpsLockOrDieTimer();

        BlinkerGpsSearch();
    }

    void OnYesTimeYesFix_Early()
    {
        t_.Event("YES_TIME_YES_FIX__EARLY");

        // figure out when to wake up to send this fix
        const Configuration &txCfg = ssTx_.GetConfiguration();
        WSPR::ChannelDetails cd = WSPR::GetChannelDetails(txCfg.band, txCfg.channel);

        uint8_t fixMinute = fix_.minute % 10;

        if (testCfg.enabled)
        {
            // pick the next 2 min slot
            cd.min = (fixMinute + (fixMinute % 2 ? 1 : 2)) % 10;
        }

        // determine minutes duration before target minute
        uint8_t minBefore;
        if (fixMinute < cd.min)
        {
            minBefore = cd.min - fixMinute;
        }
        else if (fixMinute == cd.min)
        {
            // only if we're literally on exactly the moment, then
            // you can say zero time remaining.
            // otherwise, the logic below will subtract time (from zero)
            // and end up with a very long delay (which crashes the system)
            if (fix_.second == 0 && fix_.millisecond == 0)
            {
                minBefore = 0;
            }
            else
            {
                minBefore = 10;
            }
        }
        else // fixMinute > cd.min
        {
            // move forward to the next 10 minute cycle
            // eg this is minute 8, I target minute 6
            // 8 - 6 = 2
            // 10 - 2 = 8; therefore I wait 8 minutes
            // 8 minutes after the minute 8 is the 6th minute, that's my target
            minBefore = (10 - (fixMinute - cd.min));
        }

        // calculate ms difference
        uint32_t delayTransmitMs = minBefore * 60 * 1'000;

        // give credit for how far into this minute we are already
        delayTransmitMs -= (fix_.second * 1'000);
        delayTransmitMs -= fix_.millisecond;

        // and actually supposed to be at :01, so add a little fudge
        // factor in there, tuned emperically.
        // (we're late in our timing anyway so don't overdo it)
        static const uint32_t FUDGE_MS = 410;
        delayTransmitMs += FUDGE_MS;

        // to avoid TX drift, we want to turn the radio on a while before sending
        static const uint32_t RADIO_ON_EARLY_TARGET_MS = 30'000;
        uint32_t delayRadioOnMs;
        if (delayTransmitMs >= RADIO_ON_EARLY_TARGET_MS)
        {
            delayRadioOnMs = delayTransmitMs - RADIO_ON_EARLY_TARGET_MS;
        }
        else if (delayTransmitMs < RADIO_ON_EARLY_TARGET_MS)
        {
            delayRadioOnMs = 0;
        }

        uint64_t timeNow = PAL.Millis();

        // set timer for radio on
        tedRadioOn_.SetCallback([this]{
            // bring transmitter online
            t_.Event("Transmitter Starting");

            Log("Radio warmup starting");
            Log("RTC Now: ", MsToMinutesStr(PAL.Millis()));

            ssTx_.Enable();
            ssTx_.RadioOn();
            ssTx_.SetupTransmitterForFlight();

            BlinkerTransmit();

            t_.Event("Transmitter Online");
        }, "TIMER_APP_RADIO_ON");
        tedRadioOn_.RegisterForTimedEvent(delayRadioOnMs);

        // set timer for TX
        tedSend_.SetCallback([this]{
            OnYesTimeYesFix_TimeToSend();
        }, "TIMER_APP_TX");
        tedSend_.RegisterForTimedEvent(delayTransmitMs);


        // do some logging to explain the schedule
        uint32_t fixTimeAsMs     = (fix_.minute * 60 * 1'000) + (fix_.second * 1'000) + fix_.millisecond;
        uint32_t radioOnTimeAsMs = fixTimeAsMs + delayRadioOnMs;
        uint32_t txTimeAsMs      = fixTimeAsMs + delayTransmitMs;

        string timeGpsNow                = MsToMinutesStr(fixTimeAsMs);
        string timeRadioOn               = MsToMinutesStr(radioOnTimeAsMs);
        string timeRadioOnDuration       = MsToMinutesStr(delayRadioOnMs);
        string durationRadioOnEarly      = MsToMinutesStr(delayTransmitMs - delayRadioOnMs);
        string durationRadioDelayWanted  = MsToMinutesStr(RADIO_ON_EARLY_TARGET_MS);
        string timeTx                    = MsToMinutesStr(txTimeAsMs);
        string timeTxDuration            = MsToMinutesStr(delayTransmitMs);

        LogNL();
        Log("Scheduling TX to GPS time");
        LogNL();
        Log("GPS Now  : ", timeGpsNow);
        Log("  Radio  : ", timeRadioOn, " (", timeRadioOnDuration, " from now, ", durationRadioOnEarly, " early, wanted ", durationRadioDelayWanted, ")");
        Log("  TX     : ", timeTx,      " (", timeTxDuration,      " from now)");
        Log("  Target : _", cd.min, ":00." + to_string(FUDGE_MS));
        LogNL();
        Log("RTC Now      : ", MsToMinutesStr(timeNow));
        Log("  Radio on at: ", MsToMinutesStr(timeNow + delayRadioOnMs));
        Log("  TX    on at: ", MsToMinutesStr(timeNow + delayTransmitMs));
        LogNL();

        t_.Event("Scheduled");

        BlinkerIdle();
    }

    void OnYesTimeYesFix_TimeToSend()
    {
        t_.Event("YES_TIME_YES_FIX__TIME_TO_SEND");

        Log("Regular message sending start");
        Log("RTC Now: ", MsToMinutesStr(PAL.Millis()));

        // retrieve flight configuration
        const Configuration &txCfg = ssTx_.GetConfiguration();

        // take note of the time the sending of the regular message begins
        uint64_t timeSendRegular = PAL.Millis();

        // send regular message
        static const uint8_t POWER_DBM = 13;
        ssTx_.SendRegularMessage(txCfg.callsign, fix_.maidenheadGrid.substr(0, 4), POWER_DBM);
        LogNL();

        t_.Event("Regular Message Sent");

        if (testCfg.enabled && testCfg.sendEncoded == false)
        {
            Timeline::Global().Report();
            BlinkerIdle();

            LogNL();
            Log("Not sending encoded message");
            LogNL();

            OnYesTimeNoFix();
            return;
        }

        // purposefully do not turn the radio off, the longer it
        // runs the more stable the frequency (less drift)

        // delay to send the U4B message
        uint64_t timeSinceRegular = PAL.Millis() - timeSendRegular;
        const uint32_t EXPECTED_DELAY_START_REGULAR_TO_START_U4B = 2 * 60 * 1'000;    // 2 min

        uint32_t delayToU4bMs = 0;
        if (timeSinceRegular < EXPECTED_DELAY_START_REGULAR_TO_START_U4B)
        {
            delayToU4bMs = EXPECTED_DELAY_START_REGULAR_TO_START_U4B - timeSinceRegular;
        }

        // feed watchdog while waiting
        while (delayToU4bMs)
        {
            Watchdog::Feed();

            uint32_t delayMs = min(WSPR_BIT_DURATION_MS, delayToU4bMs);
            blinker_.Toggle();
            PAL.DelayBusy(delayMs);

            delayToU4bMs -= delayMs;
        }
        Watchdog::Feed();

        t_.Event("Waited for U4B Send Time");

        Log("Encoded message sending start");
        Log("RTC Now: ", MsToMinutesStr(PAL.Millis()));

        // get data needed to fill out encoded message
        WSPR::ChannelDetails cd = WSPR::GetChannelDetails(txCfg.band, txCfg.channel);

        string   grid56    = fix_.maidenheadGrid.substr(4, 2);
        uint32_t altM      = fix_.altitudeM < 0 ? 0 : fix_.altitudeM;
        int8_t   tempC     = tempSensor_.GetTempC();
        double   voltage   = PAL.MeasureVCC();  // capture under max load
        bool     gpsValid  = true;

        ssTx_.SendEncodedMessage(
            cd.id13,
            grid56,
            altM,
            tempC,
            voltage,
            fix_.speedKnots,
            gpsValid
        );
        LogNL();

        t_.Event("U4B Message Sent");

        // bring transmitter offline
        ssTx_.RadioOff();
        ssTx_.Disable();

        t_.Event("Transmitter Offline");
        t_.ReportNow();

        BlinkerIdle();

        // get the next fix
        OnYesTimeNoFix();
    }

    void OnYesTimeNoFix()
    {
        // need a fix, get one

        // this is equivalent
        OnNoTimeNoFix();
    }


    /////////////////////////////////////////////////////////////////
    // GPS Lock Or Die timer
    /////////////////////////////////////////////////////////////////
    
    void StartGpsLockOrDieTimer()
    {
        static const uint32_t TWENTY_MINUTES = 20 * 60 * 1'000;

        tedGpsLockOrDie_.SetCallback([this]{
            LogModeSync();

            LogNL();
            Log("No GPS Lock within ", MsToMinutesStr(TWENTY_MINUTES));

            // hard reset GPS
            Log("Hard Resetting GPS");
            ssGps_.ModuleHardReset();

            // reboot via watchdog kill
            Log("Rebooting via Watchdog death");
            while (true)
            {
                BlinkerBlinkOncePanic();
            }
        }, "TIMER_GPS_LOCK_OR_DIE");
        tedGpsLockOrDie_.RegisterForTimedEvent(TWENTY_MINUTES);
    }

    void CancelGpsLockOrDieTimer()
    {
        tedGpsLockOrDie_.DeRegisterForTimedEvent();
    }


    /////////////////////////////////////////////////////////////////
    // Flight Mode - Blinker states
    /////////////////////////////////////////////////////////////////

    inline static const uint32_t WSPR_BIT_DURATION_MS = 683;

    // once every 5 seconds
    void BlinkerIdle()
    {
        blinker_.SetBlinkOnOffTime(75, 4925);
        blinker_.EnableAsyncBlink();
    }

    // once every 1 seconds
    void BlinkerGpsSearch()
    {
        blinker_.SetBlinkOnOffTime(75, 925);
        blinker_.EnableAsyncBlink();
    }

    // alternating 683ms periods
    // also operated by hand, but also run during radio warmup
    void BlinkerTransmit()
    {
        blinker_.SetBlinkOnOffTime(WSPR_BIT_DURATION_MS, WSPR_BIT_DURATION_MS);
        blinker_.EnableAsyncBlink();
    }

    void BlinkerBlinkOncePanic()
    {
        blinker_.Blink(1, 40, 40);
    }


private:

    /////////////////////////////////////////////////////////////////
    // Utility
    /////////////////////////////////////////////////////////////////

    // Indicate signs of life during runtime for a period of time
    //
    // Enhancement - Stop this from being high during transmit or GPS
    // to conserve power
    void SetupSignsOfLife(uint32_t durationMsIn)
    {
        static const uint32_t PERIOD_MS = 5000;
        static const uint32_t ON_MS     =   75;
        static const uint32_t OFF_MS    = PERIOD_MS - ON_MS;

        static uint32_t durationMs;
        static uint32_t countRemaining;

        durationMs = durationMsIn;
        countRemaining = durationMs / PERIOD_MS;

        static TimedEventHandlerDelegate tedLed;
        tedLed.SetCallback([this](){
            static uint32_t durationMsNext = OFF_MS;
            durationMsNext = (durationMsNext == ON_MS ? OFF_MS : ON_MS);

            if (countRemaining)
            {
                pinLedGreen_.DigitalToggle();

                tedLed.RegisterForTimedEvent(durationMsNext);
            }
            else
            {
                pinLedGreen_.DigitalWrite(0);
            }

            --countRemaining;
        }, "TIMER_APP_SIGNS_OF_LIFE");

        tedLed.RegisterForTimedEvent(0);
    }


    /////////////////////////////////////////////////////////////////
    // Startup
    /////////////////////////////////////////////////////////////////

    void SetupShell()
    {
        Shell::AddCommand("app.temp", [this](vector<string> argList){
            Log("TempC: ", tempSensor_.GetTempC());
            Log("TempF: ", tempSensor_.GetTempF());
        }, { .argCount = 0, .help = "app get temp"});

        Shell::AddCommand("app.test.led.green.on", [this](vector<string> argList){
            pinLedGreen_.DigitalWrite(1);
        }, { .argCount = 0, .help = ""});

        Shell::AddCommand("app.test.led.green.off", [this](vector<string> argList){
            pinLedGreen_.DigitalWrite(0);
        }, { .argCount = 0, .help = ""});
    }

    void SetupJSON()
    {
        JSONMsgRouter::RegisterHandler("REQ_GET_DEVICE_INFO", [this](auto &in, auto &out){
            out["type"] = "REP_GET_DEVICE_INFO";

            out["swVersion"] = Version::GetVersion();
            if (testCfg.enabled && testCfg.apiMode)
            {
                out["mode"] = "API";
            }
            else
            {
                out["mode"] = "TRACKER";
            }
        });
    }

    void ReadDeviceTree()
    {
        pinLedGreen_ = { DT_GET(pin_led_green) };
    }


private:

    Pin pinLedGreen_;

    SubsystemGps ssGps_;
    SubsystemTx ssTx_;

    Fix3DPlus fix_;

    TimedEventHandlerDelegate tedRadioOn_;
    TimedEventHandlerDelegate tedSend_;
    TimedEventHandlerDelegate tedWatchdog_;
    TimedEventHandlerDelegate tedGpsLockOrDie_;

    Blinker blinker_;

    Timeline t_;

    TempSensorInternal tempSensor_;
};




#pragma once

#include <algorithm>
using namespace std;

#include "ADCInternal.h"
#include "Blinker.h"
#include "JSONMsgRouter.h"
#include "SubsystemCopilotControl.h"
#include "SubsystemGps.h"
#include "SubsystemTx.h"
#include "Time.h"
#include "TempSensorInternal.h"
#include "USB.h"


struct TestConfiguration
{
    bool enabled = true;

    bool fastStartEvmOnly = false;

    bool watchdogOn = true;
    bool logAsync = true;
    bool evmOnly = false;
    bool sendEncoded = true;
    bool apiMode = false;
};

TestConfiguration testCfg;


// special convenience setting to switch to separately-released build
static const bool API_MODE_BUILD = false;

class Application
{
public:

    Application()
    {
        if (testCfg.enabled && testCfg.fastStartEvmOnly)
        {
            // do nothing
        }
        else
        {
            PowerSave();
        }

        // override for special build
        testCfg = !API_MODE_BUILD ? testCfg : TestConfiguration{
            .enabled = true,
            .apiMode = true,
        };

        // we use both the second I2C instance also
        I2C::Init1();
        I2C::SetupShell1();

        USB::SetStringManufacturer("Traquito");
        USB::SetStringProduct("Jetpack");
        USB::SetStringCdcInterface("Traquito Jetpack");
        USB::SetStringVendorInterface("Traquito Jetpack");
        USB::SetVid(0x2FE3);
        USB::SetPid(0x0008);
    }

    /////////////////////////////////////////////////////////////////
    // Program start - Decide Configuration or Flight Mode
    /////////////////////////////////////////////////////////////////

    void Run()
    {
        if (testCfg.enabled && testCfg.fastStartEvmOnly)
        {
            SetupShell();
            SetupJSON();

            Log("Main Loop Only");
            return;
        }

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
            LogNL();

            timerWatchdog_.SetName("TIMER_WATCHDOG_FEED");
            timerWatchdog_.SetCallback([]{
                Watchdog::Feed();
            });
            timerWatchdog_.TimeoutIntervalMs(2'000, 0);
        }

        // set up blinker
        blinker_.SetPin(pinLedGreen_);

        // Startup blinks indicate progressively higher power demand
        PowerTest();

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
        configurationMode_ = false;
        if (testCfg.enabled == false)
        {
            USB::SetCallbackConnected([&]{
                Log("USB CONNECTED");
                configurationMode_ = true;
            });
            USB::SetCallbackDisconnected([]{
                LogModeSync();
                Log("USB DISCONNECTED");
                LogModeAsync();
                PAL.Reset();
            });
        }

        LogNL();
        Log("Determining startup mode");
        LogNL();
        // wait for USB events to fire
        timerStartupRole_.SetName("TIMER_STARTUP_ROLE");
        timerStartupRole_.SetCallback([this]{
            EnableMode();
        });
        timerStartupRole_.TimeoutInMs(1'000);
    }

    void EnableMode()
    {
        if (testCfg.enabled && testCfg.evmOnly)
        {
            BlinkerIdle();
            LogNL(2);
            Log("Main Loop Only");
            return;
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
                configurationMode_ = true;
                cfgModeEnableBlink = false;
            }
            else
            {
                configurationMode_ = false;
            }
        }
        
        if (configurationMode_)
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
    }

    /////////////////////////////////////////////////////////////////
    // Configuration Mode
    /////////////////////////////////////////////////////////////////

    void ConfigurationMode(bool enableBlink = true)
    {
        Log("Configuration Mode");

        ssGps_.EnableConfigurationMode();

        // announce the temperature regularly
        static Timer timerTemp("APP_TEMP_TIMER");
        timerTemp.SetCallback([this]{
            router_.Send([&](const auto &out){
                out["type"] = "TEMP";
                out["tempC"] = tempSensor_.GetTempC();
                out["tempF"] = tempSensor_.GetTempF();
            });
        });
        timerTemp.TimeoutIntervalMs(1000, 0);

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
            txCfg.callsign = "KD3KDD";
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
            auto cd = WsprChannelMap::GetChannelDetails(txCfg.band.c_str(), txCfg.channel);

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
            Watchdog::Feed();

            // Set up copilot control scheduler
            SetupScheduler();
        }
    }

    void SetupScheduler()
    {
        SetupSchedulerGps();
        SetupSchedulerMessageSending();
        SetupSchedulerRadio();
        SetupSchedulerClockSpeed();
        SetupSchedulerWsprMinute();

        ssCc_.GetScheduler().Start();
    }

    void SetupSchedulerGps()
    {
        auto &scheduler = ssCc_.GetScheduler();

        Shell::AddCommand("now", [this, &scheduler](vector<string> argList){
            // 2025-01-02 19:42:02.025000
            fix_ = GPSReader::GetFix3DPlusExample();

            // our debug channel = 414, so minute 6.
            // change the time to start more quickly.
            fix_.minute = 5;
            fix_.second = 55;
            fix_.dateTime = GPSReader::MakeDateTimeFromFixTime(fix_);

            scheduler.OnGps3DPlusLock(fix_);
        }, { .argCount = 0, .help = "trigger 3d lock"});

        scheduler.SetCallbackRequestNewGpsLock([this, &scheduler]{
            BlinkerGpsSearch();


            // disable for now
            return;

            t_.Reset();
            t_.SetMaxEvents(50);
            t_.Event("GPS_REQUESTED");

            // ask gps to get a fix
            if (testCfg.enabled == false)
            {
                ssGps_.DisableVerboseLogging();
            }
            ssGps_.EnableFlightMode();
            t_.Event("GpsEnabled");

            Log("Requesting Fix Time");
            // todo
            // this is good because it lets us send admin messages when no 3dfix


            Log("Requesting Fix 3DPlus");
            ssGps_.RequestNewFix3DPlus([this, &scheduler](const Fix3DPlus &fix){
                t_.Event("Fix3DPlus");

                // cancel timer
                CancelGpsLockOrDieTimer();

                // capture fix
                fix_ = fix;

                // tell scheduler
                scheduler.OnGps3DPlusLock(fix_);
            });
            t_.Event("FixRequested");
            StartGpsLockOrDieTimer();
        });

        scheduler.SetCallbackCancelRequestNewGpsLock([this]{
            BlinkerIdle();

            // disable for now
            return;

            // shut off gps
            ssGps_.Disable();


            // todo -- time events can keep the scheduler happy and willing to
            // cancel the gps request. is that good?
            //
            // would I want the tracker to die if it can't get a 3d fix but
            // can get a time fix?
            //
            // I think it's certainly a thing to consider.
            // probably don't let x windows go by without a 3d fix.

            CancelGpsLockOrDieTimer();
        });
    }

    void SetupSchedulerMessageSending()
    {
        auto &scheduler = ssCc_.GetScheduler();

        scheduler.SetCallbackSendRegularType1([this](){
            const Configuration &txCfg = ssTx_.GetConfiguration();
            static const uint8_t POWER_DBM = 13;

            Log("Sending regular start");
            ssTx_.SendRegularMessage(txCfg.callsign, fix_.maidenheadGrid.substr(0, 4), POWER_DBM);
            Log("Sending regular done");
        });

        scheduler.SetCallbackSendBasicTelemetry([this](){
            // get data needed to fill out encoded message
            const Configuration &txCfg = ssTx_.GetConfiguration();
            WsprChannelMap::ChannelDetails cd = WsprChannelMap::GetChannelDetails(txCfg.band.c_str(), txCfg.channel);

            string   grid56    = fix_.maidenheadGrid.substr(4, 2);
            uint32_t altM      = fix_.altitudeM < 0 ? 0 : fix_.altitudeM;
            int8_t   tempC     = tempSensor_.GetTempC();
            double   voltage   = (double)ADC::GetMilliVoltsVCC() / 1'000;  // capture under max load
            bool     gpsValid  = true;

            ssTx_.SendTelemetryBasic(
                cd.id13,
                grid56,
                altM,
                tempC,
                voltage,
                fix_.speedKnots,
                gpsValid
            );
        });

        scheduler.SetCallbackSendUserDefined([this](uint8_t slot, MsgUD &msg, uint64_t quitAfterMs){
            const Configuration &txCfg = ssTx_.GetConfiguration();
            WsprChannelMap::ChannelDetails cd = WsprChannelMap::GetChannelDetails(txCfg.band.c_str(), txCfg.channel);

            msg.SetId13(cd.id13);
            msg.SetHdrSlot(slot - 1);
            msg.Encode();

            Log("Sending User-Defined Message in slot", slot, " (limit ", Commas(quitAfterMs)," ms): ", msg.GetCallsign(), " ", msg.GetGrid4(), " ", msg.GetPowerDbm());
            Log(CopilotControlUtl::GetMsgStateAsString(msg));
            ssTx_.SetTxQuitAfterMs(quitAfterMs);
            ssTx_.SendMessage(msg);
            ssTx_.SetTxQuitAfterMs(0);
            Log("Sent");
        });
    }

    void SetupSchedulerRadio()
    {
        auto &scheduler = ssCc_.GetScheduler();

        scheduler.SetCallbackRadioIsActive([this]{
            return ssTx_.IsOn();
        });

        scheduler.SetCallbackStartRadioWarmup([this]{
            ssTx_.Enable();
            ssTx_.RadioOn();
            ssTx_.SetupTransmitterForFlight();

            BlinkerTransmit();
        });

        scheduler.SetCallbackStopRadio([this]{
            ssTx_.RadioOff();
            ssTx_.Disable();
        });
    }

    void SetupSchedulerClockSpeed()
    {
        // Ignoring LED blinks, GPS, TX, etc, the following are the
        // current consumption measurements by clock speed:
        // Low-Jitter (not power-optimized):
        //  6 MHz: ~  4.7 mA
        // 12 MHz: ~  5.5 mA
        // 48 MHz: ~ 13.0 mA
        //
        // More-Jitter (power-optimized)
        //  6 MHz: ~ (not possible)
        // 12 MHz: ~ (not possible)
        // 48 MHz: ~ (exactly same)
        //
        // LED: ~ 3 mA
        //
        // Time to switch to a pre-cached clock speed:
        //             |  6 MHz | 12 MHz | 48 MHz
        // --------------------------------------
        // From  6 MHz |  32 ms |  36 ms |  40 ms
        // From 12 MHz |  40 ms |  26 ms |  25 ms
        // From 48 MHz |  32 ms |  19 ms |  17 ms

        auto &scheduler = ssCc_.GetScheduler();

        scheduler.SetCallbackGoHighSpeed([this]{
            Clock::SetClockMHz(48);
        });

        scheduler.SetCallbackGoLowSpeed([this]{
            Clock::SetClockMHz(6);
        });
    }

    void SetupSchedulerWsprMinute()
    {
        auto &scheduler = ssCc_.GetScheduler();

        Configuration &txCfg = ssTx_.GetConfiguration();
        auto cd = WsprChannelMap::GetChannelDetails(txCfg.band.c_str(), txCfg.channel);
        scheduler.SetStartMinute(cd.min);
    }

    /////////////////////////////////////////////////////////////////
    // Default Flight Mode Schedule
    /////////////////////////////////////////////////////////////////

    void GetNextGPSLock()
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
            ScheduleWarmupAndSend();
        });
        t_.Event("FixRequested");
        StartGpsLockOrDieTimer();

        BlinkerGpsSearch();
    }

    void ScheduleWarmupAndSend()
    {
        t_.Event("YES_TIME_YES_FIX__EARLY");

        // figure out when to wake up to send this fix
        const Configuration &txCfg = ssTx_.GetConfiguration();
        WsprChannelMap::ChannelDetails cd = WsprChannelMap::GetChannelDetails(txCfg.band.c_str(), txCfg.channel);

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
        static const int32_t FUDGE_MS = -40;
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
        timerRadioOn_.SetName("TIMER_APP_RADIO_ON");
        timerRadioOn_.SetCallback([this]{
            // bring transmitter online
            t_.Event("Transmitter Starting");

            Log("Radio warmup starting");
            Log("RTC Now: ", Time::MakeTimeMMSSmmmFromUs(PAL.Millis()));

            ssTx_.Enable();
            ssTx_.RadioOn();
            ssTx_.SetupTransmitterForFlight();

            BlinkerTransmit();

            t_.Event("Transmitter Online");
        });
        timerRadioOn_.TimeoutInMs(delayRadioOnMs);

        // set timer for TX
        timerSend_.SetName("TIMER_APP_TX");
        timerSend_.SetCallback([this]{
            Send();
        });
        timerSend_.TimeoutInMs(delayTransmitMs);


        // do some logging to explain the schedule
        uint32_t fixTimeAsMs     = (fix_.minute * 60 * 1'000) + (fix_.second * 1'000) + fix_.millisecond;
        uint32_t radioOnTimeAsMs = fixTimeAsMs + delayRadioOnMs;
        uint32_t txTimeAsMs      = fixTimeAsMs + delayTransmitMs;

        string timeGpsNow                = Time::MakeTimeMMSSmmmFromUs(fixTimeAsMs);
        string timeRadioOn               = Time::MakeTimeMMSSmmmFromUs(radioOnTimeAsMs);
        string timeRadioOnDuration       = Time::MakeTimeMMSSmmmFromUs(delayRadioOnMs);
        string durationRadioOnEarly      = Time::MakeTimeMMSSmmmFromUs(delayTransmitMs - delayRadioOnMs);
        string durationRadioDelayWanted  = Time::MakeTimeMMSSmmmFromUs(RADIO_ON_EARLY_TARGET_MS);
        string timeTx                    = Time::MakeTimeMMSSmmmFromUs(txTimeAsMs);
        string timeTxDuration            = Time::MakeTimeMMSSmmmFromUs(delayTransmitMs);

        LogNL();
        Log("Scheduling TX to GPS time");
        LogNL();
        Log("GPS Now  : ", timeGpsNow);
        Log("  Radio  : ", timeRadioOn, " (", timeRadioOnDuration, " from now, ", durationRadioOnEarly, " early, wanted ", durationRadioDelayWanted, ")");
        Log("  TX     : ", timeTx,      " (", timeTxDuration,      " from now)");
        Log("  Target : _", cd.min, ":00.000");
        LogNL();
        Log("RTC Now      : ", Time::MakeTimeMMSSmmmFromUs(timeNow));
        Log("  Radio on at: ", Time::MakeTimeMMSSmmmFromUs(timeNow + delayRadioOnMs));
        Log("  TX    on at: ", Time::MakeTimeMMSSmmmFromUs(timeNow + delayTransmitMs));
        LogNL();

        t_.Event("Scheduled");

        BlinkerIdle();
    }

    void Send()
    {
        t_.Event("YES_TIME_YES_FIX__TIME_TO_SEND");

        Log("Regular message sending start");
        Log("RTC Now: ", Time::MakeTimeMMSSmmmFromUs(PAL.Millis()));

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

            GetNextGPSLock();
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

        // emperically determined
        static int32_t FUDGE_MS_U4B = -75;
        delayToU4bMs += FUDGE_MS_U4B;

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
        Log("RTC Now: ", Time::MakeTimeMMSSmmmFromUs(PAL.Millis()));

        // get data needed to fill out encoded message
        WsprChannelMap::ChannelDetails cd = WsprChannelMap::GetChannelDetails(txCfg.band.c_str(), txCfg.channel);

        string   grid56    = fix_.maidenheadGrid.substr(4, 2);
        uint32_t altM      = fix_.altitudeM < 0 ? 0 : fix_.altitudeM;
        int8_t   tempC     = tempSensor_.GetTempC();
        double   voltage   = (double)ADC::GetMilliVoltsVCC() / 1'000;  // capture under max load
        bool     gpsValid  = true;

        ssTx_.SendTelemetryBasic(
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
        GetNextGPSLock();
    }


    /////////////////////////////////////////////////////////////////
    // GPS Lock Or Die timer
    /////////////////////////////////////////////////////////////////
    
    void StartGpsLockOrDieTimer()
    {
        static const uint32_t TWENTY_MINUTES = 20 * 60 * 1'000;

        timerGpsLockOrDie_.SetName("TIMER_GPS_LOCK_OR_DIE");
        timerGpsLockOrDie_.SetCallback([this]{
            LogModeSync();

            LogNL();
            Log("No GPS Lock within ", Time::MakeTimeMMSSmmmFromUs(TWENTY_MINUTES));

            // hard reset GPS
            Log("Hard Resetting GPS");
            ssGps_.ModuleHardReset();

            // reboot via watchdog kill
            Log("Rebooting via Watchdog death");
            while (true)
            {
                BlinkerBlinkOncePanic();
            }
        });
        timerGpsLockOrDie_.TimeoutInMs(TWENTY_MINUTES);
    }

    void CancelGpsLockOrDieTimer()
    {
        timerGpsLockOrDie_.Cancel();
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
    // Power
    /////////////////////////////////////////////////////////////////

    void PowerSave()
    {
        Log("Power saving processing");

        // prepare to swtich to 48MHz in the event that USB is connected
        // later.  willing to take a moment longer on high power to
        // speed up calculation and make device startup not annoyingly
        // longer. ~70ms to accomplish.
        Log("Prepare 48MHz clock speed");
        Clock::PrepareClockMHz(48);

        // 5mA baseline
        // takes ~10ms to accomplish
        Log("Drop to 6MHz clock speed");
        Clock::SetClockMHz(6);
        LogNL();

        if (testCfg.enabled)
        {
            Clock::PrintAll();
            LogNL();
        }

        // saves 5mA at 125MHz
        // saves 2mA at  48MHz
        Log("Disable unused peripherals");
        PeripheralControl::DisablePeripheralList({
            PeripheralControl::SPI1,
            PeripheralControl::SPI0,
            PeripheralControl::PWM,
            PeripheralControl::PIO1,
            PeripheralControl::PIO0,
        });

        // Set up USB to be off unless VBUS detected, but we'll get notified
        // before USB re-enabled so we can get up to speed to handle the bus.
        // emperically I see 45MHz is the minimum required but let's do 48MHz
        // just because.        
        USB::SetCallbackVbusConnected([]{
            Log("App VBUS HIGH handler, switching to 48MHz");
            Clock::SetClockMHz(48);
        });
        USB::SetCallbackVbusDisconnected([]{
            Log("App VBUS LOW handler, switching to 6MHz");
            Clock::SetClockMHz(6);
        });
        USB::EnablePowerSaveMode();
        LogNL();
    }

    void PowerTest()
    {
        // Initial blink pattern indicates testing of systems and the
        // sufficiency of the power source (ie solar).
        Log("Startup Power Test");

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
    }


    /////////////////////////////////////////////////////////////////
    // Startup
    /////////////////////////////////////////////////////////////////

    void SetupShell()
    {
        Shell::AddCommand("app.test.led.green.on", [this](vector<string> argList){
            pinLedGreen_.DigitalWrite(1);
        }, { .argCount = 0, .help = ""});

        Shell::AddCommand("app.test.led.green.off", [this](vector<string> argList){
            pinLedGreen_.DigitalWrite(0);
        }, { .argCount = 0, .help = ""});


        static uint32_t count = 0;
        static bool show = false;
        UartAddLineStreamCallback(UART::UART_1, [](const string &line){
            UartTarget target(UART::UART_0);
            if (show)
            {
                Log(line);
            }
            ++count;
        });

        Shell::AddCommand("app.count", [this](vector<string> argList){
            Log(count);
        }, { .argCount = 0, .help = ""});

        Shell::AddCommand("app.show", [this](vector<string> argList){
            show = !show;
        }, { .argCount = 0, .help = ""});
    }

    void SetupJSON()
    {
        UartAddLineStreamCallback(UART::UART_USB, [this](const string &line){
            router_.Route(line);
        });

        router_.SetOnReceiveCallback([this](const string &jsonStr){
            UartTarget target(UART::UART_USB);
            Log(jsonStr);
        });

        JSONMsgRouter::RegisterHandler("REQ_GET_DEVICE_INFO", [this](auto &in, auto &out){
            out["type"] = "REP_GET_DEVICE_INFO";

            out["swVersion"] = Split(Version::GetVersion())[0];
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

private:

    bool configurationMode_ = false;

    Pin pinLedGreen_ = { 25 };

    SubsystemCopilotControl ssCc_;
    SubsystemGps ssGps_;
    SubsystemTx ssTx_;

    Fix3DPlus fix_;

    JSONMsgRouter::Iface router_;

    Timer timerStartupRole_;
    Timer timerRadioOn_;
    Timer timerSend_;
    Timer timerWatchdog_;
    Timer timerGpsLockOrDie_;

    Blinker blinker_;

    Timeline t_;

    TempSensorInternal tempSensor_;
};




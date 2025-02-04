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
    bool enabled = false;

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


    /////////////////////////////////////////////////////////////////
    // Scheduler Integration
    /////////////////////////////////////////////////////////////////
    
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
            fix3dPlus_ = GPSReader::GetFix3DPlusExample();

            // our debug channel = 414, so minute 6.
            // change the time to start more quickly.
            fix3dPlus_.timeAtPpsUs = PAL.Micros();
            fix3dPlus_.minute = 5;
            fix3dPlus_.second = 55;
            fix3dPlus_.dateTime = GPSReader::MakeDateTimeFromFixTime(fix3dPlus_);

            scheduler.OnGps3DPlusLock(fix3dPlus_);
        }, { .argCount = 0, .help = "trigger 3d lock"});

        scheduler.SetCallbackRequestNewGpsLock([this, &scheduler]{
            BlinkerGpsSearch();

            t_.Reset();
            t_.SetMaxEvents(50);
            t_.Event("GPS_REQUESTED");

            // Enable GPS in preparation for new request
            if (testCfg.enabled == false)
            {
                ssGps_.DisableVerboseLogging();
            }
            ssGps_.EnableFlightMode();
            t_.Event("GpsEnabled");

            // Request new fix
            Log("Requesting FixTime and Fix3DPlus");
            
            auto FnOnFixTime = [this, &scheduler](const FixTime &fixTime){
                t_.Event("FixTime");

                // cancel timer
                CancelGpsLockOrDieTimer();

                // tell scheduler
                scheduler.OnGpsTimeLock(fixTime);
            };

            auto FnOnFix3DPlus = [this, &scheduler](const Fix3DPlus &fix3dPlus){
                t_.Event("Fix3DPlus");

                // cancel timer
                CancelGpsLockOrDieTimer();

                // capture fix
                fix3dPlus_ = fix3dPlus;

                // note that the 3d fix was acquired
                gotFix3dPlus_ = true;

                // tell scheduler
                scheduler.OnGps3DPlusLock(fix3dPlus_);
            };

            ssGps_.RequestNewFixTimeAnd3DPlus(FnOnFixTime, FnOnFix3DPlus);
            t_.Event("FixRequested");

            // Setup timer to ensure we don't wait forever
            StartGpsLockOrDieTimer();
        });

        scheduler.SetCallbackCancelRequestNewGpsLock([this]{
            // consider whether too much coasting
            MaybeDieIfTooMuchCoasting();

            // indicate idle state
            BlinkerIdle();

            // shut off gps
            ssGps_.Disable();
        });
    }

    void SetupSchedulerMessageSending()
    {
        auto &scheduler = ssCc_.GetScheduler();

        scheduler.SetCallbackScheduleNow([this, &scheduler](bool haveGpsLock){
            if (haveGpsLock)
            {
                scheduler.SetCallbackSendDefault(1, true, [this](uint8_t, uint64_t){ SendRegularType1();   });
                scheduler.SetCallbackSendDefault(2, true, [this](uint8_t, uint64_t){ SendBasicTelemetry(); });
            }
            else
            {
                scheduler.UnSetCallbackSendDefault(1);
                scheduler.UnSetCallbackSendDefault(2);
            }
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
    // Message Sending
    /////////////////////////////////////////////////////////////////

    void SendRegularType1()
    {
        const Configuration &txCfg = ssTx_.GetConfiguration();
        static const uint8_t POWER_DBM = 13;

        Log("Sending regular start");
        ssTx_.SendRegularMessage(txCfg.callsign, fix3dPlus_.maidenheadGrid.substr(0, 4), POWER_DBM);
        Log("Sending regular done");
    };

    void SendBasicTelemetry()
    {
        // get data needed to fill out encoded message
        const Configuration &txCfg = ssTx_.GetConfiguration();
        WsprChannelMap::ChannelDetails cd = WsprChannelMap::GetChannelDetails(txCfg.band.c_str(), txCfg.channel);

        string   grid56    = fix3dPlus_.maidenheadGrid.substr(4, 2);
        uint32_t altM      = fix3dPlus_.altitudeM < 0 ? 0 : fix3dPlus_.altitudeM;
        int8_t   tempC     = tempSensor_.GetTempC();
        double   voltage   = (double)ADC::GetMilliVoltsVCC() / 1'000;  // capture under max load
        bool     gpsValid  = true;

        ssTx_.SendTelemetryBasic(
            cd.id13,
            grid56,
            altM,
            tempC,
            voltage,
            fix3dPlus_.speedKnots,
            gpsValid
        );
    };


    /////////////////////////////////////////////////////////////////
    // GPS health
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

    void MaybeDieIfTooMuchCoasting()
    {
        // The strategy around GPS locking has two limits:
        // - Any attempt at a lock can take no more than the max timeout
        //   - This applies to getting either a time lock or 3d lock
        // - The system can coast for no more than 2 consecutive windows
        //
        // The consequences are:
        // - In a default configuration, where 3d fix required, and only coasting
        //   - the time before reset will be the sum of:
        //     - duration to get first time lock (less than 20 min)
        //     - two windows (20 min)
        //     - duration to learn going to coast again (0 min)
        //       - this will be within the second window, so zero additional time
        //     - this is longer than 20 minutes but less than 40
        //     - this also means the gps will be off/on twice during that time, which
        //       maybe resolves the issue anyway

        // check if coasting
        if (gotFix3dPlus_) { coastCount_ = 0; }
        else               { ++coastCount_;   }

        // reset coast detection state for next window
        gotFix3dPlus_ = false;

        // consider if coasting too much
        const uint8_t COAST_COUNT_MAX = 2;
        if (coastCount_ > COAST_COUNT_MAX)
        {
            LogModeSync();

            LogNL();
            Log("Coast attempt exceeds limit (", coastCount_, " would exceed max of ", COAST_COUNT_MAX, " consecutive)");

            // hard reset GPS
            Log("Hard Resetting GPS");
            ssGps_.ModuleHardReset();

            // reboot via watchdog kill
            Log("Rebooting via Watchdog death");
            while (true)
            {
                BlinkerBlinkOncePanic();
            }
        }
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

        // prepare to switch to 48MHz in the event that USB is connected
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
        // empirically I see 45MHz is the minimum required but let's do 48MHz
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

    Fix3DPlus fix3dPlus_;
    bool gotFix3dPlus_ = false;
    uint8_t coastCount_ = 0;

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




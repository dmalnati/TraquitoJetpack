#pragma once

#include "App.h"
#include "GPS.h"
#include "JSONMsgRouter.h"



inline static int InitSubsystemGps()
{
    // 7ms from 180ms of gps draining high current at startup.
    // possible the uart driver inits and the time between then and
    // now is the high current.
    Pin{ DT_GET(pin_gps_reset), Pin::Type::OUTPUT, 0 };
    Pin{ DT_GET(pin_gps_uart1_tx), Pin::Type::OUTPUT, 0 };

    return 1;
}

#include <zephyr/init.h>
SYS_INIT(InitSubsystemGps, POST_KERNEL, 1);


class SubsystemGps
{
public:

    SubsystemGps()
    {
        ReadDeviceTree();

        UartDisable(UART::UART_1);

        Disable();

        SetupShell();
        SetupJSON();
    }

    void DisableVerboseLogging()
    {
        gpsWriter_.DisableVerboseLogging();
        gpsReader_.DisableVerboseLogging();
    }

    void EnableConfigurationMode()
    {
        EnableInternal(true);
        EnterMonitorMode();
    }

    void EnableFlightMode()
    {
        EnableInternal(false);
    }

    void EnableInternal(bool maxGpsMessages)
    {
        ModulePowerOnBatteryOn();

        // Have GPSWriter watch for NMEA/UBX messages (replies to commands)
        gpsWriter_.Reset();
        gpsWriter_.StartMonitorForReplies();

        // Issue commands to set params and save configuration
        gpsWriter_.SendHighAltitudeMode();
        if (maxGpsMessages)
        {
            gpsWriter_.SendModuleMessageRateConfigurationMaximal();
        }
        else
        {
            gpsWriter_.SendModuleMessageRateConfigurationMinimal();
        }
        gpsWriter_.SendModuleSaveConfiguration();

        // Start decoding NMEA
        gpsReader_.Reset();
        gpsReader_.StartMonitoring();
    }

    void RequestNewFix3DPlus(function<void(const Fix3DPlus &fix)> fn)
    {
        static uint64_t timeStart;
        static uint8_t count;

        timeStart = PAL.Millis();
        count = 0;

        gpsReader_.Reset();

        gpsReader_.SetCallbackOnFixTime([this](const FixTime &fix){
            Log("Got Fix Time   in ", MsToMinutesStr(PAL.Millis() - timeStart), " at GPS Time ", gpsReader_.ConvertFixTimeToString(gpsReader_.GetFixTime()), " UTC");
            gpsReader_.UnSetCallbackOnFixTime();
        });
        gpsReader_.SetCallbackOnFix2D([this](const Fix2D &fix){
            Log("Got Fix 2D     in ", MsToMinutesStr(PAL.Millis() - timeStart), " at GPS Time ", gpsReader_.ConvertFixTimeToString(gpsReader_.GetFixTime()), " UTC");
            gpsReader_.UnSetCallbackOnFix2D();
        });
        gpsReader_.SetCallbackOnFix3DPlus([=, this](const Fix3DPlus &fix){
            ++count;

            // let a few locks go by, hopefully bringing figures closer to
            // accurate where that is possible.  this is not a deeply researched
            // area, mostly leaving in place for historical purposes.  eyeballing
            // the data doesn't show any improvement in such a small delay.
            if (count == 2)
            {
                Log("Got Fix 3DPlus in ", MsToMinutesStr(PAL.Millis() - timeStart), " at GPS Time ", gpsReader_.ConvertFixTimeToString(gpsReader_.GetFixTime()), " UTC");
                LogNL();
                fn(fix);
                gpsReader_.UnSetCallbackOnFix3DPlus();
            }
        });
    }

    void CancelNewFix3DPlus()
    {
        gpsReader_.UnSetCallbackOnFix3DPlus();
    }

    void EnterMonitorMode()
    {
        Log("GPS Monitor Mode");

        UartAddLineStreamCallback(UART::UART_1, [this](const string &line){
            if (NmeaStringParser::IsValid(line))
            {
                JSONMsgRouter::Send([&](const auto &out){
                    out["type"] = "GPS_LINE";
                    out["line"] = line.c_str();
                });
            }
        });

        StartMonitorLockSequenceWeb();
    }

    void Disable()
    {
        gpsReader_.StopMonitoring();
        gpsWriter_.StopMonitorForReplies();

        ModulePowerOffBatteryOn();
    }

private:

    void StartMonitorLockSequenceWeb()
    {
        Log("StartMonitorLockSequenceWeb");

        // for webpage -- just want time and 2D location
        MonitorConfig cfg = {
            .fixTime = {
                .unsubscribeOnFix = false,
                .emitJson = true,
            },
            .fix2D = {
                .unsubscribeOnFix = false,
                .emitJson = true,
            },
            .fix3DPlus = {
                .unsubscribeOnFix = false,
                .emitJson = true,
            },
        };

        MonitorLockSequence(cfg);
    }


public:

    /////////////////////////////////////////////////////////////////
    // GPS Subsystem Control
    /////////////////////////////////////////////////////////////////

    void ModulePowerOnBatteryOn()
    {
        Log("GPS Module Power On, Battery On");

        // enable power
        pinGpsLoadSwitchOnOff_.DigitalWrite(0);
        pinGpsReset_.DigitalWrite(1);
        pinGpsBatteryPowerOnOff_.DigitalWrite(1);

        // Give it time to start up
        PAL.Delay(500);

        // enable uart function, this overrides the previous pin function
        UartEnable(UART::UART_1);
    }

    void ModulePowerOffBatteryOn(bool log = true)
    {
        if (log)
        {
            Log("GPS Module Off, Battery On");
        }

        // prevent interrupts and any data processing for testing mode
        // when the signals above don't affect external module
        UartDisable(UART::UART_1);

        // Drive pin low to avoid current draw from GPS
        // this overrides the previous uart function
        pinUart1Tx_.ReInit();

        // pull device reset low, as it continues drawing large current if not
        pinGpsReset_.DigitalWrite(0);

        // shut off the load power source
        pinGpsLoadSwitchOnOff_.DigitalWrite(1);
    }

    void ModulePowerOff()
    {
        Log("GPS Module Off, Battery Off");

        ModulePowerOffBatteryOn(false);

        pinGpsBatteryPowerOnOff_.DigitalWrite(0);
    }

    void ModuleHardReset()
    {
        gpsWriter_.SendModuleFactoryResetConfiguration();
        gpsWriter_.SendModuleResetColdCasic();

        ModulePowerOff();
        PAL.Delay(1'000);
        ModulePowerOnBatteryOn();
    }

private:

    /////////////////////////////////////////////////////////////////
    // Test
    /////////////////////////////////////////////////////////////////

    struct FixConfig
    {
        bool unsubscribeOnFix = true;
        bool emitJson = false;
    };

    struct MonitorConfig
    {
        FixConfig fixTime;
        FixConfig fix2D;
        FixConfig fix3D;
        FixConfig fix3DPlus;
    };

    void MonitorLockSequence(const MonitorConfig &cfgIn)
    {
        static MonitorConfig cfg;
        static uint64_t timeStart;
        static Timeline gpsTimeline_;

        // This function is called from many places, most of them wanting to
        // see the effect of actions taken on the GPS and the resulting
        // lock time.
        // Clear any queued data so fresh readings are known to be the source
        // of new GPS locks
        UartClearRxBuffer(UART::UART_1);

        cfg = cfgIn;
        timeStart = PAL.Millis();

        Log("Monitoring lock sequence");

        // Have GPSReader watch for incoming NMEA messages to parse them
        auto LogLatLng = [](const Fix2D &fix){
            Log("Lat: ", fix.latDeg, " ", fix.latMin, " ", fix.latSec);
            Log("Lng: ", fix.lngDeg, " ", fix.lngMin, " ", fix.lngSec);
        };

        static bool loggedOnceFixTime;
        loggedOnceFixTime = false;
        static bool firstLockSentFixTime;
        firstLockSentFixTime = false;
        gpsReader_.SetCallbackOnFixTime([&](const FixTime &fix){
            uint64_t timeNow = PAL.Millis();

            if (!loggedOnceFixTime)
            {
                gpsTimeline_.Event("GPS Time");
                loggedOnceFixTime = true;

                Log("FixTime acquired in ", MsToMinutesStr(timeNow - timeStart));
                Log("GPS Time: ", gpsReader_.ConvertFixTimeToString(fix), " UTC");
                Log("FixTime source: ", gpsReader_.GetFixTimeSource());
                gpsTimeline_.ReportNow("GPS Time Timeline");
            }

            if (cfg.fixTime.unsubscribeOnFix)
            {
                gpsReader_.UnSetCallbackOnFixTime();
            }

            if (cfg.fixTime.emitJson)
            {
                JSONMsgRouter::Send([&](const auto &out){
                    out["type"] = "GPS_FIX_TIME";
                    out["time"] =
                        StrUtl::PadLeft(fix.hour,   '0', 2) + ":" +
                        StrUtl::PadLeft(fix.minute, '0', 2) + ":" +
                        StrUtl::PadLeft(fix.second, '0', 2);
                    
                    if (firstLockSentFixTime == false)
                    {
                        firstLockSentFixTime = true;
                        out["firstLockDuration"] = timeNow - timeStart;
                    }
                });
            }
        });

        static bool loggedOnceFix2D;
        loggedOnceFix2D = false;
        static bool firstLockSentFix2D;
        firstLockSentFix2D = false;
        gpsReader_.SetCallbackOnFix2D([&](const Fix2D &fix){
            uint64_t timeNow = PAL.Millis();

            if (!loggedOnceFix2D)
            {
                gpsTimeline_.Event("GPS 2D");
                loggedOnceFix2D = true;
                Log("Fix2D acquired in ", MsToMinutesStr(timeNow - timeStart));
                Log("GPS Time at Fix2D: ", gpsReader_.ConvertFixTimeToString(fix), " UTC");
                LogLatLng(fix);
                Log("Fix2D source: ", gpsReader_.GetFix2DSource());
                gpsTimeline_.ReportNow("GPS 2D Timeline");
            }

            if (cfg.fix2D.unsubscribeOnFix)
            {
                gpsReader_.UnSetCallbackOnFix2D();
            }

            if (cfg.fix2D.emitJson)
            {
                JSONMsgRouter::Send([&](const auto &out){
                    out["type"] = "GPS_FIX_2D";
                    out["latDeg"] = fix.latDegMillionths / 1'000'000.0;
                    out["lngDeg"] = fix.lngDegMillionths / 1'000'000.0;

                    if (firstLockSentFix2D == false)
                    {
                        firstLockSentFix2D = true;
                        out["firstLockDuration"] = timeNow - timeStart;
                    }
                });
            }
        });

        static bool loggedOnceFix3D;
        loggedOnceFix3D = false;
        gpsReader_.SetCallbackOnFix3D([&](const Fix3D &fix){
            uint64_t timeNow = PAL.Millis();

            if (!loggedOnceFix3D)
            {
                gpsTimeline_.Event("GPS 3D");
                loggedOnceFix3D = true;
                Log("Fix3D acquired in ", MsToMinutesStr(timeNow - timeStart));
                Log("GPS Time at Fix3D: ", gpsReader_.ConvertFixTimeToString(fix), " UTC");
                LogLatLng(fix);
                Log("AltM: ", Commas(fix.altitudeM), ", AltF: ", Commas(fix.altitudeFt));
                Log("Fix3D sources:");
                for (const auto &source : gpsReader_.GetFix3DSourceList())
                {
                    Log("  ", source);
                }
                gpsTimeline_.ReportNow("GPS 3D Timeline");
            }

            if (cfg.fix3D.unsubscribeOnFix)
            {
                gpsReader_.UnSetCallbackOnFix3D();
            }
        });

        static bool loggedOnceFix3DPlus;
        loggedOnceFix3DPlus = false;
        static bool firstLockSentFix3DPlus;
        firstLockSentFix3DPlus = false;
        gpsReader_.SetCallbackOnFix3DPlus([&](const Fix3DPlus &fix){
            uint64_t timeNow = PAL.Millis();

            if (!loggedOnceFix3DPlus)
            {
                gpsTimeline_.Event("GPS 3DPlus");
                loggedOnceFix3DPlus = true;
                Log("Fix3DPlus acquired in ", MsToMinutesStr(timeNow - timeStart));
                Log("GPS Time at Fix3DPlus: ", gpsReader_.ConvertFixTimeToString(fix), " UTC");
                LogLatLng(fix);
                Log("AltM: ", Commas(fix.altitudeM), ", AltF: ", Commas(fix.altitudeFt));
                Log("SpeedKnots: ", fix.speedKnots, ", CourseDeg: ", fix.courseDegrees);
                Log("Fix3DPlus sources:");
                for (const auto &source : gpsReader_.GetFix3DSourceList())
                {
                    Log("  ", source);
                }
                gpsTimeline_.ReportNow("GPS 3DPlus Timeline");
            }

            if (cfg.fix3DPlus.unsubscribeOnFix)
            {
                gpsReader_.UnSetCallbackOnFix3DPlus();
            }

            if (cfg.fix3DPlus.emitJson)
            {
                JSONMsgRouter::Send([&](const auto &out){
                    out["type"] = "GPS_FIX_3D";
                    out["altM"] = fix.altitudeM;
                    out["altF"] = fix.altitudeFt;
                    out["speedK"] = fix.speedKnots;
                    out["courseDeg"] = fix.courseDegrees;

                    if (firstLockSentFix3DPlus == false)
                    {
                        firstLockSentFix3DPlus = true;
                        out["firstLockDuration"] = timeNow - timeStart;
                    }
                });
            }
        });

        // gpsReader_.SetCallbackOnFixSatelliteList([&](const vector<FixSatelliteData> &satDataList){
        //     Log("SatList - ", satDataList.size());
        //     for (auto &satData : satDataList)
        //     {
        //         Log("  { id: ", satData.id, ", el: ", satData.elevation, ", az: ", satData.azimuth, " }");
        //     }
        // });

        gpsTimeline_.Reset();

        // Data can still be sent by the GPS even after it has been
        // reset (elsewhere).
        // This function assumes we want a clean lock, so we just blindly
        // ignore everything for the next few seconds, since the GPS cannot
        // be synchronized against easily.
        // Emperical observation shows this filters out some causes of
        // erronious locking.
        gpsReader_.ResetAndDelayProcessing(2'000);

        gpsTimeline_.Event("GPS Start");
    }


private:

    void SetupShell()
    {
        gpsWriter_.SetupShell("app.gps");

        Shell::AddCommand("app.ss.gps", [this](vector<string> argList){
            if (argList[0] == "on") { ModulePowerOnBatteryOn();  MonitorLockSequence({}); }
            else                    { ModulePowerOffBatteryOn(); }
        }, { .argCount = 1, .help = "gps subsystem <on/off>"});

        Shell::AddCommand("app.ss.gps.flightmode", [this](vector<string> argList){
            EnableFlightMode();
        }, { .argCount = 0, .help = "gps enable flight mode"});

        Shell::AddCommand("app.ss.gps.hardreset", [this](vector<string> argList){
            ModuleHardReset();
        }, { .argCount = 0, .help = "gps hard reset module"});

        Shell::AddCommand("app.ss.gps.mode.monitor", [this](vector<string> argList){
            EnterMonitorMode();
        }, { .argCount = 0, .help = "gps subsystem enter monitor mode"});

        Shell::AddCommand("app.ss.gps.send.reset", [this](vector<string> argList){
            string temp = argList[0];

            if      (temp == "hot")  { gpsWriter_.SendModuleResetHotCasic();  }
            else if (temp == "warm") { gpsWriter_.SendModuleResetWarmCasic(); }
            else                     { gpsWriter_.SendModuleResetColdCasic(); }

            MonitorLockSequence({});
        }, { .argCount = 1, .help = "gps send reset <hot/warm/cold> to module"});

        Shell::AddCommand("app.ss.gps.mon.lock", [this](vector<string> argList){
            MonitorLockSequence({});
        }, { .argCount = 0, .help = "gps monitor lock sequence"});

        Shell::AddCommand("app.ss.gps.bat", [this](vector<string> argList){
            if (argList[0] == "on") { pinGpsBatteryPowerOnOff_.DigitalWrite(1);  }
            else                    { pinGpsBatteryPowerOnOff_.DigitalWrite(0); }
        }, { .argCount = 1, .help = "gps battery <on/off>"});
    }

    void SetupJSON()
    {
        JSONMsgRouter::RegisterHandler("REQ_GPS_RESET", [this](auto &in, auto &out){
            string temp = (const char *)in["temp"];

            Log("REQ_GPS_RESET: ", temp);

            if      (temp == "hot")  { gpsWriter_.SendModuleResetHotCasic();  }
            else if (temp == "warm") { gpsWriter_.SendModuleResetWarmCasic(); }
            else                     { gpsWriter_.SendModuleResetColdCasic(); }

            StartMonitorLockSequenceWeb();
        });

        JSONMsgRouter::RegisterHandler("REQ_GPS_POWER_ON", [this](auto &in, auto &out){
            ModulePowerOnBatteryOn();
            StartMonitorLockSequenceWeb();
        });

        JSONMsgRouter::RegisterHandler("REQ_GPS_POWER_OFF_BATT_ON", [this](auto &in, auto &out){
            ModulePowerOffBatteryOn();
            StartMonitorLockSequenceWeb();
        });

        JSONMsgRouter::RegisterHandler("REQ_GPS_POWER_OFF", [this](auto &in, auto &out){
            ModulePowerOff();
            StartMonitorLockSequenceWeb();
        });
    }

    void ReadDeviceTree()
    {
        pinGpsLoadSwitchOnOff_   = { DT_GET(pin_gps_load_switch_on_off), Pin::Type::OUTPUT, 1 };
        pinGpsReset_             = { DT_GET(pin_gps_reset), Pin::Type::OUTPUT, 0 };
        pinGpsBatteryPowerOnOff_ = { DT_GET(pin_gps_battery_power_off), Pin::Type::OUTPUT, 1 };
        pinUart1Tx_              = { DT_GET(pin_gps_uart1_tx), Pin::Type::OUTPUT, 0 };

        pin1pps_ = { DT_GET(pin_1pps),   Pin::Type::INPUT };
    }


private:

    Pin pinGpsLoadSwitchOnOff_;
    Pin pinGpsReset_;
    Pin pinGpsBatteryPowerOnOff_;
    Pin pinUart1Tx_;

    Pin pin1pps_;

    GPSReader gpsReader_;
    GPSWriter gpsWriter_;
};
#pragma once

#include "App.h"
#include "JSONMsgRouter.h"
#include "WSPRMessageTransmitter.h"
#include "WSPRMessageU4B.h"

#include "Configuration.h"


// Do we want a warmup period before sending?
// I want a nice stable frequency


class SubsystemTx
{
public:

    SubsystemTx()
    {
        ReadDeviceTree();

        Disable();

        SetupShell();
        SetupJSON();
    }

    bool ReadyToFly()
    {
        bool retVal = true;

        if (cfg_.Get() != true)
        {
            retVal = false;

            Log("ERR: ReadyToFly: Could not read config");
        }

        if (cfg_.callsign == "")
        {
            retVal = false;

            Log("ERR: ReadyToFly: Callsign blank");
        }

        return retVal;
    }

    Configuration &GetConfiguration()
    {
        return cfg_;
    }

    void Enable()
    {
        Log("TX Subsystem On");
        pinTxLoadSwitchOnOff_.DigitalWrite(0);

        // Give it time to start up
        PAL.Delay(5);

        enabled_ = true;
    }

    void RadioOn()
    {
        Log("Radio on");
        wsprMessageTransmitter_.RadioOn();

        on_ = true;
    }

    void SetupTransmitterForCalibration()
    {
        // unlike normal flight mode, this:
        // - doesn't restore the saved settings (ephemeral settings used)
        // - uses the dial frequency of the band + 1500 (for calibration simplicity)
        WSPR::ChannelDetails cd = WSPR::GetChannelDetails(cfg_.band, cfg_.channel);

        Log("Setup Transmitter (Calibration mode)");
        Log("Band: ", cfg_.band, ", Channel: ", cfg_.channel);
        Log("Freq: ", Commas(cd.freqDial + 1500), ", Correction: ", cfg_.correction);
        LogNL();

        wsprMessageTransmitter_.SetFrequency(cd.freqDial + 1500);
        wsprMessageTransmitter_.SetCorrection(cfg_.correction);
    }

    void SetupTransmitterForFlight()
    {
        // make sure config is the stored version
        cfg_.Get();

        WSPR::ChannelDetails cd = WSPR::GetChannelDetails(cfg_.band, cfg_.channel);

        Log("Setup Transmitter (Flight mode)");
        Log("Band: ", cfg_.band, ", Channel: ", cfg_.channel);
        Log("Freq: ", Commas(cd.freq), ", Correction: ", cfg_.correction);
        LogNL();

        wsprMessageTransmitter_.SetFrequency(cd.freq);
        wsprMessageTransmitter_.SetCorrection(cfg_.correction);
    }

    void SetCallbackOnTxStart(function<void()> fn)
    {
        wsprMessageTransmitter_.SetCallbackOnTxStart(fn);
    }

    void SetCallbackOnBitChange(function<void()> fn)
    {
        wsprMessageTransmitter_.SetCallbackOnBitChange(fn);
    }

    void SetCallbackOnTxEnd(function<void()> fn)
    {
        wsprMessageTransmitter_.SetCallbackOnTxEnd(fn);
    }

    void SendRegularMessage(string callsign, string grid, uint8_t powerDbm)
    {
        WSPRMessage msg;
        msg.SetCallsign(callsign);
        msg.SetGrid(grid);
        msg.SetPowerDbm(powerDbm);

        Log("Sending regular msg: ", msg.GetCallsign(), " ", msg.GetGrid(), " ", msg.GetPowerDbm());
        SendMessage(msg);
        Log("Sent");
    }

    void SendEncodedMessage(string   id13,
                            string   grid56,
                            int32_t  altM,
                            int32_t  tempC,
                            double   voltage,
                            uint32_t speedKnots,
                            bool     gpsValid)
    {
        Log("Encoding message");
        Log("ID13      : ", id13);
        Log("Grid56    : ", grid56);
        Log("AltM      : ", altM);
        Log("TempC     : ", tempC);
        Log("Voltage   : ", voltage);
        Log("SpeedKnots: ", speedKnots);
        Log("GpsValid  : ", gpsValid);

        // fill out encoded message
        WSPRMessageU4B msgU4b;
        msgU4b.SetId13(id13);
        msgU4b.SetGrid56(grid56);
        msgU4b.SetAltM(altM);
        msgU4b.SetTempC(tempC);
        msgU4b.SetVoltage(voltage);
        msgU4b.SetSpeedKnots(speedKnots);
        msgU4b.SetGpsValid(gpsValid);

        // send encoded message
        Log("Sending encoded msg: ", msgU4b.GetCallsign(), " ", msgU4b.GetGrid(), " ", msgU4b.GetPowerDbm());
        SendMessage(msgU4b);
        Log("Sent");
        LogNL();
    }

    void SendMessage(const WSPRMessage &msg)
    {
        wsprMessageTransmitter_.Send(msg);
    }

    void RadioOff()
    {
        Log("Radio off");
        LogNL();
        wsprMessageTransmitter_.RadioOff();

        on_ = false;
    }

    void Disable()
    {
        Log("TX Subsystem Off");
        LogNL();
        pinTxLoadSwitchOnOff_.DigitalWrite(1);

        enabled_ = false;
    }


private:


    void TestWsprSend(string callsign, string grid, uint8_t powerDbm = 17)
    {
        Timeline::Global().SetMaxEvents(300);
        Timeline::Global().Reset();
        Timeline::Global().Event("TestWsprSend Start");

        bool enableCache = enabled_;
        bool onCache = on_;

        if (enableCache == false)
        {
            Enable();
        }

        SetupTransmitterForFlight();

        if (onCache == false)
        {
            RadioOn();
        }

        Log("Sending");
        SendRegularMessage(callsign, grid, powerDbm);
        Log("Sent");

        if (onCache == false)
        {
            RadioOff();
        }

        SetupTransmitterForCalibration();

        if (enableCache == false)
        {
            Disable();
        }

        Timeline::Global().Event("TestWsprSend End");
        Timeline::Global().Report();
    }

    void SetupShell()
    {
        WSPR::SetupShell();
        WSPRMessageU4B::SetupShell();

        ///////////////////////////////////////////////////
        // App WSPR testing
        ///////////////////////////////////////////////////

        Shell::AddCommand("app.tx", [this](vector<string> argList){
            if (argList[0] == "on") { Enable();  }
            else                    { Disable(); }
        }, { .argCount = 1, .help = "tx subsystem <on/off>"});

        Shell::AddCommand("app.wspr.send", [this](vector<string> argList){
            string callsign = argList[0];
            string grid = argList[1];

            Evm::QueueWork("app.wspr.send", [=, this]{
                TestWsprSend(callsign, grid);
            });
        }, { .argCount = 2, .help = "wspr send <callsign> <grid>"});
    }

    void SetupJSON()
    {
        JSONMsgRouter::RegisterHandler("REQ_RADIO_POWER_ON", [this](auto &in, auto &out){
            Log("REQ_RADIO_POWER_ON");

            Enable();
        });

        JSONMsgRouter::RegisterHandler("REQ_RADIO_POWER_OFF", [this](auto &in, auto &out){
            Log("REQ_RADIO_POWER_OFF");

            Disable();
        });

        JSONMsgRouter::RegisterHandler("REQ_RADIO_OUTPUT_ENABLE", [this](auto &in, auto &out){
            Log("REQ_RADIO_OUTPUT_ENABLE");

            Enable();
            RadioOn();
        });

        JSONMsgRouter::RegisterHandler("REQ_RADIO_OUTPUT_DISABLE", [this](auto &in, auto &out){
            Log("REQ_RADIO_OUTPUT_DISABLE");

            if (enabled_)
            {
                RadioOff();
            }
        });

        // interactive control -- does not commit to saved state
        JSONMsgRouter::RegisterHandler("REQ_SET_CONFIG_TEMP", [this](auto &in, auto &out){
            Log("REQ_SET_CONFIG_TEMP");

            string band = (string)in["band"];
            uint16_t channel = (uint16_t)in["channel"];
            int32_t correction = (int32_t)in["correction"];

            cfg_.band       = band;
            cfg_.channel    = channel;
            cfg_.correction = correction;

            SetupTransmitterForCalibration();
        });

        JSONMsgRouter::RegisterHandler("REQ_RESTORE_CONFIG", [this](auto &in, auto &out){
            Log("REQ_RESTORE_CONFIG");

            SetupTransmitterForFlight();
        });

        JSONMsgRouter::RegisterHandler("REQ_WSPR_SEND", [this](auto &in, auto &out){
            out["type"] = "REP_WSPR_SEND";

            Log("REQ_WSPR_SEND");

            string callsign  = (const char *)in["callsign"];
            string grid      = (const char *)in["grid"];
            uint8_t powerDbm = (uint8_t)in["power"];

            Log("callsign: ", callsign, ", grid: ", grid, ", powerDbm: ", powerDbm);

            TestWsprSend(callsign, grid, powerDbm);
        });
    }

    void ReadDeviceTree()
    {
        pinTxLoadSwitchOnOff_ = { DT_GET(pin_tx_load_switch_on_off), Pin::Type::OUTPUT, 1 };

        pinSi5351_ = { DT_GET(pin_si5351), Pin::Type::INPUT };
    }


private:

    Configuration cfg_;

    Pin pinTxLoadSwitchOnOff_;
    Pin pinSi5351_;

    bool enabled_ = false;
    bool on_ = false;

    WSPRMessageTransmitter wsprMessageTransmitter_;
};
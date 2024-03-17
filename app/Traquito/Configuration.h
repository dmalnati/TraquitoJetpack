#pragma once

#include <array>
#include <string>
using namespace std;

#include "Flashable.h"
#include "JSONMsgRouter.h"
#include "WSPR.h"


class Configuration
{
private:
    inline static const string DEFAULT_BAND = "20m";

    struct ConfigurationFlashState
    {
        array<char, 5 + 1> bandStorage;
        uint16_t channel;
        array<char, 6 + 1> callsignStorage;
        int32_t correction;
    };

    Flashable<ConfigurationFlashState> flashState_;

    void Reset()
    {
        flashState_.bandStorage.fill(0);
        band = DEFAULT_BAND;

        flashState_.channel = 0;
        channel = 0;

        flashState_.callsignStorage.fill(0);
        callsign = "";

        flashState_.correction = 0;
        correction = 0;
    }

public:
    Configuration()
    {
        Reset();

        // Pull up stored configuration
        bool cfgOk = Get();

        LogNL();
        if (cfgOk == false)
        {
            Log("INF: No saved configuration");
        }
        else
        {
            Log("INF: Restoring saved configuration");
        }

        SetupShell();
        SetupJSON();
    }

    bool Get()
    {
        bool retVal = flashState_.Get();

        // the storage values are a write-through cache.
        // don't read from them unless you know you've written to them first
        if (retVal)
        {
            band       = (const char *)flashState_.bandStorage.data();
            channel    = (uint16_t)flashState_.channel;
            callsign   = (const char *)flashState_.callsignStorage.data();
            correction = flashState_.correction;
        }

        return retVal;
    }

    bool Put()
    {
        flashState_.bandStorage.fill(0);
        band.copy(flashState_.bandStorage.data(), min(band.size(), flashState_.bandStorage.size()));

        flashState_.channel = channel;

        flashState_.callsignStorage.fill(0);
        callsign.copy(flashState_.callsignStorage.data(), min(callsign.size(), flashState_.callsignStorage.size()));

        flashState_.correction = correction;

        return flashState_.Put();
    }


private:

    void SetupShell()
    {
        Shell::AddCommand("app.cfg.del", [this](vector<string> argList){
            flashState_.Delete();
            Reset();

            Log("Configuration Deleted, state reset");
        }, { .argCount = 0, .help = "delete config"});
    }

    void SetupJSON()
    {
        JSONMsgRouter::RegisterHandler("REQ_GET_CONFIG", [this](auto &in, auto &out){
            out["type"] = "REP_GET_CONFIG";

            out["band"]       = band;
            out["channel"]    = channel;
            out["callsign"]   = callsign;
            out["correction"] = correction;

            out["callsignOk"] = WSPR::CallsignIsValid(callsign);
        });

        JSONMsgRouter::RegisterHandler("REQ_SET_CONFIG", [this](auto &in, auto &out){
            out["type"] = "REP_SET_CONFIG";

            string bandIn        = (const char *)in["band"];
            int16_t channelIn    = (int16_t)in["channel"];
            string callsignIn    = (const char *)in["callsign"];
            int32_t correctionIn = (int32_t)in["correction"];

            bool ok = true;
            string err = "";
            string sep = "";

            if (bandIn != WSPR::GetDefaultBandIfNotValid(bandIn))
            {
                ok = false;
                err += sep + "Invalid band";
                sep = ", ";
            }
            
            if (channelIn != WSPR::GetDefaultChannelIfNotValid(channelIn))
            {
                ok = false;
                err += sep + "Invalid channel";
                sep = ", ";
            }

            if (WSPR::CallsignIsValid(callsignIn) == false)
            {
                ok = false;
                err += sep + "Invalid callsign";
                sep = ", ";
            }

            if (ok)
            {
                // attempt to store
                Configuration cfgCache = *this;

                band       = bandIn;
                channel    = channelIn;
                callsign   = callsignIn;
                correction = correctionIn;

                ok = Put();

                if (ok == false)
                {
                    *this = cfgCache;

                    err += sep + "Could not store to flash";
                    sep = ", ";
                }
            }

            Log("REQ_SET_CONFIG: ", bandIn, ", ", channelIn, ", ", callsignIn, ", ", correctionIn);
            Log("OK: ", ok, ", err: \"", err, "\"");
            Log("Current config: ", band, ", ", channel, ", ", callsign, ", ", correction);

            out["ok"] = ok;
            out["err"] = err;
        });
    }

public:

    string   band;
    uint16_t channel;
    string   callsign;
    int32_t correction;
};

inline void LogNNL(const Configuration &c)
{
    Log("{");
    Log("  .band       = ", c.band);
    Log("  .callsign   = ", c.callsign);
    Log("  .channel    = ", c.channel);
    Log("  .correction = ", c.correction);
    LogNNL("}");
}

inline void Log(const Configuration &c)
{
    LogNNL(c);
    LogNL();
}

#pragma once

#include "CopilotControlConfiguration.h"
#include "CopilotControlMessageDefinition.h"
#include "CopilotControlUtl.h"
#include "JerryScriptIntegration.h"
#include "JSFn_DelayMs.h"
#include "JSObj_ADC.h"
#include "JSObj_BH1750.h"
#include "JSObj_BME280.h"
#include "JSObj_BMP280.h"
#include "JSObj_DS18X.h"
#include "JSObj_I2C.h"
#include "JSObj_MMC56x3.h"
#include "JSObj_Pin.h"
#include "JSObj_SI7021.h"
#include "JSProxy_GPS.h"
#include "JSProxy_WsprMessageTelemetryExtendedUserDefined.h"
#include "Log.h"
#include "Shell.h"
#include "TempSensorInternal.h"
#include "Utl.h"
#include "WsprEncodedDynamic.h"

#include <string>
#include <vector>
using namespace std;


class CopilotControlJavaScript
{
public:

    CopilotControlJavaScript()
    {
        SetupShell();
        SetupJSON();
        CalculateJavaScriptBaselineUsage();
        LogNL();
    }

    uint64_t GetScriptTimeLimitMs()
    {
        return SCRIPT_TIME_LIMIT_MS;
    }


private:


    /////////////////////////////////////////////////////////////////
    // Javascript Execution Functions
    /////////////////////////////////////////////////////////////////

    // assumes the VM is running
    static void LoadJavaScriptBindings(MsgUD &msg, Fix3DPlus *gpsFix = nullptr)
    {
        // UserDefined Message API
        JerryScript::UseThenFreeNewObj([&](auto obj){
            JerryScript::SetGlobalPropertyNoFree("msg", obj);

            JSProxy_WsprMessageTelemetryExtendedUserDefined::Proxy(obj, (MsgUD *)&msg);
        });

        // GPS API
        JerryScript::UseThenFreeNewObj([&](auto obj){
            JerryScript::SetGlobalPropertyNoFree("gps", obj);

            static Fix3DPlus gpsFixExample = GPSReader::GetFix3DPlusExample();

            Fix3DPlus *gpsFixUse = gpsFix ? gpsFix : &gpsFixExample;

            JSProxy_GPS::Proxy(obj, gpsFixUse);
        });

        // I2C API
        JSObj_I2C::SetI2CInstance(I2C::Instance::I2C1);
        JSObj_I2C::Register();

        // Pin API
        JSObj_Pin::SetPinWhitelist({ 10, 11, 12, 13, 20, 21 });
        JSObj_Pin::Register();

        // ADC API
        JSObj_ADC::SetPinWhitelist({ 26, 27 });
        JSObj_ADC::Register();

        // SYS API
        JerryScript::UseThenFreeNewObj([&](auto obj){
            JerryScript::SetGlobalPropertyNoFree("sys", obj);

            JerryScript::SetPropertyToNativeFunction(obj, "GetTemperatureFahrenheit", []{
                return TempSensorInternal::GetTempF();
            });
            JerryScript::SetPropertyToNativeFunction(obj, "GetTemperatureCelsius", []{
                return TempSensorInternal::GetTempC();
            });
            JerryScript::SetPropertyToNativeFunction(obj, "GetInputVoltageVolts", []{
                return (double)ADC::GetMilliVoltsVCC() / 1'000;
            });
        });

        // Basic Functions API
        JSFn_DelayMs::Register();

        // BH1750 Sensor API
        JSObj_BH1750::SetI2CInstance(I2C::Instance::I2C1);
        JSObj_BH1750::Register();

        // BME280 Sensor API
        JSObj_BME280::SetI2CInstance(I2C::Instance::I2C1);
        JSObj_BME280::Register();

        // BMP280 Sensor API
        JSObj_BMP280::SetI2CInstance(I2C::Instance::I2C1);
        JSObj_BMP280::Register();

        // DS18X Sensor API
        JSObj_DS18X::SetPinWhitelist({ 10, 11, 12, 13, 20, 21 });
        JSObj_DS18X::Register();

        // MMC56x3 Sensor API
        JSObj_MMC56x3::SetI2CInstance(I2C::Instance::I2C1);
        JSObj_MMC56x3::Register();

        // SI7021 Sensor API
        JSObj_SI7021::SetI2CInstance(I2C::Instance::I2C1);
        JSObj_SI7021::Register();
    }

    struct JavaScriptRunResult
    {
        bool     parseOk  = false;
        string   parseErr = "[Did not parse]";
        uint64_t parseMs  = 0;

        bool     runOk       = false;
        string   runErr      = "[Did not run]";
        uint64_t runMs       = 0;
        uint64_t runDelayMs  = 0;
        uint32_t runMemAvail = 0;
        uint32_t runMemUsed  = 0;
        string   runOutput;

        string  msgStateStr;
    };

    JavaScriptRunResult RunSlotJavaScriptCustomScript(const string &slotName, const string &script)
    {
        // look up slot context
        MsgUD &msg = CopilotControlMessageDefinition::GetMsgResetAndConfigureBySlotName(slotName);

        return RunJavaScript(script, msg);
    }

public:
    JavaScriptRunResult RunSlotJavaScript(const string &slotName, Fix3DPlus *gpsFix = nullptr)
    {
        MsgUD  &msg    = CopilotControlMessageDefinition::GetMsgResetAndConfigureBySlotName(slotName);
        string  script = CopilotControlConfiguration::GetJavaScript(slotName);

        return RunJavaScript(script, msg, gpsFix);
    }
private:

    JavaScriptRunResult RunJavaScript(const string &script, MsgUD &msg, Fix3DPlus *gpsFix = nullptr)
    {
        JavaScriptRunResult retVal;

        Log("Running script");
        JerryScript::UseVM([&]{
            // parse to detect errors
            retVal.parseErr = JerryScript::ParseScript(script);
            retVal.parseOk  = retVal.parseErr == "";
            retVal.parseMs  = JerryScript::GetScriptParseDurationMs();

            if (retVal.parseOk)
            {
                // reset message values to default
                msg.Reset();

                // load javascript integrations
                LoadJavaScriptBindings(msg, gpsFix);

                // set maximum execution time
                JSFn_DelayMs::SetTotalDurationLimitMs(SCRIPT_TIME_LIMIT_MS);
                JSFn_DelayMs::StartTimeNow();

                // run it
                retVal.runErr = JerryScript::ParseAndRunScript(script, SCRIPT_TIME_LIMIT_MS);

                // capture result of run
                retVal.runOk      = retVal.runErr == "";
                retVal.runMs      = JerryScript::GetScriptRunDurationMs();
                retVal.runDelayMs = JSFn_DelayMs::GetTotalDelayTimeMs();
                retVal.runOutput  = JerryScript::GetScriptOutput();

                retVal.msgStateStr = CopilotControlUtl::GetMsgStateAsString(msg);
            }
        });

        // capture memory utilization stats
        retVal.runMemAvail = JerryScript::GetHeapCapacity();
        retVal.runMemUsed  = JerryScript::GetHeapSizeMax();

        Log("ParseOk: ", retVal.parseOk, ", ", retVal.parseMs, " ms");
        if (retVal.parseOk)
        {
            int pct = retVal.runMemUsed * 100 / retVal.runMemAvail;

            uint64_t runMsScript = retVal.runMs - retVal.runDelayMs;

            Log("RunOk  : ", retVal.runOk, ", ", retVal.runMs, " ms (", runMsScript, " ms script / ", retVal.runDelayMs, " ms delay), ", pct, " % heap used (", Commas(retVal.runMemUsed), " / ", Commas(retVal.runMemAvail), ")");
        }
        if (retVal.runOk)
        {
            Log("Script output:");
            Log(retVal.runOutput);
        }
        else
        {
            Log(retVal.runErr);
        }
        Log("Message state:");
        Log(retVal.msgStateStr);
        LogNL();

        return retVal;
    }


    /////////////////////////////////////////////////////////////////
    // JavaScript Utility Functions
    /////////////////////////////////////////////////////////////////

    bool ScriptHasNonCommentedSubString(const string &script, const string &substr)
    {
        bool retVal = false;

        vector<string> lineList = Split(script, "\n", false, true);

        string sep = "";
        for (size_t i = 0; i < lineList.size() && retVal == false; ++i)
        {
            string &line = lineList[i];

            // chop off any commented part of the line
            string lineUncommentedPart = Split(line, "//", false, true)[0];

            // split by the string we're looking for.
            // if the returned list is greater than 1, the substring exists.
            vector<string> splitList = Split(lineUncommentedPart, substr, false, true);
            retVal = splitList.size() > 1;
        }

        return retVal;
    }

    bool ScriptUsesAPIGPS(const string &script)
    {
        return ScriptHasNonCommentedSubString(script, "gps.Get");
    }

    bool ScriptUsesAPIMsg(const string &script)
    {
        return ScriptHasNonCommentedSubString(script, "msg.Set");
    }


public:

    struct APIUsage
    {
        bool gps = false;
        bool msg = false;
    };

    APIUsage GetSlotScriptAPIUsage(const string &slotName)
    {
        string script = CopilotControlConfiguration::GetJavaScript(slotName);

        return {
            ScriptUsesAPIGPS(script),
            ScriptUsesAPIMsg(script),
        };
    }


private:


    /////////////////////////////////////////////////////////////////
    // Baseline Resource Utilization Measurement
    /////////////////////////////////////////////////////////////////

    void CalculateJavaScriptBaselineUsage()
    {
        // get a baseline reading of how much resource usage the VM and bindings
        // takes so that users can be told how much of the remaining capacity
        // their script uses

        // create a baseline situation where no fields are configured
        auto retVal = RunSlotJavaScript("");

        runMemUsedBaseline_ = retVal.runMemUsed;

        int pctUse = runMemUsedBaseline_ * 100 / retVal.runMemAvail;
        int pctAvail = 100 - pctUse;
        Log("JavaScript Baseline Resource Allocation:");
        Log("- Pct Heap Use : ", pctUse,   " % (", Commas(runMemUsedBaseline_),                      " / ", Commas(retVal.runMemAvail), ")");
        Log("- Pct Heap Free: ", pctAvail, " % (", Commas(retVal.runMemAvail - runMemUsedBaseline_), " / ", Commas(retVal.runMemAvail), ")");
    }


private:

    /////////////////////////////////////////////////////////////////
    // Shell and JSON setup
    /////////////////////////////////////////////////////////////////

    void SetupShell()
    {
        Shell::AddCommand("app.ss.cc.run", [&](vector<string> argList){
            string slotName = string{"slot"} + argList[0];

            RunSlotJavaScript(slotName);
        }, { .argCount = 1, .help = "run <slotNum> js"});
    }

    void SetupJSON()
    {
        JSONMsgRouter::RegisterHandler("REQ_PARSE_JS", [this](auto &in, auto &out){
            string name   = (const char *)in["name"];
            string script = (const char *)in["script"];

            Log("Parsing script for ", name);
            // Log(script);

            string err;
            JerryScript::UseVM([&]{
                err = JerryScript::ParseScript(script);
            });

            Log("Return: ", err);
            LogNL();

            bool ok = err == "";

            out["type"]         = "REP_PARSE_JS";
            out["name"]         = name;
            out["ok"]           = ok;
            out["err"]          = err;
            out["parseMs"]      = JerryScript::GetScriptParseDurationMs();
            out["vmOverheadMs"] = JerryScript::GetVMOverheadDurationMs();
        });

        JSONMsgRouter::RegisterHandler("REQ_RUN_JS", [this](auto &in, auto &out){
            string name   = (const char *)in["name"];
            string script = (const char *)in["script"];

            Log("REQ_RUN_JS - ", name);

            JavaScriptRunResult result = RunSlotJavaScriptCustomScript(name, script);

            // give user a view of what they have influence over, not the underlying
            // actual capacity of the system.
            uint32_t runMemUsed  = result.runMemUsed - runMemUsedBaseline_;
            uint32_t runMemAvail = result.runMemAvail - runMemUsedBaseline_;

            // determine what important bindings are being used
            bool usesAPIGPS = ScriptUsesAPIGPS(script);
            bool usesAPIMsg = ScriptUsesAPIMsg(script);

            int pctUse = runMemUsed * 100 / runMemAvail;
            Log("User Heap: ", pctUse, " % (", Commas(runMemUsed), " / ", Commas(runMemAvail), ")");
            Log("Uses GPS : ", usesAPIGPS);
            Log("Uses Msg : ", usesAPIMsg);

            out["type"]        = "REP_RUN_JS";
            out["name"]        = name;
            out["parseOk"]     = result.parseOk;
            out["parseErr"]    = result.parseErr;
            out["parseMs"]     = result.parseMs;
            out["runOk"]       = result.runOk;
            out["runErr"]      = result.runErr;
            out["runMs"]       = result.runMs;
            out["runDelayMs"]  = result.runDelayMs;
            out["runLimitMs"]  = SCRIPT_TIME_LIMIT_MS;
            out["runMemAvail"] = runMemAvail;
            out["runMemUsed"]  = runMemUsed;
            out["runOutput"]   = result.runOutput;
            out["usesAPIGPS"]  = usesAPIGPS;
            out["usesAPIMsg"]  = usesAPIMsg;
            out["msgState"]    = result.msgStateStr;
        });
    }


private:

    static inline const uint64_t SCRIPT_TIME_LIMIT_MS = 1'000;

    uint32_t runMemUsedBaseline_ = 0;
};
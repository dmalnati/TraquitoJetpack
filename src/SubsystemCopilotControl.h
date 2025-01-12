#pragma once

#include <algorithm>
using namespace std;

#include "FilesystemLittleFS.h"
#include "JerryScriptIntegration.h"
#include "JSFn_DelayMs.h"
#include "JSON.h"
#include "JSONMsgRouter.h"
#include "JSObj_ADC.h"
#include "JSObj_BH1750.h"
#include "JSObj_BME280.h"
#include "JSObj_BMP280.h"
#include "JSObj_I2C.h"
#include "JSObj_MMC56x3.h"
#include "JSObj_Pin.h"
#include "JSObj_SI7021.h"
#include "JSProxy_GPS.h"
#include "JSProxy_WsprMessageTelemetryExtendedUserDefined.h"
#include "Shell.h"
#include "TempSensorInternal.h"
#include "Utl.h"
#include "WsprEncoded.h"
#include "WsprMessageTelemetryExtendedUserDefinedDynamic.h"


class SubsystemUserDefined
{
    using MsgUD  = WsprMessageTelemetryExtendedUserDefined<29>;
    using MsgUDD = WsprMessageTelemetryExtendedUserDefinedDynamic<29>;


public:

    SubsystemUserDefined()
    {
        SetupShell();
        SetupJSON();
        SetupJavaScript();
        LogNL();
    }


private:

    /////////////////////////////////////////////////////////////////
    // Application Logic
    /////////////////////////////////////////////////////////////////

    void SetupJavaScript()
    {
        // get a baseline reading of how much resource usage the VM and bindings
        // takes so that users can be told how much of the remaining capacity
        // their script uses

        // create a baseline situation where no fields are configured
        auto retVal = RunSlotJavaScript("", "");

        runMemUsedBaseline_ = retVal.runMemUsed;

        int pctUse = runMemUsedBaseline_ * 100 / retVal.runMemAvail;
        int pctAvail = 100 - pctUse;
        Log("JavaScript Baseline Resource Allocation:");
        Log("- Pct Heap Use : ", pctUse,   " % (", Commas(runMemUsedBaseline_),                      " / ", Commas(retVal.runMemAvail), ")");
        Log("- Pct Heap Free: ", pctAvail, " % (", Commas(retVal.runMemAvail - runMemUsedBaseline_), " / ", Commas(retVal.runMemAvail), ")");
    }

    // 20ms at 48MHz with 29 fields (aka don't worry about it)
    static bool ConfigureUserDefinedMessageFromMsgDef(MsgUDD &msg, const string &msgDef)
    {
        bool retVal = false;

        msg.ResetEverything();

        string jsonStr;
        jsonStr += "{ \"fieldDefList\": [";
        jsonStr += "\n";
        jsonStr += SanitizeMsgDef(msgDef);
        jsonStr += "\n";
        jsonStr += "] }";

        JSON::UseJSON(jsonStr, [&](auto &json){
            retVal = true;

            JsonArray jsonFieldDefList = json["fieldDefList"];
            for (auto jsonFieldDef : jsonFieldDefList)
            {
                // ensure keys exist
                vector<const char *> keyList = { "name", "unit", "lowValue", "highValue", "stepSize" };
                if (JSON::HasKeyList(jsonFieldDef, keyList))
                {
                    // extract fields
                    string name      = (const char *)jsonFieldDef["name"];
                    string unit      = (const char *)jsonFieldDef["unit"];
                    double lowValue  = (double)jsonFieldDef["lowValue"];
                    double highValue = (double)jsonFieldDef["highValue"];
                    double stepSize  = (double)jsonFieldDef["stepSize"];

                    const string fieldName = name + unit;

                    if (msg.DefineField(fieldName.c_str(), lowValue, highValue, stepSize) == false)
                    {
                        retVal = false;

                        string line = string{"name: "} + name + ", " + "unit: " + unit + ", " + "lowValue: " + to_string(lowValue) + ", " + "highValue: " + to_string(highValue) + ", " + "stepSize: " + to_string(stepSize) + ", ";

                        Log("Failed to define field:");
                        Log("- field: ", fieldName);
                        Log("- line : ", line);
                        Log("- err  : ", msg.GetDefineFieldErr());
                    }
                }
                else
                {
                    retVal = false;

                    Log("Field definition missing keys");
                }
            }
        });

        if (retVal == false)
        {
            Log("JSON:");
            Log(jsonStr);
            LogNL();
        }

        return retVal;
    }


    /////////////////////////////////////////////////////////////////
    // Javascript Execution Functions
    /////////////////////////////////////////////////////////////////

    // assumes the VM is running
    static void LoadJavaScriptBindings(MsgUDD &msg)
    {
        // UserDefined Message API
        JerryScript::UseThenFreeNewObj([&](auto obj){
            JerryScript::SetGlobalPropertyNoFree("msg", obj);

            JSProxy_WsprMessageTelemetryExtendedUserDefined::Proxy(obj, (MsgUD *)&msg);
        });

        // GPS API
        JerryScript::UseThenFreeNewObj([&](auto obj){
            JerryScript::SetGlobalPropertyNoFree("gps", obj);

            static Fix3DPlus gpsFix;
            gpsFix = GPSReader::GetFix3DPlusExample();

            JSProxy_GPS::Proxy(obj, &gpsFix);
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

    JavaScriptRunResult RunSlotJavaScript(const string &slotName, const string &script)
    {
        JavaScriptRunResult retVal;

        // look up slot context
        MsgUDD &msg = GetMsgBySlotName(slotName);

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
                LoadJavaScriptBindings(msg);

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

                retVal.msgStateStr = GetMsgStateAsString(msg);
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
        for (int i = 0; i < lineList.size() && retVal == false; ++i)
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


    /////////////////////////////////////////////////////////////////
    // JSON Utility Functions
    /////////////////////////////////////////////////////////////////

    static string SanitizeMsgDef(const string &jsonStr)
    {
        string retVal;

        vector<string> lineList = Split(jsonStr, "\n");

        string sep = "";
        for (int i = 0; i < lineList.size(); ++i)
        {
            string &line = lineList[i];

            if (line.size() >= 2)
            {
                if (line[0] == '/' && line[1] == '/')
                {
                    // ignore
                }
                else
                {
                    // strip trailing comma from last line thus far.
                    // if another is added, the comma will be added back.
                    line[line.size() - 1] = ' ';

                    retVal += sep + line;

                    sep = ",\n";
                }
            }
        }

        return retVal;
    }


    /////////////////////////////////////////////////////////////////
    // Message State Functions
    /////////////////////////////////////////////////////////////////

    MsgUDD &GetMsgBySlotName(string slotName)
    {
        MsgUDD &msg = msg_;

        // pull stored field def and configure
        string msgDef = GetMsgDef(slotName);

        Log("Configuring ", slotName);
        ConfigureUserDefinedMessageFromMsgDef(msg, msgDef);
        LogNL();
        
        return msg;
    }

    string GetMsgStateAsString(MsgUDD &msg)
    {
        string retVal;

        const vector<string> &fieldList = msg.GetFieldList();

        // first pass to figure out string lengths
        size_t maxLen = 0;
        size_t overhead = 9;
        for (const auto &fieldName : fieldList)
        {
            maxLen = max(maxLen, (fieldName.length() + overhead));
        }

        // second pass to format
        string sep = "";
        for (const auto &fieldName : fieldList)
        {
            double value = msg.Get(fieldName.c_str());

            string line;
            line += "msg.Get";
            line += fieldName;
            line += "()";
            line = StrUtl::PadRight(line, ' ', maxLen);

            line += " == ";

            // keep the value good looking
            if (value == (int)value)
            {
                line += to_string((int)value);
            }
            else
            {
                line += ToString(value, 3);
            }

            retVal += sep;
            retVal += line;

            sep = "\n";
        }

        return retVal;
    }


    /////////////////////////////////////////////////////////////////
    // Flash storage and retrieval
    /////////////////////////////////////////////////////////////////

    string GetMsgDef(const string &slotName)
    {
        string fileName = slotName + ".json";

        string retVal = FilesystemLittleFS::Read(fileName);

        return retVal;
    }

    bool SetMsgDef(const string &slotName, const string &msgDef)
    {
        bool retVal = false;

        string fileName = slotName + ".json";

        FilesystemLittleFS::Remove(fileName);
        retVal = FilesystemLittleFS::Write(fileName, msgDef);

        return retVal;
    }

    string GetJavaScript(const string &slotName)
    {
        string fileName = slotName + ".js";

        string retVal = FilesystemLittleFS::Read(fileName);

        return retVal;
    }
    
    bool SetJavaScript(const string &slotName, const string &script)
    {
        bool retVal = false;

        string fileName = slotName + ".js";

        FilesystemLittleFS::Remove(fileName);
        retVal = FilesystemLittleFS::Write(fileName, script);

        return retVal;
    }


    /////////////////////////////////////////////////////////////////
    // Shell and JSON setup
    /////////////////////////////////////////////////////////////////

    void SetupShell()
    {
        Shell::AddCommand("app.ss.ud.run", [&](vector<string> argList){
            string slotName = string{"slot"} + argList[0];
            string script = GetJavaScript(slotName);

            RunSlotJavaScript(slotName, script);
        }, { .argCount = 1, .help = "run <slotNum> js"});
    }

    void SetupJSON()
    {
        JSONMsgRouter::RegisterHandler("REQ_GET_MSG_DEF", [this](auto &in, auto &out){
            string name = (const char *)in["name"];

            Log("REQ_GET_MSG_DEF for ", name);

            string msgDef = GetMsgDef(name);

            out["type"]   = "REP_GET_MSG_DEF";
            out["name"]   = name;
            out["msgDef"] = msgDef;
        });

        JSONMsgRouter::RegisterHandler("REQ_SET_MSG_DEF", [this](auto &in, auto &out){
            string name   = (const char *)in["name"];
            string msgDef = (const char *)in["msgDef"];

            Log("REQ_SET_MSG_DEF for ", name);
            // Log(msgDef);

            bool ok = false;
            MsgUDD &msg = GetMsgBySlotName(name);
            if (SetMsgDef(name, msgDef))
            {
                ok = ConfigureUserDefinedMessageFromMsgDef(msg, msgDef);
            }

            out["type"] = "REP_SET_MSG_DEF";
            out["name"] = name;
            out["ok"]   = ok;
        });

        JSONMsgRouter::RegisterHandler("REQ_GET_JS", [this](auto &in, auto &out){
            string name = (const char *)in["name"];

            Log("REQ_GET_JS for ", name);

            string script = GetJavaScript(name);

            out["type"]   = "REP_GET_JS";
            out["name"]   = name;
            out["script"] = script;
        });

        JSONMsgRouter::RegisterHandler("REQ_SET_JS", [this](auto &in, auto &out){
            string name   = (const char *)in["name"];
            string script = (const char *)in["script"];

            Log("REQ_SET_JS for ", name);
            // Log(script);

            bool ok = SetJavaScript(name, script);

            out["type"] = "REP_SET_JS";
            out["name"] = name;
            out["ok"]   = ok;
        });

        JSONMsgRouter::RegisterHandler("REQ_PARSE_JS", [this](auto &in, auto &out){
            string name   = (const char *)in["name"];
            string script = (const char *)in["script"];

            Log("Parsing script for ", name);
            // Log(script);

            string err;
            JerryScript::UseVM([&]{
                uint64_t timeStart = PAL.Millis();
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

            JavaScriptRunResult result = RunSlotJavaScript(name, script);

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

    inline static MsgUDD msg_;
};
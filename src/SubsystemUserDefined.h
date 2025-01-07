#pragma once

#include <algorithm>
using namespace std;

#include "FilesystemLittleFS.h"
#include "JerryScriptIntegration.h"
#include "JSON.h"
#include "JSONMsgRouter.h"
#include "JSObj_ADC.h"
#include "JSObj_BH1750.h"
#include "JSObj_BME280.h"
#include "JSObj_BMP280.h"
#include "JSObj_I2C.h"
#include "JSObj_MMC56x3.h"
#include "JSObj_Pin.h"
#include "JSProxy_GPS.h"
#include "JSProxy_WsprMessageTelemetryExtendedUserDefined.h"
#include "Shell.h"
#include "TempSensorInternal.h"
#include "Utl.h"
#include "WsprEncoded.h"


class SubsystemUserDefined
{
    using MsgUD = WsprMessageTelemetryExtendedUserDefined<29>;

    struct MsgState
    {
        MsgUD msg;
        vector<string> fieldList;
    };


public:

    SubsystemUserDefined()
    {
        SetupShell();
        SetupJSON();
        SetupUserDefinedMessages();
        SetupJavaScript();
        LogNL();
    }


private:

    /////////////////////////////////////////////////////////////////
    // Application Logic
    /////////////////////////////////////////////////////////////////

    void SetupUserDefinedMessages()
    {
        for (uint8_t slot = 0; slot < 5; ++slot)
        {
            string slotName = "slot" + to_string(slot);

            MsgState *msgState = GetMsgStateBySlotName(slotName);
            if (msgState)
            {
                // one-time pre-allocate state memory to avoid reallocation later on.
                //
                // previously, as fields were added to the vector, the vector
                // would grow and reallocate, causing some of the stored strings
                // to be re-created, invalidating the pointers held by the message
                // and ultimately messing things up.
                msgState->fieldList.reserve(29);

                // pull stored field def and configure
                string fieldDef = GetFieldDef(slotName);

                Log("Configuring ", slotName);
                ConfigureUserDefinedMessageFromFieldDef(*msgState, fieldDef);
                LogNL();
                Log("Message state:");
                Log(GetMsgStateAsString(*msgState));
                LogNL();
                LogNL();
            }
        }
    }

    void SetupJavaScript()
    {
        // get a baseline reading of how much resource usage the VM and bindings
        // takes so that users can be told how much of the remaining capacity
        // their script uses
        auto retVal = RunSlotJavaScript("slot0", "");

        runMemUsedBaseline_ = retVal.runMemUsed;

        int pctUse = runMemUsedBaseline_ * 100 / retVal.runMemAvail;
        int pctAvail = 100 - pctUse;
        Log("JavaScript Baseline Resource Allocation:");
        Log("- Pct Heap Use : ", pctUse,   " % (", Commas(runMemUsedBaseline_),                      " / ", Commas(retVal.runMemAvail), ")");
        Log("- Pct Heap Free: ", pctAvail, " % (", Commas(retVal.runMemAvail - runMemUsedBaseline_), " / ", Commas(retVal.runMemAvail), ")");
    }

    static bool ConfigureUserDefinedMessageFromFieldDef(MsgState &msgState, const string &fieldDef)
    {
        bool retVal = false;

        MsgUD          &msg       = msgState.msg;
        vector<string> &fieldList = msgState.fieldList;

        msg.ResetEverything();
        fieldList.clear();

        string jsonStr;
        jsonStr += "{ \"fieldDefList\": [";
        jsonStr += "\n";
        jsonStr += SanitizeFieldDef(fieldDef);
        jsonStr += "\n";
        jsonStr += "] }";

        Log("JSON:");
        Log(jsonStr);

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

                    fieldList.push_back(name + unit);
                    const string &fieldName = fieldList[fieldList.size() - 1];

                    Log("Defining ", fieldName);
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

        return retVal;
    }


    /////////////////////////////////////////////////////////////////
    // Javascript Execution Functions
    /////////////////////////////////////////////////////////////////

    // assumes the VM is running
    static void LoadJavaScriptBindings(MsgUD *msg)
    {
        // UserDefined Message API
        JerryScript::UseThenFreeNewObj([&](auto obj){
            JerryScript::SetGlobalPropertyNoFree("msg", obj);

            JSProxy_WsprMessageTelemetryExtendedUserDefined::Proxy(obj, msg);
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

            JerryScript::SetPropertyToBareFunction(obj, "GetTemperatureFahrenheit", []{
                return TempSensorInternal::GetTempF();
            });
            JerryScript::SetPropertyToBareFunction(obj, "GetTemperatureCelsius", []{
                return TempSensorInternal::GetTempC();
            });
            JerryScript::SetPropertyToBareFunction(obj, "GetInputVoltageVolts", []{
                return (double)ADC::GetMilliVoltsVCC() / 1'000;
            });
        });

        // Basic Functions API
        JerryScript::SetGlobalPropertyToBareFunction("DelayMs", [](uint32_t arg){
            PAL.Delay(arg);
        });

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
    }

    struct JavaScriptRunResult
    {
        bool     parseOk  = false;
        string   parseErr = "[Did not parse]";
        uint64_t parseMs  = 0;

        bool     runOk       = false;
        string   runErr      = "[Did not run]";
        uint64_t runMs       = 0;
        uint32_t runMemAvail = 0;
        uint32_t runMemUsed  = 0;
        string   runOutput;

        string  msgStateStr;
    };

    JavaScriptRunResult RunSlotJavaScript(const string &slotName, const string &script)
    {
        JavaScriptRunResult retVal;

        // look up slot context
        MsgState *msgState = GetMsgStateBySlotName(slotName);
        if (msgState)
        {
            Log("Running script");
            JerryScript::UseVM([&]{
                // parse to detect errors
                retVal.parseErr = JerryScript::ParseScript(script);
                retVal.parseOk  = retVal.parseErr == "";
                retVal.parseMs  = JerryScript::GetScriptParseDurationMs();

                if (retVal.parseOk)
                {
                    // reset message values to default
                    msgState->msg.Reset();

                    // load javascript integrations
                    LoadJavaScriptBindings(&msgState->msg);

                    // run it
                    retVal.runErr = JerryScript::ParseAndRunScript(script);

                    // capture result of run
                    retVal.runOk     = retVal.runErr == "";
                    retVal.runMs     = JerryScript::GetScriptRunDurationMs();
                    retVal.runOutput = JerryScript::GetScriptOutput();

                    retVal.msgStateStr = GetMsgStateAsString(*msgState);
                }
            });

            // capture memory utilization stats
            retVal.runMemAvail = JerryScript::GetHeapCapacity();
            retVal.runMemUsed  = JerryScript::GetHeapSizeMax();

            Log("ParseOk: ", retVal.parseOk, ", ", retVal.parseMs, " ms");
            if (retVal.parseOk)
            {
                int pct = retVal.runMemUsed * 100 / retVal.runMemAvail;
                Log("RunOk  : ", retVal.runOk, ", ", retVal.runMs, " ms, ", pct, " % heap used (", Commas(retVal.runMemUsed), " / ", Commas(retVal.runMemAvail), ")");
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
        }
        else
        {
            Log("ERR: Invalid slot name: ", slotName);
        }

        return retVal;
    }


    /////////////////////////////////////////////////////////////////
    // JSON Utility Functions
    /////////////////////////////////////////////////////////////////

    static string SanitizeFieldDef(const string &jsonStr)
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

    MsgState *GetMsgStateBySlotName(string slotName)
    {
        MsgState *msgState = nullptr;

             if (slotName == "slot0") { msgState = &msgStateSlot0_; }
        else if (slotName == "slot1") { msgState = &msgStateSlot1_; }
        else if (slotName == "slot2") { msgState = &msgStateSlot2_; }
        else if (slotName == "slot3") { msgState = &msgStateSlot3_; }
        else if (slotName == "slot4") { msgState = &msgStateSlot4_; }
        
        return msgState;
    }

    string GetMsgStateAsString(MsgState &msgState)
    {
        string retVal;

        MsgUD          &msg       = msgState.msg;
        vector<string> &fieldList = msgState.fieldList;

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

    string GetFieldDef(const string &slotName)
    {
        string fileName = slotName + ".json";

        string retVal = FilesystemLittleFS::Read(fileName);

        return retVal;
    }

    bool SetFieldDef(const string &slotName, const string &fieldDef)
    {
        bool retVal = false;

        string fileName = slotName + ".json";

        FilesystemLittleFS::Remove(fileName);
        retVal = FilesystemLittleFS::Write(fileName, fieldDef);

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
        JSONMsgRouter::RegisterHandler("REQ_GET_FIELD_DEF", [this](auto &in, auto &out){
            string name = (const char *)in["name"];

            Log("REQ_GET_FIELD_DEF for ", name);

            string fieldDef = GetFieldDef(name);

            out["type"]     = "REP_GET_FIELD_DEF";
            out["name"]     = name;
            out["fieldDef"] = fieldDef;
        });

        JSONMsgRouter::RegisterHandler("REQ_SET_FIELD_DEF", [this](auto &in, auto &out){
            string name     = (const char *)in["name"];
            string fieldDef = (const char *)in["fieldDef"];

            Log("REQ_SET_FIELD_DEF for ", name);
            // Log(fieldDef);

            bool ok = false;
            MsgState *msgState = GetMsgStateBySlotName(name);
            if (msgState)
            {
                if (SetFieldDef(name, fieldDef))
                {
                    ok = ConfigureUserDefinedMessageFromFieldDef(*msgState, fieldDef);
                }
            }

            out["type"] = "REP_SET_FIELD_DEF";
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
            // actual capacity of the system
            uint32_t runMemUsed  = result.runMemUsed - runMemUsedBaseline_;
            uint32_t runMemAvail = result.runMemAvail - runMemUsedBaseline_;

            int pctUse = runMemUsed * 100 / runMemAvail;
            Log("User Heap: ", pctUse, " % (", Commas(runMemUsed), " / ", Commas(runMemAvail), ")");

            out["type"]        = "REP_RUN_JS";
            out["name"]        = name;
            out["parseOk"]     = result.parseOk;
            out["parseErr"]    = result.parseErr;
            out["parseMs"]     = result.parseMs;
            out["runOk"]       = result.runOk;
            out["runErr"]      = result.runErr;
            out["runMs"]       = result.runMs;
            out["runMemAvail"] = runMemAvail;
            out["runMemUsed"]  = runMemUsed;
            out["runOutput"]   = result.runOutput;
            out["msgState"]    = result.msgStateStr;
        });
    }


private:

    uint32_t runMemUsedBaseline_ = 0;

    // keep memory off of the main stack to avoid needing to
    // size the stack itself to a large size that every app
    // would have to tune
    inline static MsgState msgStateSlot0_;
    inline static MsgState msgStateSlot1_;
    inline static MsgState msgStateSlot2_;
    inline static MsgState msgStateSlot3_;
    inline static MsgState msgStateSlot4_;
};
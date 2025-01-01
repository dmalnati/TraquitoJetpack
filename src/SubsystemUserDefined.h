#pragma once

#include "FilesystemLittleFS.h"
#include "JerryScriptIntegration.h"
#include "JSON.h"
#include "JSONMsgRouter.h"
#include "Shell.h"
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
    }

private:

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

    bool SetAndApplyFieldDef(const string &slotName, const string &fieldDef)
    {
        bool retVal = SetFieldDef(slotName, fieldDef);

        if (retVal)
        {
            MsgState *msgState = GetMsgStateBySlotName(slotName);
            if (msgState)
            {
                ConfigureUserDefinedMessageFromFieldDef(*msgState, fieldDef);
            }
            else
            {
                retVal = false;
            }
        }

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

    string GetMessageStateString(MsgState &msgState)
    {
        string retVal;

        MsgUD          &msg       = msgState.msg;
        vector<string> &fieldList = msgState.fieldList;

        string sep = "";
        for (const auto &fieldName : fieldList)
        {
            double value = msg.Get(fieldName.c_str());

            retVal += sep;
            retVal += "msg.Get";
            retVal += fieldName;
            retVal += "() = ";
            retVal += to_string(value);

            sep = "\n";
        }

        Log("Doing ShowMessage while doing string state");
        ShowMessage(msg);

        return retVal;
    }

    void ShowMessage(MsgUD &msg)
    {
        auto     &fieldDefList    = msg.GetFieldDefList();
        uint16_t  fieldDefListLen = msg.GetFieldDefListLen();

        Log("Field count: ", fieldDefListLen);

        for (int i = 0; i < fieldDefListLen; ++i)
        {
            const auto &fieldDef = fieldDefList[i];

            Log(fieldDef.name, "(", fieldDef.value, "): ", fieldDef.lowValue, ", ", fieldDef.highValue, ", ", fieldDef.stepSize);
        }
    }

    void ShowJavaScript(uint8_t slot)
    {
        string fileName = string{"slot"} + to_string(slot) + ".js";

        string script = FilesystemLittleFS::Read(fileName);

        Log("Script:");
        Log(script);
    }

    void SetupShell()
    {
        Shell::AddCommand("app.ss.ud.show", [this](vector<string> argList){
            vector<MsgState *> msgStateList = {
                &msgStateSlot0_,
                &msgStateSlot1_,
                &msgStateSlot2_,
                &msgStateSlot3_,
                &msgStateSlot4_,
            };

            string sep;
            for (int i = 0; auto msgState : msgStateList)
            {
                LogNNL(sep);
                Log("Slot", i, ":");
                ShowMessage(msgState->msg);

                LogNL();

                ShowJavaScript(i);

                LogNL();

                sep = "\n";

                ++i;
            }
        }, { .argCount = 0, .help = "subsystem user defined show"});
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

            bool ok = SetAndApplyFieldDef(name, fieldDef);

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

            bool ok = err == "";

            out["type"]         = "REP_PARSE_JS";
            out["name"]         = name;
            out["ok"]           = ok;
            out["err"]          = err;
            out["parseMs"]      = JerryScript::GetScriptParseDurationMs();
            out["vmOverheadMs"] = JerryScript::GetVMOverheadDurationMs();
        });

        JSONMsgRouter::RegisterHandler("REQ_RUN_JS", [this](auto &in, auto &out){
            string name = (const char *)in["name"];

            Log("REQ_RUN_JS - ", name);

            // values to capture
            bool     parseOk = false;
            string   parseErr = "[Did not parse]";
            uint64_t parseMs = 0;

            bool     runOk = false;
            string   runErr = "[Did not run]";
            uint64_t runMs = 0;
            string   runOutput;

            string  msgStateStr;

            // look up slot context
            MsgState *msgState = GetMsgStateBySlotName(name);
            if (msgState)
            {
                string script = (const char *)in["script"];

                // run
                Log("Running script");
                JerryScript::UseVM([&]{
                    parseErr = JerryScript::ParseScript(script);
                    parseOk  = parseErr == "";
                    parseMs  = JerryScript::GetScriptParseDurationMs();

                    if (parseOk)
                    {
                        runErr    = JerryScript::ParseAndRunScript(script);
                        runOk     = runErr == "";
                        runMs     = JerryScript::GetScriptRunDurationMs();
                        runOutput = JerryScript::GetScriptOutput();

                        msgStateStr = GetMessageStateString(*msgState);
                    }
                });

                Log("Return: parseOk: ", parseOk, ", runOk: ", runOk);
                Log("Script output:");
                Log(runOutput);
                Log("Message state:");
                Log(msgStateStr);
            }
            else
            {
                Log("ERR: Invalid slot name: ", name);
            }

            // return
            out["type"]       = "REP_RUN_JS";
            out["name"]       = name;
            out["parseOk"]    = parseOk;
            out["parseErr"]   = parseErr;
            out["parseMs"]    = parseMs;
            out["runOk"]      = runOk;
            out["runErr"]     = runErr;
            out["runMs"]      = runMs;
            out["runOutput"]  = runOutput;
            out["msgState"]   = msgStateStr;
        });
    }

    void SetupUserDefinedMessages()
    {
        for (uint8_t slot = 0; slot < 5; ++slot)
        {
            string slotName = "slot" + to_string(slot);

            MsgState *msgState = GetMsgStateBySlotName(slotName);
            if (msgState)
            {
                string fieldDef = GetFieldDef(slotName);

                Log("Configuring slot ", slotName);
                ConfigureUserDefinedMessageFromFieldDef(*msgState, fieldDef);
            }
        }
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
        Log("ToJSON");
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

                    string line;
                    line += "name: " + name + ", " + "unit: " + unit + ", " + "lowValue: " + to_string(lowValue) + ", " + "highValue: " + to_string(highValue) + ", " + "stepSize: " + to_string(stepSize) + ", ";
                    Log(line);

                    fieldList.push_back(name + unit);
                    const string &fieldName = fieldList[fieldList.size() - 1];

                    if (msg.DefineField(fieldName.c_str(), lowValue, highValue, stepSize) == false)
                    {
                        retVal = false;

                        Log("Failed to define field:");
                        Log("- field: ", fieldName);
                        Log("- line : ", line);
                        Log("- err  : ", msg.GetDefineFieldErr());
                    }
                    else
                    {
                        Log("Succeeded in defining field ", fieldName);
                        Log("Value: ", msg.Get(fieldName.c_str()));
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
                    if (i == lineList.size() - 1)
                    {
                        // strip trailing comma from last line
                        line[line.size() - 1] = ' ';
                    }

                    retVal += sep + line;

                    sep = "\n";
                }
            }
        }

        return retVal;
    }



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


private:


    inline static MsgState msgStateSlot0_;
    inline static MsgState msgStateSlot1_;
    inline static MsgState msgStateSlot2_;
    inline static MsgState msgStateSlot3_;
    inline static MsgState msgStateSlot4_;

};
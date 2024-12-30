#pragma once

#include "FilesystemLittleFS.h"
#include "JerryScriptIntegration.h"
#include "JSONMsgRouter.h"
#include "Shell.h"
#include "WsprEncoded.h"


class SubsystemUserDefined
{
    using MsgUD = WsprMessageTelemetryExtendedUserDefined<29>;

public:

    SubsystemUserDefined()
    {
        SetupSlots();
        SetupShell();
        SetupJSON();
    }

private:

    void SetupSlots()
    {
        for (int i = 0; i < 5; ++i)
        {
            string fileName = string{"slot"} + to_string(i) + ".js";

            if (FilesystemLittleFS::FileExists(fileName) == false)
            {
                auto f = FilesystemLittleFS::GetFile(fileName);

                if (f.Open())
                {
                    string data;
                    data += "print(`I am slot";
                    data += to_string(i);
                    data += "`);";

                    f.Write(data);

                    f.Close();
                }
            }
        }
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
            vector<MsgUD *> msgList = {
                &msgSlot0_,
                &msgSlot1_,
                &msgSlot2_,
                &msgSlot3_,
                &msgSlot4_,
            };

            string sep;
            for (int i = 0; auto msg : msgList)
            {
                LogNNL(sep);
                Log("Slot", i, ":");
                ShowMessage(*msg);

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
        JSONMsgRouter::RegisterHandler("REQ_JS_PARSE", [this](auto &in, auto &out){
            string script = (const char *)in["script"];

            Log("Parsing script");
            Log(script);

            uint64_t timeDiffMs = 0;
            string ret;
            JerryScript::UseVM([&]{
                uint64_t timeStart = PAL.Millis();
                ret = JerryScript::ParseScript(script);
                timeDiffMs = PAL.Millis() - timeStart;
            });

            Log("Return: ", ret);

            bool ok = ret == "";

            out["type"] = "REP_JS_PARSE";
            out["ok"]   = ok;
            out["err"]  = ret;
            out["parseMs"] = timeDiffMs;
        });
    }

private:

    inline static MsgUD msgSlot0_;
    inline static MsgUD msgSlot1_;
    inline static MsgUD msgSlot2_;
    inline static MsgUD msgSlot3_;
    inline static MsgUD msgSlot4_;
};
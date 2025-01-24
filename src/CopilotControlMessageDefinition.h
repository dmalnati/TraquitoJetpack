#pragma once

#include "CopilotControlConfiguration.h"
#include "CopilotControlUtl.h"
#include "JSON.h"
#include "Log.h"
#include "Utl.h"
#include "WsprEncodedDynamic.h"

#include <string>
#include <vector>
using namespace std;


class CopilotControlMessageDefinition
{
public:

    static bool SlotHasMsgDef(string slotName)
    {
        MsgUD &msg = GetMsgResetAndConfigureBySlotName(slotName);

        return msg.GetFieldList().size();
    }

    static MsgUD &GetMsgLastConfigured()
    {
        return msg_;
    }

    static MsgUD &GetMsgResetAndConfigureBySlotName(string slotName)
    {
        MsgUD &msg = msg_;

        // pull stored field def and configure
        string msgDef = CopilotControlConfiguration::GetMsgDef(slotName);

        ConfigureMsgFromMsgDef(msg, msgDef, slotName);
        
        return msg;
    }


private:

    // 20ms at 48MHz with 29 fields (ie don't worry about it)
    static bool ConfigureMsgFromMsgDef(MsgUD &msg, const string &msgDef, const string &title)
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
            Log("ERR: ", title);
            Log("JSON:");
            Log(jsonStr);
            LogNL();
        }

        return retVal;
    }

    static string SanitizeMsgDef(const string &jsonStr)
    {
        string retVal;

        vector<string> lineList = Split(jsonStr, "\n");

        string sep = "";
        for (size_t i = 0; i < lineList.size(); ++i)
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


private:

    inline static MsgUD msg_;
};
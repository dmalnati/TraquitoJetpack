#pragma once

#include "CopilotControlConfiguration.h"
#include "JSON.h"
#include "Log.h"
#include "Utl.h"
#include "WsprMessageTelemetryExtendedUserDefinedDynamic.h"

#include <string>
#include <vector>
using namespace std;


class CopilotControlMessageDefinition
{
    using MsgUDD = WsprMessageTelemetryExtendedUserDefinedDynamic<29>;

public:

    MsgUDD &GetMsgBySlotName(string slotName)
    {
        MsgUDD &msg = msg_;

        // pull stored field def and configure
        string msgDef = CopilotControlConfiguration::GetMsgDef(slotName);

        Log("Configuring ", slotName);
        ConfigureMsgFromMsgDef(msg, msgDef);
        LogNL();
        
        return msg;
    }

    static string GetMsgStateAsString(MsgUDD &msg)
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


private:

    // 20ms at 48MHz with 29 fields (ie don't worry about it)
    static bool ConfigureMsgFromMsgDef(MsgUDD &msg, const string &msgDef)
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


private:

    inline static MsgUDD msg_;
};
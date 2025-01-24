#pragma once

#include "Utl.h"
#include "WsprEncodedDynamic.h"

#include <string>
using namespace std;

using MsgUD = WsprMessageTelemetryExtendedUserDefined<29>;


class CopilotControlUtl
{
public:

    static string GetMsgStateAsString(MsgUD &msg)
    {
        static MsgUD msgDecodedValues;
        msgDecodedValues = msg;
        msgDecodedValues.Encode();
        msgDecodedValues.Decode();

        const vector<string> &fieldList = msg.GetFieldList();

        // first pass to figure out field lengths
        size_t maxLen = 0;
        size_t overhead = 9;
        for (const auto &fieldName : fieldList)
        {
            maxLen = max(maxLen, (fieldName.length() + overhead));
        }

        // second pass to format and add current values
        vector<string> lineList;
        size_t maxLine = 0;
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

            lineList.push_back(line);

            maxLine = max(maxLine, line.length());
        }

        // third pass to add decoded values
        for (size_t i = 0; i < lineList.size(); ++i)
        {
            string &line = lineList[i];
            line = StrUtl::PadRight(line, ' ', maxLine);

            line += " (decodes as ";

            double value = msgDecodedValues.Get(fieldList[i].c_str());

            if (value == (int)value)
            {
                line += to_string((int)value);
            }
            else
            {
                line += ToString(value, 3);
            }

            line += ")";
        }

        // assemble
        string retVal;
        string sep = "";
        for (const auto &line : lineList)
        {
            retVal += sep;
            retVal += line;
            sep = "\n";
        }

        return retVal;
    }
};
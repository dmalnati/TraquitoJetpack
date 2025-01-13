#pragma once

#include "FilesystemLittleFS.h"
#include "JSONMsgRouter.h"
#include "Log.h"
#include "Shell.h"

#include <string>
using namespace std;


class CopilotControlConfiguration
{
public:


    /////////////////////////////////////////////////////////////////
    // Flash storage and retrieval
    /////////////////////////////////////////////////////////////////

    static string GetMsgDef(const string &slotName)
    {
        string fileName = slotName + ".json";

        string retVal = FilesystemLittleFS::Read(fileName);

        return retVal;
    }

    static bool SetMsgDef(const string &slotName, const string &msgDef)
    {
        bool retVal = false;

        string fileName = slotName + ".json";
        retVal = FilesystemLittleFS::Write(fileName, msgDef);

        return retVal;
    }

    static string GetJavaScript(const string &slotName)
    {
        string fileName = slotName + ".js";

        string retVal = FilesystemLittleFS::Read(fileName);

        return retVal;
    }
    
    static bool SetJavaScript(const string &slotName, const string &script)
    {
        bool retVal = false;

        string fileName = slotName + ".js";
        retVal = FilesystemLittleFS::Write(fileName, script);

        return retVal;
    }


    /////////////////////////////////////////////////////////////////
    // Shell and JSON setup
    /////////////////////////////////////////////////////////////////

    static void SetupShell()
    {

    }

    static void SetupJSON()
    {
        JSONMsgRouter::RegisterHandler("REQ_GET_MSG_DEF", [](auto &in, auto &out){
            string name = (const char *)in["name"];

            Log("REQ_GET_MSG_DEF for ", name);

            string msgDef = GetMsgDef(name);

            out["type"]   = "REP_GET_MSG_DEF";
            out["name"]   = name;
            out["msgDef"] = msgDef;
        });

        JSONMsgRouter::RegisterHandler("REQ_SET_MSG_DEF", [](auto &in, auto &out){
            string name   = (const char *)in["name"];
            string msgDef = (const char *)in["msgDef"];

            Log("REQ_SET_MSG_DEF for ", name);

            bool ok = SetMsgDef(name, msgDef);

            out["type"] = "REP_SET_MSG_DEF";
            out["name"] = name;
            out["ok"]   = ok;
        });

        JSONMsgRouter::RegisterHandler("REQ_GET_JS", [](auto &in, auto &out){
            string name = (const char *)in["name"];

            Log("REQ_GET_JS for ", name);

            string script = GetJavaScript(name);

            out["type"]   = "REP_GET_JS";
            out["name"]   = name;
            out["script"] = script;
        });

        JSONMsgRouter::RegisterHandler("REQ_SET_JS", [](auto &in, auto &out){
            string name   = (const char *)in["name"];
            string script = (const char *)in["script"];

            Log("REQ_SET_JS for ", name);

            bool ok = SetJavaScript(name, script);

            out["type"] = "REP_SET_JS";
            out["name"] = name;
            out["ok"]   = ok;
        });
    }
};
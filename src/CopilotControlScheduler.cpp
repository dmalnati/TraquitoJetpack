#include "CopilotControlScheduler.h"
#include "Utl.h"

#include <source_location>
using namespace std;

#include "StrictMode.h"


// set up test case primitives
static string msgDefBlank = "";
static string msgDefSet   = "{ \"name\": \"Altitude\", \"unit\": \"Meters\", \"lowValue\": 0, \"highValue\": 21340, \"stepSize\": 20 },";

static string jsUsesNeither = "";
static string jsUsesGps     = "gps.GetAltitudeMeters();";
static string jsUsesMsg     = "msg.SetAltitudeMeters(1);";
static string jsUsesBoth    = jsUsesGps + jsUsesMsg;

static string jsBad            = "1x;";
static string jsUsesNeitherBad = jsUsesNeither + jsBad;
static string jsUsesGpsBad     = jsUsesGps     + jsBad;
static string jsUsesMsgBad     = jsUsesMsg     + jsBad;
static string jsUsesBothBad    = jsUsesBoth    + jsBad;

static auto SetSlot = [](const string &slotName, const string &msgDef, const string &js){
    FilesystemLittleFS::Write(slotName + ".json", msgDef);
    FilesystemLittleFS::Write(slotName + ".js", js);
};











void CopilotControlScheduler::TestConfigureWindowSlotBehavior()
{
    BackupFiles();

    // set up checking outcome
    auto Assert = [](string slotName, const SlotBehavior &slotBehavior, bool runJs, string msgSend, bool canSendDefault){
        bool retVal = true;

        if (slotBehavior.runJs != runJs || slotBehavior.msgSend != msgSend || slotBehavior.canSendDefault != canSendDefault)
        {
            retVal = false;

            Log("Assert ERR: ", slotName);

            if (slotBehavior.runJs != runJs)
            {
                Log("- runJs expected(", runJs, ") != actual(", slotBehavior.runJs, ")");
            }

            if (slotBehavior.msgSend != msgSend)
            {
                Log("- msgSend expected(", msgSend, ") != actual(", slotBehavior.msgSend, ")");
            }

            if (slotBehavior.canSendDefault != canSendDefault)
            {
                Log("- canSendDefault expected(", canSendDefault, ") != actual(", slotBehavior.canSendDefault, ")");
            }
        }

        return retVal;
    };


    // run test cases
    LogNL();

    bool ok = true;

    {
        Log("=== Testing GPS = False, w/ Msg Def ===");
        LogNL();

        bool haveGpsLock = false;
        SetSlot("slot1", msgDefSet, jsUsesNeither);
        SetSlot("slot2", msgDefSet, jsUsesMsg);
        SetSlot("slot3", msgDefSet, jsUsesGps);
        SetSlot("slot4", msgDefSet, jsUsesBoth);

        PrepareWindowSlotBehavior(haveGpsLock);

        bool testsOk = true;
        testsOk &= Assert("slot1", slotState1_.slotBehavior, true,  "none",   haveGpsLock);
        testsOk &= Assert("slot2", slotState2_.slotBehavior, true,  "custom", haveGpsLock);
        testsOk &= Assert("slot3", slotState3_.slotBehavior, false, "none",   haveGpsLock);
        testsOk &= Assert("slot4", slotState4_.slotBehavior, false, "none",   haveGpsLock);

        ok &= testsOk;

        Log("=== Tests ", testsOk ? "" : "NOT ", "ok ===");
        LogNL();
    }

    {
        Log("=== Testing GPS = False, w/ no Msg Def ===");
        LogNL();

        bool haveGpsLock = false;
        SetSlot("slot1", msgDefBlank, jsUsesNeither);
        SetSlot("slot2", msgDefBlank, jsUsesMsg);
        SetSlot("slot3", msgDefBlank, jsUsesGps);
        SetSlot("slot4", msgDefBlank, jsUsesBoth);

        PrepareWindowSlotBehavior(haveGpsLock);

        bool testsOk = true;
        testsOk &= Assert("slot1", slotState1_.slotBehavior, true,  "none", haveGpsLock);
        testsOk &= Assert("slot2", slotState2_.slotBehavior, true,  "none", haveGpsLock);
        testsOk &= Assert("slot3", slotState3_.slotBehavior, false, "none", haveGpsLock);
        testsOk &= Assert("slot4", slotState4_.slotBehavior, false, "none", haveGpsLock);

        ok &= testsOk;

        Log("=== Tests ", testsOk ? "" : "NOT ", "ok ===");
        LogNL();
    }

    {
        Log("=== Testing GPS = True, w/ Msg Def ===");
        LogNL();

        bool haveGpsLock = true;
        SetSlot("slot1", msgDefSet, jsUsesNeither);
        SetSlot("slot2", msgDefSet, jsUsesMsg);
        SetSlot("slot3", msgDefSet, jsUsesGps);
        SetSlot("slot4", msgDefSet, jsUsesBoth);

        PrepareWindowSlotBehavior(haveGpsLock);

        bool testsOk = true;
        testsOk &= Assert("slot1", slotState1_.slotBehavior, true, "default", haveGpsLock);
        testsOk &= Assert("slot2", slotState2_.slotBehavior, true, "custom",  haveGpsLock);
        // here it's "none" because there is no actual default function, but if there was
        // a default function, it would fire here
        testsOk &= Assert("slot3", slotState3_.slotBehavior, true, "none",    haveGpsLock);
        testsOk &= Assert("slot4", slotState4_.slotBehavior, true, "custom",  haveGpsLock);

        ok &= testsOk;

        Log("=== Tests ", testsOk ? "" : "NOT ", "ok ===");
        LogNL();
    }

    {
        Log("=== Testing GPS = True, w/ no Msg Def ===");
        LogNL();

        bool haveGpsLock = true;
        SetSlot("slot1", msgDefBlank, jsUsesNeither);
        SetSlot("slot2", msgDefBlank, jsUsesMsg);
        SetSlot("slot3", msgDefBlank, jsUsesGps);
        SetSlot("slot4", msgDefBlank, jsUsesBoth);

        PrepareWindowSlotBehavior(haveGpsLock);

        bool testsOk = true;
        testsOk &= Assert("slot1", slotState1_.slotBehavior, true, "default", haveGpsLock);
        testsOk &= Assert("slot2", slotState2_.slotBehavior, true, "default", haveGpsLock);
        testsOk &= Assert("slot3", slotState3_.slotBehavior, true, "none",    haveGpsLock);
        testsOk &= Assert("slot4", slotState4_.slotBehavior, true, "none",    haveGpsLock);

        ok &= testsOk;

        Log("=== Tests ", testsOk ? "" : "NOT ", "ok ===");
        LogNL();
    }

    Log("=== ALL Tests ", ok ? "" : "NOT ", "ok ===");
    LogNL();

    RestoreFiles();
}

































// expected is a subset of actual, but all elements need to be found
// in the order expressed for it to be a match
static auto Assert = [](string title, vector<string> actualList, vector<string> expectedList){
    bool retVal = true;

    while (expectedList.size())
    {
        // pop first element from expected list
        string expected = *expectedList.begin();
        expectedList.erase(expectedList.begin());

        // remove non-matching elements from actual list
        while (actualList.size() && expected != *actualList.begin())
        {
            actualList.erase(actualList.begin());
        }

        // remove the element you just compared equal to (if exists)
        if (actualList.size())
        {
            actualList.erase(actualList.begin());
        }

        // if now empty, the compare is over
        if (actualList.size() == 0)
        {
            break;
        }
    }

    retVal = expectedList.size() == 0;

    if (retVal == false)
    {
        Log("Assert ERR: test", title);
        Log("Expected items remain:");
        for (const auto &str : expectedList)
        {
            Log("  ", str);
        }
    }

    return retVal;
};

static uint64_t durationMs = 0;
static auto NextTestDuration = []{
    uint64_t retVal = durationMs;
    
    // works at 125 MHz
    durationMs += 1500;

    return retVal;
};

static void ResetTestDuration()
{
    durationMs = 0;
}

static const uint64_t INNER_DELAY_MS = 50;

static auto IncrAndGetTestId = []{
    static int id = 0;
    ++id;
    return id;
};

static CopilotControlScheduler *scheduler = nullptr;


string JustFunctionName(string fnScoped)
{
    return Split(fnScoped, "::")[0];
}















///////////////////////////////////////////////////////////////////////////////
// Tests with good javascript
///////////////////////////////////////////////////////////////////////////////


// default behavior, with gps lock
void TestDefaultWithGps()
{
    static TimedEventHandlerDelegate tedTestOuter;
    tedTestOuter.SetCallback([]{
        static TimedEventHandlerDelegate tedTestInner;

        scheduler->SetTesting(true);
        int id = IncrAndGetTestId();
        scheduler->CreateMarkList(id);

        bool haveGpsLock = true;
        SetSlot("slot1", msgDefBlank, jsUsesNeither);
        SetSlot("slot2", msgDefBlank, jsUsesNeither);
        SetSlot("slot3", msgDefBlank, jsUsesNeither);
        SetSlot("slot4", msgDefBlank, jsUsesNeither);
        SetSlot("slot5", msgDefBlank, jsUsesNeither);
        scheduler->PrepareWindowSlotBehavior(haveGpsLock);
        scheduler->PrepareWindowSchedule(0);

        tedTestInner.SetCallback([id]{
            string title = JustFunctionName(source_location::current().function_name());

            scheduler->SetTesting(false);

            vector<string> expectedList = {
                "JS_EXEC",               "SEND_REGULAR_TYPE1",      // slot 1
                "JS_EXEC",               "SEND_BASIC_TELEMETRY",    // slot 2
                "JS_EXEC",                                          // slot 3 js
                "TX_DISABLE_GPS_ENABLE",
                                         "SEND_NO_MSG_NONE",        // slot 3 msg
                "JS_EXEC",               "SEND_NO_MSG_NONE",        // slot 4
                "JS_EXEC",               "SEND_NO_MSG_NONE",        // slot 5
            };

            bool testOk = Assert(title, scheduler->GetMarkList(), expectedList);
            scheduler->DestroyMarkList(id);

            LogNL();
            Log("=== Test ", title, " ", testOk ? "" : "NOT ", "ok ===");
            LogNL();
        });
        tedTestInner.RegisterForTimedEvent(INNER_DELAY_MS);
    });
    tedTestOuter.RegisterForTimedEvent(NextTestDuration());
}


// default behavior, with no gps lock
void TestDefaultNoGps()
{
    static TimedEventHandlerDelegate tedTestOuter;
    tedTestOuter.SetCallback([]{
        static TimedEventHandlerDelegate tedTestInner;

        scheduler->SetTesting(true);
        int id = IncrAndGetTestId();
        scheduler->CreateMarkList(id);

        bool haveGpsLock = false;
        SetSlot("slot1", msgDefBlank, jsUsesNeither);
        SetSlot("slot2", msgDefBlank, jsUsesNeither);
        SetSlot("slot3", msgDefBlank, jsUsesNeither);
        SetSlot("slot4", msgDefBlank, jsUsesNeither);
        SetSlot("slot5", msgDefBlank, jsUsesNeither);
        scheduler->PrepareWindowSlotBehavior(haveGpsLock);
        scheduler->PrepareWindowSchedule(0);

        tedTestInner.SetCallback([id]{
            string title = JustFunctionName(source_location::current().function_name());

            scheduler->SetTesting(false);

            vector<string> expectedList = {
                "JS_EXEC",                                      // slot 1 js
                "TX_DISABLE_GPS_ENABLE",
                                         "SEND_NO_MSG_NONE",    // slot 1 msg
                "JS_EXEC",               "SEND_NO_MSG_NONE",    // slot 2
                "JS_EXEC",               "SEND_NO_MSG_NONE",    // slot 3
                "JS_EXEC",               "SEND_NO_MSG_NONE",    // slot 4
                "JS_EXEC",               "SEND_NO_MSG_NONE",    // slot 5
            };

            bool testOk = Assert(title, scheduler->GetMarkList(), expectedList);
            scheduler->DestroyMarkList(id);

            LogNL();
            Log("=== Test ", title, " ", testOk ? "" : "NOT ", "ok ===");
            LogNL();
        });
        tedTestInner.RegisterForTimedEvent(INNER_DELAY_MS);
    });
    tedTestOuter.RegisterForTimedEvent(NextTestDuration());
}


// all override, with gps lock
void TestAllOverrideWithGps()
{
    static TimedEventHandlerDelegate tedTestOuter;
    tedTestOuter.SetCallback([]{
        static TimedEventHandlerDelegate tedTestInner;

        scheduler->SetTesting(true);
        int id = IncrAndGetTestId();
        scheduler->CreateMarkList(id);

        bool haveGpsLock = true;
        SetSlot("slot1", msgDefSet, jsUsesBoth);
        SetSlot("slot2", msgDefSet, jsUsesBoth);
        SetSlot("slot3", msgDefSet, jsUsesBoth);
        SetSlot("slot4", msgDefSet, jsUsesBoth);
        SetSlot("slot5", msgDefSet, jsUsesBoth);
        scheduler->PrepareWindowSlotBehavior(haveGpsLock);
        scheduler->PrepareWindowSchedule(0);

        tedTestInner.SetCallback([id]{
            string title = JustFunctionName(source_location::current().function_name());

            scheduler->SetTesting(false);

            vector<string> expectedList = {
                "JS_EXEC",               "SEND_CUSTOM_MESSAGE",    // slot 1
                "JS_EXEC",               "SEND_CUSTOM_MESSAGE",    // slot 2
                "JS_EXEC",               "SEND_CUSTOM_MESSAGE",    // slot 3
                "JS_EXEC",               "SEND_CUSTOM_MESSAGE",    // slot 4
                "JS_EXEC",               "SEND_CUSTOM_MESSAGE",    // slot 5
                "TX_DISABLE_GPS_ENABLE",
            };

            bool testOk = Assert(title, scheduler->GetMarkList(), expectedList);
            scheduler->DestroyMarkList(id);

            LogNL();
            Log("=== Test ", title, " ", testOk ? "" : "NOT ", "ok ===");
            LogNL();
        });
        tedTestInner.RegisterForTimedEvent(INNER_DELAY_MS);
    });
    tedTestOuter.RegisterForTimedEvent(NextTestDuration());
}


// all custom messages need gps, with no gps lock
void TestAllCustomMessagesNeedGpsWithNoGps()
{
    static TimedEventHandlerDelegate tedTestOuter;
    tedTestOuter.SetCallback([]{
        static TimedEventHandlerDelegate tedTestInner;

        scheduler->SetTesting(true);
        int id = IncrAndGetTestId();
        scheduler->CreateMarkList(id);

        bool haveGpsLock = false;
        SetSlot("slot1", msgDefSet, jsUsesBoth);
        SetSlot("slot2", msgDefSet, jsUsesBoth);
        SetSlot("slot3", msgDefSet, jsUsesBoth);
        SetSlot("slot4", msgDefSet, jsUsesBoth);
        SetSlot("slot5", msgDefSet, jsUsesBoth);
        scheduler->PrepareWindowSlotBehavior(haveGpsLock);
        scheduler->PrepareWindowSchedule(0);

        tedTestInner.SetCallback([id]{
            string title = JustFunctionName(source_location::current().function_name());

            scheduler->SetTesting(false);

            vector<string> expectedList = {
                "JS_NO_EXEC",                                      // slot 1 js
                "TX_DISABLE_GPS_ENABLE",
                                            "SEND_NO_MSG_NONE",    // slot 1 msg
                "JS_NO_EXEC",               "SEND_NO_MSG_NONE",    // slot 2
                "JS_NO_EXEC",               "SEND_NO_MSG_NONE",    // slot 3
                "JS_NO_EXEC",               "SEND_NO_MSG_NONE",    // slot 4
                "JS_NO_EXEC",               "SEND_NO_MSG_NONE",    // slot 5
            };

            bool testOk = Assert(title, scheduler->GetMarkList(), expectedList);
            scheduler->DestroyMarkList(id);

            LogNL();
            Log("=== Test ", title, " ", testOk ? "" : "NOT ", "ok ===");
            LogNL();
        });
        tedTestInner.RegisterForTimedEvent(INNER_DELAY_MS);
    });
    tedTestOuter.RegisterForTimedEvent(NextTestDuration());
}


// some custom messages need gps, others don't, with gps lock
void TestSomeCustomMessagesNeedGpsSomeDontWithGps()
{
    static TimedEventHandlerDelegate tedTestOuter;
    tedTestOuter.SetCallback([]{
        static TimedEventHandlerDelegate tedTestInner;

        scheduler->SetTesting(true);
        int id = IncrAndGetTestId();
        scheduler->CreateMarkList(id);

        bool haveGpsLock = true;
        SetSlot("slot1", msgDefBlank, jsUsesNeither);
        SetSlot("slot2", msgDefBlank, jsUsesNeither);
        SetSlot("slot3", msgDefSet,   jsUsesMsg);
        SetSlot("slot4", msgDefSet,   jsUsesBoth);
        SetSlot("slot5", msgDefBlank, jsUsesNeither);
        scheduler->PrepareWindowSlotBehavior(haveGpsLock);
        scheduler->PrepareWindowSchedule(0);

        tedTestInner.SetCallback([id]{
            string title = JustFunctionName(source_location::current().function_name());

            scheduler->SetTesting(false);

            vector<string> expectedList = {
                "JS_EXEC",               "SEND_REGULAR_TYPE1",      // slot 1
                "JS_EXEC",               "SEND_BASIC_TELEMETRY",    // slot 2
                "JS_EXEC",               "SEND_CUSTOM_MESSAGE",     // slot 3
                "JS_EXEC",               "SEND_CUSTOM_MESSAGE",     // slot 4
                "JS_EXEC",                                          // slot 5 js
                "TX_DISABLE_GPS_ENABLE",
                                         "SEND_NO_MSG_NONE",        // slot 5 msg
            };

            bool testOk = Assert(title, scheduler->GetMarkList(), expectedList);
            scheduler->DestroyMarkList(id);

            LogNL();
            Log("=== Test ", title, " ", testOk ? "" : "NOT ", "ok ===");
            LogNL();
        });
        tedTestInner.RegisterForTimedEvent(INNER_DELAY_MS);
    });
    tedTestOuter.RegisterForTimedEvent(NextTestDuration());
}



// some custom messages need gps, others don't, with no gps lock.
// expect to see earlier gps enable as a result of not sending the
// custom message that depends on gps having a lock this time.
void TestSomeCustomMessagesNeedGpsSomeDontNoGps()
{
    static TimedEventHandlerDelegate tedTestOuter;
    tedTestOuter.SetCallback([]{
        static TimedEventHandlerDelegate tedTestInner;

        scheduler->SetTesting(true);
        int id = IncrAndGetTestId();
        scheduler->CreateMarkList(id);

        bool haveGpsLock = false;
        SetSlot("slot1", msgDefBlank, jsUsesNeither);
        SetSlot("slot2", msgDefBlank, jsUsesNeither);
        SetSlot("slot3", msgDefSet,   jsUsesMsg);
        SetSlot("slot4", msgDefSet,   jsUsesBoth);
        SetSlot("slot5", msgDefBlank, jsUsesNeither);
        scheduler->PrepareWindowSlotBehavior(haveGpsLock);
        scheduler->PrepareWindowSchedule(0);

        tedTestInner.SetCallback([id]{
            string title = JustFunctionName(source_location::current().function_name());

            scheduler->SetTesting(false);

            vector<string> expectedList = {
                "JS_EXEC",               "SEND_NO_MSG_NONE",        // slot 1
                "JS_EXEC",               "SEND_NO_MSG_NONE",        // slot 2
                "JS_EXEC",               "SEND_CUSTOM_MESSAGE",     // slot 3
                "JS_NO_EXEC",                                       // slot 4 js
                "TX_DISABLE_GPS_ENABLE",
                                         "SEND_NO_MSG_NONE",        // slot 4 msg
                "JS_EXEC",               "SEND_NO_MSG_NONE",        // slot 5
            };

            bool testOk = Assert(title, scheduler->GetMarkList(), expectedList);
            scheduler->DestroyMarkList(id);

            LogNL();
            Log("=== Test ", title, " ", testOk ? "" : "NOT ", "ok ===");
            LogNL();
        });
        tedTestInner.RegisterForTimedEvent(INNER_DELAY_MS);
    });
    tedTestOuter.RegisterForTimedEvent(NextTestDuration());
}















///////////////////////////////////////////////////////////////////////////////
// Tests with bad javascript
///////////////////////////////////////////////////////////////////////////////


// default behavior, with gps lock, bad js
void TestDefaultWithGpsBadJs()
{
    static TimedEventHandlerDelegate tedTestOuter;
    tedTestOuter.SetCallback([]{
        static TimedEventHandlerDelegate tedTestInner;

        scheduler->SetTesting(true);
        int id = IncrAndGetTestId();
        scheduler->CreateMarkList(id);

        bool haveGpsLock = true;
        SetSlot("slot1", msgDefBlank, jsUsesNeitherBad);
        SetSlot("slot2", msgDefBlank, jsUsesNeitherBad);
        SetSlot("slot3", msgDefBlank, jsUsesNeitherBad);
        SetSlot("slot4", msgDefBlank, jsUsesNeitherBad);
        SetSlot("slot5", msgDefBlank, jsUsesNeitherBad);
        scheduler->PrepareWindowSlotBehavior(haveGpsLock);
        scheduler->PrepareWindowSchedule(0);

        tedTestInner.SetCallback([id]{
            string title = JustFunctionName(source_location::current().function_name());

            scheduler->SetTesting(false);

            vector<string> expectedList = {
                "JS_EXEC",               "SEND_REGULAR_TYPE1",      // slot 1
                "JS_EXEC",               "SEND_BASIC_TELEMETRY",    // slot 2
                "JS_EXEC",                                          // slot 3 js
                "TX_DISABLE_GPS_ENABLE",
                                         "SEND_NO_MSG_NONE",        // slot 3 msg
                "JS_EXEC",               "SEND_NO_MSG_NONE",        // slot 4
                "JS_EXEC",               "SEND_NO_MSG_NONE",        // slot 5
            };

            bool testOk = Assert(title, scheduler->GetMarkList(), expectedList);
            scheduler->DestroyMarkList(id);

            LogNL();
            Log("=== Test ", title, " ", testOk ? "" : "NOT ", "ok ===");
            LogNL();
        });
        tedTestInner.RegisterForTimedEvent(INNER_DELAY_MS);
    });
    tedTestOuter.RegisterForTimedEvent(NextTestDuration());
}


// default behavior, with no gps lock, bad js
void TestDefaultNoGpsBadJs()
{
    static TimedEventHandlerDelegate tedTestOuter;
    tedTestOuter.SetCallback([]{
        static TimedEventHandlerDelegate tedTestInner;

        scheduler->SetTesting(true);
        int id = IncrAndGetTestId();
        scheduler->CreateMarkList(id);

        bool haveGpsLock = false;
        SetSlot("slot1", msgDefBlank, jsUsesNeitherBad);
        SetSlot("slot2", msgDefBlank, jsUsesNeitherBad);
        SetSlot("slot3", msgDefBlank, jsUsesNeitherBad);
        SetSlot("slot4", msgDefBlank, jsUsesNeitherBad);
        SetSlot("slot5", msgDefBlank, jsUsesNeitherBad);
        scheduler->PrepareWindowSlotBehavior(haveGpsLock);
        scheduler->PrepareWindowSchedule(0);

        tedTestInner.SetCallback([id]{
            string title = JustFunctionName(source_location::current().function_name());

            scheduler->SetTesting(false);

            vector<string> expectedList = {
                "JS_EXEC",                                      // slot 1 js
                "TX_DISABLE_GPS_ENABLE",
                                         "SEND_NO_MSG_NONE",    // slot 1 msg
                "JS_EXEC",               "SEND_NO_MSG_NONE",    // slot 2
                "JS_EXEC",               "SEND_NO_MSG_NONE",    // slot 3
                "JS_EXEC",               "SEND_NO_MSG_NONE",    // slot 4
                "JS_EXEC",               "SEND_NO_MSG_NONE",    // slot 5
            };

            bool testOk = Assert(title, scheduler->GetMarkList(), expectedList);
            scheduler->DestroyMarkList(id);

            LogNL();
            Log("=== Test ", title, " ", testOk ? "" : "NOT ", "ok ===");
            LogNL();
        });
        tedTestInner.RegisterForTimedEvent(INNER_DELAY_MS);
    });
    tedTestOuter.RegisterForTimedEvent(NextTestDuration());
}


// all override, with gps lock, bad js
void TestAllOverrideWithGpsBadJs()
{
    static TimedEventHandlerDelegate tedTestOuter;
    tedTestOuter.SetCallback([]{
        static TimedEventHandlerDelegate tedTestInner;

        scheduler->SetTesting(true);
        int id = IncrAndGetTestId();
        scheduler->CreateMarkList(id);

        bool haveGpsLock = true;
        SetSlot("slot1", msgDefSet, jsUsesBothBad);
        SetSlot("slot2", msgDefSet, jsUsesBothBad);
        SetSlot("slot3", msgDefSet, jsUsesBothBad);
        SetSlot("slot4", msgDefSet, jsUsesBothBad);
        SetSlot("slot5", msgDefSet, jsUsesBothBad);
        scheduler->PrepareWindowSlotBehavior(haveGpsLock);
        scheduler->PrepareWindowSchedule(0);

        tedTestInner.SetCallback([id]{
            string title = JustFunctionName(source_location::current().function_name());

            scheduler->SetTesting(false);

            vector<string> expectedList = {
                "JS_EXEC",               "SEND_REGULAR_TYPE1",               // slot 1
                "JS_EXEC",               "SEND_BASIC_TELEMETRY",             // slot 2
                "JS_EXEC",               "SEND_NO_MSG_BAD_JS_NO_DEFAULT",    // slot 3
                "JS_EXEC",               "SEND_NO_MSG_BAD_JS_NO_DEFAULT",    // slot 4
                "JS_EXEC",               "SEND_NO_MSG_BAD_JS_NO_DEFAULT",    // slot 5
                "TX_DISABLE_GPS_ENABLE",
            };

            bool testOk = Assert(title, scheduler->GetMarkList(), expectedList);
            scheduler->DestroyMarkList(id);

            LogNL();
            Log("=== Test ", title, " ", testOk ? "" : "NOT ", "ok ===");
            LogNL();
        });
        tedTestInner.RegisterForTimedEvent(INNER_DELAY_MS);
    });
    tedTestOuter.RegisterForTimedEvent(NextTestDuration());
}


// all custom messages need gps, with no gps lock, bad js
void TestAllCustomMessagesNeedGpsWithNoGpsBadJs()
{
    static TimedEventHandlerDelegate tedTestOuter;
    tedTestOuter.SetCallback([]{
        static TimedEventHandlerDelegate tedTestInner;

        scheduler->SetTesting(true);
        int id = IncrAndGetTestId();
        scheduler->CreateMarkList(id);

        bool haveGpsLock = false;
        SetSlot("slot1", msgDefSet, jsUsesBothBad);
        SetSlot("slot2", msgDefSet, jsUsesBothBad);
        SetSlot("slot3", msgDefSet, jsUsesBothBad);
        SetSlot("slot4", msgDefSet, jsUsesBothBad);
        SetSlot("slot5", msgDefSet, jsUsesBothBad);
        scheduler->PrepareWindowSlotBehavior(haveGpsLock);
        scheduler->PrepareWindowSchedule(0);

        tedTestInner.SetCallback([id]{
            string title = JustFunctionName(source_location::current().function_name());

            scheduler->SetTesting(false);

            vector<string> expectedList = {
                "JS_NO_EXEC",                                      // slot 1 js
                "TX_DISABLE_GPS_ENABLE",
                                            "SEND_NO_MSG_NONE",    // slot 1 msg
                "JS_NO_EXEC",               "SEND_NO_MSG_NONE",    // slot 2
                "JS_NO_EXEC",               "SEND_NO_MSG_NONE",    // slot 3
                "JS_NO_EXEC",               "SEND_NO_MSG_NONE",    // slot 4
                "JS_NO_EXEC",               "SEND_NO_MSG_NONE",    // slot 5
            };

            bool testOk = Assert(title, scheduler->GetMarkList(), expectedList);
            scheduler->DestroyMarkList(id);

            LogNL();
            Log("=== Test ", title, " ", testOk ? "" : "NOT ", "ok ===");
            LogNL();
        });
        tedTestInner.RegisterForTimedEvent(INNER_DELAY_MS);
    });
    tedTestOuter.RegisterForTimedEvent(NextTestDuration());
}


// some custom messages need gps, others don't, with gps lock, bad js
void TestSomeCustomMessagesNeedGpsSomeDontWithGpsBadJs()
{
    static TimedEventHandlerDelegate tedTestOuter;
    tedTestOuter.SetCallback([]{
        static TimedEventHandlerDelegate tedTestInner;

        scheduler->SetTesting(true);
        int id = IncrAndGetTestId();
        scheduler->CreateMarkList(id);

        bool haveGpsLock = true;
        SetSlot("slot1", msgDefBlank, jsUsesNeitherBad);
        SetSlot("slot2", msgDefBlank, jsUsesNeitherBad);
        SetSlot("slot3", msgDefSet,   jsUsesMsgBad);
        SetSlot("slot4", msgDefSet,   jsUsesBothBad);
        SetSlot("slot5", msgDefBlank, jsUsesNeitherBad);
        scheduler->PrepareWindowSlotBehavior(haveGpsLock);
        scheduler->PrepareWindowSchedule(0);

        tedTestInner.SetCallback([id]{
            string title = JustFunctionName(source_location::current().function_name());

            scheduler->SetTesting(false);

            vector<string> expectedList = {
                "JS_EXEC",               "SEND_REGULAR_TYPE1",              // slot 1
                "JS_EXEC",               "SEND_BASIC_TELEMETRY",            // slot 2
                "JS_EXEC",               "SEND_NO_MSG_BAD_JS_NO_DEFAULT",   // slot 3
                "JS_EXEC",               "SEND_NO_MSG_BAD_JS_NO_DEFAULT",   // slot 4
                "JS_EXEC",                                                  // slot 5 js
                "TX_DISABLE_GPS_ENABLE",
                                         "SEND_NO_MSG_NONE",                // slot 5 msg
            };

            bool testOk = Assert(title, scheduler->GetMarkList(), expectedList);
            scheduler->DestroyMarkList(id);

            LogNL();
            Log("=== Test ", title, " ", testOk ? "" : "NOT ", "ok ===");
            LogNL();
        });
        tedTestInner.RegisterForTimedEvent(INNER_DELAY_MS);
    });
    tedTestOuter.RegisterForTimedEvent(NextTestDuration());
}



// some custom messages need gps, others don't, with no gps lock.
// expect to see earlier gps enable as a result of not sending the
// custom message that depends on gps having a lock this time.
// bad js
void TestSomeCustomMessagesNeedGpsSomeDontNoGpsBadJs()
{
    static TimedEventHandlerDelegate tedTestOuter;
    tedTestOuter.SetCallback([]{
        static TimedEventHandlerDelegate tedTestInner;

        scheduler->SetTesting(true);
        int id = IncrAndGetTestId();
        scheduler->CreateMarkList(id);

        bool haveGpsLock = false;
        SetSlot("slot1", msgDefBlank, jsUsesNeitherBad);
        SetSlot("slot2", msgDefBlank, jsUsesNeitherBad);
        SetSlot("slot3", msgDefSet,   jsUsesMsgBad);
        SetSlot("slot4", msgDefSet,   jsUsesBothBad);
        SetSlot("slot5", msgDefBlank, jsUsesNeitherBad);
        scheduler->PrepareWindowSlotBehavior(haveGpsLock);
        scheduler->PrepareWindowSchedule(0);

        tedTestInner.SetCallback([id]{
            string title = JustFunctionName(source_location::current().function_name());

            scheduler->SetTesting(false);

            vector<string> expectedList = {
                "JS_EXEC",               "SEND_NO_MSG_NONE",                // slot 1
                "JS_EXEC",               "SEND_NO_MSG_NONE",                // slot 2
                "JS_EXEC",               "SEND_NO_MSG_BAD_JS_NO_DEFAULT",   // slot 3
                "JS_NO_EXEC",                                               // slot 4 js
                "TX_DISABLE_GPS_ENABLE",
                                         "SEND_NO_MSG_NONE",                // slot 4 msg
                "JS_EXEC",               "SEND_NO_MSG_NONE",                // slot 5
            };

            bool testOk = Assert(title, scheduler->GetMarkList(), expectedList);
            scheduler->DestroyMarkList(id);

            LogNL();
            Log("=== Test ", title, " ", testOk ? "" : "NOT ", "ok ===");
            LogNL();
        });
        tedTestInner.RegisterForTimedEvent(INNER_DELAY_MS);
    });
    tedTestOuter.RegisterForTimedEvent(NextTestDuration());
}


void TestOverrideBasicTelemetryButBadJs()
{
    static TimedEventHandlerDelegate tedTestOuter;
    tedTestOuter.SetCallback([]{
        static TimedEventHandlerDelegate tedTestInner;

        scheduler->SetTesting(true);
        int id = IncrAndGetTestId();
        scheduler->CreateMarkList(id);

        bool haveGpsLock = true;
        SetSlot("slot1", msgDefBlank, jsUsesNeitherBad);
        SetSlot("slot2", msgDefSet,   jsUsesMsgBad);
        SetSlot("slot3", msgDefBlank, jsUsesNeitherBad);
        SetSlot("slot4", msgDefBlank, jsUsesNeitherBad);
        SetSlot("slot5", msgDefBlank, jsUsesNeitherBad);
        scheduler->PrepareWindowSlotBehavior(haveGpsLock);
        scheduler->PrepareWindowSchedule(0);

        tedTestInner.SetCallback([id]{
            string title = JustFunctionName(source_location::current().function_name());

            scheduler->SetTesting(false);

            vector<string> expectedList = {
                "JS_EXEC",               "SEND_REGULAR_TYPE1",      // slot 1
                "JS_EXEC",               "SEND_BASIC_TELEMETRY",    // slot 2
                "JS_EXEC",                                          // slot 3 js
                "TX_DISABLE_GPS_ENABLE",
                                         "SEND_NO_MSG_NONE",        // slot 3 msg
                "JS_EXEC",               "SEND_NO_MSG_NONE",        // slot 4
                "JS_EXEC",               "SEND_NO_MSG_NONE",        // slot 5
            };

            bool testOk = Assert(title, scheduler->GetMarkList(), expectedList);
            scheduler->DestroyMarkList(id);

            LogNL();
            Log("=== Test ", title, " ", testOk ? "" : "NOT ", "ok ===");
            LogNL();
        });
        tedTestInner.RegisterForTimedEvent(INNER_DELAY_MS);
    });
    tedTestOuter.RegisterForTimedEvent(NextTestDuration());
}


void TestOverrideBasicTelemetryNoGpsButBadJs()
{
    static TimedEventHandlerDelegate tedTestOuter;
    tedTestOuter.SetCallback([]{
        static TimedEventHandlerDelegate tedTestInner;

        scheduler->SetTesting(true);
        int id = IncrAndGetTestId();
        scheduler->CreateMarkList(id);

        bool haveGpsLock = false;
        SetSlot("slot1", msgDefBlank, jsUsesNeitherBad);
        SetSlot("slot2", msgDefSet,   jsUsesMsgBad);
        SetSlot("slot3", msgDefBlank, jsUsesNeitherBad);
        SetSlot("slot4", msgDefBlank, jsUsesNeitherBad);
        SetSlot("slot5", msgDefBlank, jsUsesNeitherBad);
        scheduler->PrepareWindowSlotBehavior(haveGpsLock);
        scheduler->PrepareWindowSchedule(0);

        tedTestInner.SetCallback([id]{
            string title = JustFunctionName(source_location::current().function_name());

            scheduler->SetTesting(false);

            vector<string> expectedList = {
                "JS_EXEC",               "SEND_NO_MSG_NONE",                    // slot 1
                "JS_EXEC",               "SEND_NO_MSG_BAD_JS_NO_ABLE_DEFAULT",  // slot 2
                "JS_EXEC",                                                      // slot 3 js
                "TX_DISABLE_GPS_ENABLE",
                                         "SEND_NO_MSG_NONE",                    // slot 3 msg
                "JS_EXEC",               "SEND_NO_MSG_NONE",                    // slot 4
                "JS_EXEC",               "SEND_NO_MSG_NONE",                    // slot 5
            };

            bool testOk = Assert(title, scheduler->GetMarkList(), expectedList);
            scheduler->DestroyMarkList(id);

            LogNL();
            Log("=== Test ", title, " ", testOk ? "" : "NOT ", "ok ===");
            LogNL();
        });
        tedTestInner.RegisterForTimedEvent(INNER_DELAY_MS);
    });
    tedTestOuter.RegisterForTimedEvent(NextTestDuration());
}








































void CopilotControlScheduler::TestPrepareWindowSchedule()
{
    scheduler = this;

    BackupFiles();

    Log("TestPrepareWindowSchedule Start");
    ResetTestDuration();

    // with good javascript
    TestDefaultWithGps();
    TestDefaultNoGps();
    TestAllOverrideWithGps();
    TestAllCustomMessagesNeedGpsWithNoGps();
    TestSomeCustomMessagesNeedGpsSomeDontWithGps();
    TestSomeCustomMessagesNeedGpsSomeDontNoGps();


    // with bad javascript
    TestDefaultWithGpsBadJs();
    TestDefaultNoGpsBadJs();
    TestAllOverrideWithGpsBadJs();
    TestAllCustomMessagesNeedGpsWithNoGpsBadJs();
    TestSomeCustomMessagesNeedGpsSomeDontWithGpsBadJs();
    TestSomeCustomMessagesNeedGpsSomeDontNoGpsBadJs();
    TestOverrideBasicTelemetryButBadJs();
    TestOverrideBasicTelemetryNoGpsButBadJs();


    Log("TestPrepareWindowSchedule Done");

    // async restore files after last test run
    {
        static TimedEventHandlerDelegate tedRestore;
        tedRestore.SetCallback([this]{
            RestoreFiles();
        });
        tedRestore.RegisterForTimedEvent(NextTestDuration());
    }
}









































void CopilotControlScheduler::TestCalculateTimeAtWindowStartUs(bool fullSweep)
{
    auto GpsTime = [](const FixTime &gpsFix){
        string time = Time::MakeDateTime(0, gpsFix.minute, gpsFix.second, gpsFix.millisecond * 1'000);
        time = time.substr(14);         // get rid of leading date and hour
        time.resize(time.size() - 3);   // get rid of microseconds, leave just ms
        return time;
    };

    auto Test = [&](uint8_t windowStartMin, uint8_t gpsMin, uint8_t gpsSec, uint16_t gpsMs, bool verbose = true) {
        FixTime gpsFix = {
            .minute      = gpsMin,
            .second      = gpsSec,
            .millisecond = gpsMs,
        };

        uint64_t timeAtWindowStartUs =
            CalculateTimeAtWindowStartUs(windowStartMin, gpsFix, 0);

        if (verbose)
        {
            string windowTimeTarget = string{" "} + to_string(windowStartMin) + ":01.000";
            string gpsTime          = GpsTime(gpsFix);

            Log("Window Time Target: ", windowTimeTarget);
            Log("GPS Time          : ", gpsTime);
            // we display the 'time at' directly, knowingly having fed the calcuation that
            // the gps current time is 0. from that, we see that the 'time at' as the
            // time "above" 0, which is usefully also the difference between 'now' and the
            // next window.
            // this makes visually confirming the results easier for a few reasons, like
            // eyeballing the diff is clear, and also the values calculated are always the
            // same since we don't have a real running clock.
            Log("Window Time Calc  : ", Time::MakeTimeMMSSmmmFromUs(timeAtWindowStartUs));
            LogNL();
        }

        return timeAtWindowStartUs;
    };


    // window: 4:01.000
    // gps   : 2:30.400
    // --------------------
    // minDiff = 2
    // secDiff = -29
    // msDiff  = -400
    Test(4, 22, 30, 400);


    // window: 4:01.000
    // gps   : 4:00.400
    // --------------------
    // minDiff = 0
    // secDiff = 1
    // msDiff  = -400
    Test(4, 4, 0, 400);


    // window: 4:01.000
    // gps   : 5:30.400
    // --------------------
    // minDiff = -1
    // secDiff = -29
    // msDiff  = -400
    Test(4, 5, 30, 400);


    // cases right on the wraparound point
    Test(4, 4, 0, 999);
    Test(4, 4, 1,   0);
    Test(4, 4, 1,   1);


    // test every case
    int totalTests = 0;
    int failedTests = 0;
    auto Assert = [&](uint8_t windowStartMin, uint8_t gpsMin, uint8_t gpsSec, uint16_t gpsMs, uint64_t expected){
        ++totalTests;

        uint64_t actual = Test(windowStartMin, gpsMin, gpsSec, gpsMs, false);

        if (actual != expected)
        {
            ++failedTests;

            Log("ERR: Actual(", actual, ") != Expected(", expected, ")");
            Log("- winStartMin: ", windowStartMin);
            Log("- gpsMin     : ", gpsMin);
            Log("- gpsSec     : ", gpsSec);
            Log("- gpsMs      : ", gpsMs);
            LogNL();
        }
    };


    if (fullSweep == false) { return ; }

    Log("Testing all cases (full sweep)");


    // full sweep of all values takes 2 minutes at 125MHz (36,000,000 tests)
    // full sweep of real window values takes 1 minute at 125MHz (18,000,000 tests)
    Timeline::Use([&](auto &t){
        uint8_t windowStartMinLow  = 0;
        uint8_t windowStartMinHigh = 8;
        uint8_t windowStepSize     = 2;
        for (uint8_t windowStartMin = windowStartMinLow; windowStartMin <= windowStartMinHigh; windowStartMin += windowStepSize)
        {
            for (uint8_t gpsMin = 0; gpsMin < 60; ++gpsMin)
            {
                for (uint8_t gpsSec = 0; gpsSec < 60; ++gpsSec)
                {
                    for (uint16_t gpsMs = 0; gpsMs < 1'000; ++gpsMs)
                    {
                        int64_t expected = 0;
                        expected += windowStartMin * 60 * 1'000;
                        expected += 1 * 1'000;
                        expected -= (gpsMin % 10) * 60 * 1'000;
                        expected -= gpsSec * 1'000;
                        expected -= gpsMs;

                        if (expected < 0)
                        {
                            expected += 10 * 60 * 1'000;
                        }

                        // scale to microseconds
                        expected *= 1'000;

                        Assert(windowStartMin, gpsMin, gpsSec, gpsMs, (uint64_t)expected);
                    }
                }
            }
        }
    });

    Log("Tests ", failedTests != 0 ? "NOT " : "", "ok");
    Log(Commas(failedTests), " failed / ", Commas(totalTests), " total");
}











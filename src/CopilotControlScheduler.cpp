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

        ConfigureWindowSlotBehavior(haveGpsLock);

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

        ConfigureWindowSlotBehavior(haveGpsLock);

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

        ConfigureWindowSlotBehavior(haveGpsLock);

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

        ConfigureWindowSlotBehavior(haveGpsLock);

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

static auto NextTestDuration = []{
    static uint64_t durationMs = 0;

    uint64_t retVal = durationMs;
    
    // works at 125 MHz
    durationMs += 1500;

    return retVal;
};

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
        scheduler->ConfigureWindowSlotBehavior(haveGpsLock);
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
        scheduler->ConfigureWindowSlotBehavior(haveGpsLock);
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
        scheduler->ConfigureWindowSlotBehavior(haveGpsLock);
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
        scheduler->ConfigureWindowSlotBehavior(haveGpsLock);
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
        scheduler->ConfigureWindowSlotBehavior(haveGpsLock);
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
        scheduler->ConfigureWindowSlotBehavior(haveGpsLock);
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
        scheduler->ConfigureWindowSlotBehavior(haveGpsLock);
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
        scheduler->ConfigureWindowSlotBehavior(haveGpsLock);
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
        scheduler->ConfigureWindowSlotBehavior(haveGpsLock);
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
        scheduler->ConfigureWindowSlotBehavior(haveGpsLock);
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
        scheduler->ConfigureWindowSlotBehavior(haveGpsLock);
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
        scheduler->ConfigureWindowSlotBehavior(haveGpsLock);
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
        scheduler->ConfigureWindowSlotBehavior(haveGpsLock);
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
        scheduler->ConfigureWindowSlotBehavior(haveGpsLock);
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

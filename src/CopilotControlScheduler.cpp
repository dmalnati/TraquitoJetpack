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



// all elements need to be found in the order expressed for it to be a match.
// inputs are modified.
bool IsSequencedSubset(vector<string> &subsetElementList, vector<string> &supersetElementList)
{
    while (subsetElementList.size())
    {
        // pop first element from superset
        string subsetElement = *subsetElementList.begin();
        subsetElementList.erase(subsetElementList.begin());

        // remove non-matching elements from superset
        while (supersetElementList.size() && subsetElement != *supersetElementList.begin())
        {
            supersetElementList.erase(supersetElementList.begin());
        }

        // remove the element you just compared equal to (if exists)
        if (supersetElementList.size())
        {
            supersetElementList.erase(supersetElementList.begin());
        }

        // if now empty, the compare is over
        if (supersetElementList.size() == 0)
        {
            break;
        }
    }

    return subsetElementList.size() == 0;
}



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

vector<string> testResultList;

void ClearTestResultList()
{
    testResultList.clear();
}


string JustFunctionName(string fnScoped)
{
    return Split(fnScoped, "::")[0];
}










///////////////////////////////////////////////////////////////////////////////
// TestEventInterface
///////////////////////////////////////////////////////////////////////////////


// expected is a subset of actual, but all elements need to be found
// in the order expressed for it to be a match
static auto AssertEvent = [](string title, vector<string> actualList, vector<string> expectedList){
    bool retVal = true;

    vector<string> actualListCpy = actualList;

    bool isSeqSubset = IsSequencedSubset(expectedList, actualList);

    if (isSeqSubset == false)
    {
        retVal = false;

        LogNL();
        Log("Assert ERR: test", title);
        Log("Actual List:");
        for (const auto &str : actualListCpy)
        {
            Log("  ", str);
        }
        Log("Expected items remain:");
        for (const auto &str : expectedList)
        {
            Log("  ", str);
        }
    }

    return retVal;
};


static Fix3DPlus MakeFix3DPlus(const char *dateTime)
{
    auto tp = Time::ParseDateTime(dateTime);

    Fix3DPlus gpsFix;
    gpsFix.year        = tp.year;
    gpsFix.hour        = tp.hour;
    gpsFix.minute      = tp.minute;
    gpsFix.second      = tp.second;
    gpsFix.millisecond = (uint16_t)(tp.us / 1'000);
    gpsFix.dateTime    = dateTime;

    return gpsFix;
}

static FixTime MakeFixTime(const char *dateTime)
{
    return MakeFix3DPlus(dateTime);
}





static int id = IncrAndGetTestId();

void TestEventsStart(TimerSequence &ts)
{
    ts.Add([&]{
        scheduler->SetTesting(true);
        scheduler->CreateMarkList(id);
        Log("Test pre-start: ", Time::MakeTimeFromUs(PAL.Micros()));

        ts.TimeoutInMs(0);
    }).Add([&]{
        scheduler->Start();

        ts.TimeoutInMs(0);
    }).Add([&]{
        scheduler->Stop();
        Log("Test post-stop: ", Time::MakeTimeFromUs(PAL.Micros()));

        string title = JustFunctionName(source_location::current().function_name());

        scheduler->SetTesting(false);

        vector<string> expectedList = {
            "REQ_NEW_GPS_LOCK"
        };

        bool testOk = AssertEvent(title, scheduler->GetMarkList(), expectedList);
        scheduler->DestroyMarkList(id);

        LogNL();
        uint64_t timeNowUs = PAL.Micros();
        string result = string{"["} + Time::GetNotionalTimeAtSystemUs(timeNowUs) + ", " + Time::MakeTimeFromUs(timeNowUs) + "] === Test " + (testOk ? "" : "NOT ") + "ok " + title + " ===";
        testResultList.push_back(result);
        Log(result);
        LogNL();

        ts.TimeoutInMs(0);
    });
}

void TestEventsStartTime(TimerSequence &ts)
{
    ts.Add([&]{
        scheduler->SetTesting(true);
        scheduler->CreateMarkList(id);
        Log("Test pre-start: ", Time::MakeTimeFromUs(PAL.Micros()));

        ts.TimeoutInMs(0);
    }).Add([&]{
        scheduler->Start();

        ts.TimeoutInMs(0);
    }).Add([&]{
        scheduler->OnGpsTimeLock(MakeFixTime("2025-01-01 12:10:00.500"));

        ts.TimeoutInMs(1'000);
    }).Add([&]{
        scheduler->Stop();
        Log("Test post-stop: ", Time::MakeTimeFromUs(PAL.Micros()));

        string title = JustFunctionName(source_location::current().function_name());

        scheduler->SetTesting(false);

        vector<string> expectedList = {
            "REQ_NEW_GPS_LOCK",
            "APPLY_TIME_AND_UPDATE_SCHEDULE",
            "COAST_SCHEDULED",
            "COAST_TRIGGERED",
            "SCHEDULE_LOCK_OUT_START",
            "SCHEDULE_LOCK_OUT_END",

            // next window
            "APPLY_CACHE_OLD_TIME",
        };

        bool testOk = AssertEvent(title, scheduler->GetMarkList(), expectedList);
        scheduler->DestroyMarkList(id);

        LogNL();
        uint64_t timeNowUs = PAL.Micros();
        string result = string{"["} + Time::GetNotionalTimeAtSystemUs(timeNowUs) + ", " + Time::MakeTimeFromUs(timeNowUs) + "] === Test " + (testOk ? "" : "NOT ") + "ok " + title + " ===";
        testResultList.push_back(result);
        Log(result);
        LogNL();

        ts.TimeoutInMs(0);
    });
}

void TestEventsStartTimeTime(TimerSequence &ts)
{
    ts.Add([&]{
        scheduler->SetTesting(true);
        scheduler->CreateMarkList(id);
        Log("Test pre-start: ", Time::MakeTimeFromUs(PAL.Micros()));

        ts.TimeoutInMs(0);
    }).Add([&]{
        scheduler->Start();

        ts.TimeoutInMs(0);
    }).Add([&]{
        scheduler->OnGpsTimeLock(MakeFixTime("2025-01-01 12:10:00.500"));

        ts.TimeoutInMs(0);
    }).Add([&]{
        scheduler->OnGpsTimeLock(MakeFixTime("2025-01-01 12:10:00.600"));

        ts.TimeoutInMs(1'000);
    }).Add([&]{
        scheduler->Stop();
        Log("Test post-stop: ", Time::MakeTimeFromUs(PAL.Micros()));

        string title = JustFunctionName(source_location::current().function_name());

        scheduler->SetTesting(false);

        vector<string> expectedList = {
            "REQ_NEW_GPS_LOCK",
            "APPLY_TIME_AND_UPDATE_SCHEDULE",
            "COAST_SCHEDULED",
            "COAST_TRIGGERED",
            "SCHEDULE_LOCK_OUT_START",
            "SCHEDULE_LOCK_OUT_END",

            // next window
            "APPLY_CACHE_OLD_TIME",
        };

        bool testOk = AssertEvent(title, scheduler->GetMarkList(), expectedList);
        scheduler->DestroyMarkList(id);

        LogNL();
        uint64_t timeNowUs = PAL.Micros();
        string result = string{"["} + Time::GetNotionalTimeAtSystemUs(timeNowUs) + ", " + Time::MakeTimeFromUs(timeNowUs) + "] === Test " + (testOk ? "" : "NOT ") + "ok " + title + " ===";
        testResultList.push_back(result);
        Log(result);
        LogNL();

        ts.TimeoutInMs(0);
    });
}

void TestEventsStart3d(TimerSequence &ts)
{
    ts.Add([&]{
        scheduler->SetTesting(true);
        scheduler->CreateMarkList(id);
        Log("Test pre-start: ", Time::MakeTimeFromUs(PAL.Micros()));

        ts.TimeoutInMs(0);
    }).Add([&]{
        scheduler->Start();

        ts.TimeoutInMs(0);
    }).Add([&]{
        scheduler->OnGps3DPlusLock(MakeFix3DPlus("2025-01-01 12:10:00.500"));

        ts.TimeoutInMs(1'000);
    }).Add([&]{
        scheduler->Stop();
        Log("Test post-stop: ", Time::MakeTimeFromUs(PAL.Micros()));

        string title = JustFunctionName(source_location::current().function_name());

        scheduler->SetTesting(false);

        vector<string> expectedList = {
            "REQ_NEW_GPS_LOCK",
            "APPLY_TIME_AND_UPDATE_SCHEDULE",
            "COAST_SCHEDULED",
            "COAST_TRIGGERED",
            "SCHEDULE_LOCK_OUT_START",
            "HI",
            "SCHEDULE_LOCK_OUT_END",

            // next window
            "APPLY_CACHE_OLD_3D_PLUS",
        };

        bool testOk = AssertEvent(title, scheduler->GetMarkList(), expectedList);
        scheduler->DestroyMarkList(id);

        LogNL();
        uint64_t timeNowUs = PAL.Micros();
        string result = string{"["} + Time::GetNotionalTimeAtSystemUs(timeNowUs) + ", " + Time::MakeTimeFromUs(timeNowUs) + "] === Test " + (testOk ? "" : "NOT ") + "ok " + title + " ===";
        testResultList.push_back(result);
        Log(result);
        LogNL();

        ts.TimeoutInMs(0);
    });
}




void CopilotControlScheduler::TestEventInterface()
{
    scheduler = this;

    BackupFiles();

    Log("TestEventInterface Start");
    ResetTestDuration();

    SetUseMarkList(true);
    ClearTestResultList();


    // setup a definite testing environment
    SetStartMinute(0);
    scheduler->Stop();
    SetSlot("slot1", msgDefBlank, jsUsesNeither);
    SetSlot("slot2", msgDefBlank, jsUsesNeither);
    SetSlot("slot3", msgDefBlank, jsUsesNeither);
    SetSlot("slot4", msgDefBlank, jsUsesNeither);
    SetSlot("slot5", msgDefBlank, jsUsesNeither);

    // Reset the async test sequence
    TimerSequence ts;

    // tests
    // TestEventsStart(ts);
    // TestEventsStartTime(ts);
    // TestEventsStartTimeTime(ts);
    TestEventsStart3d(ts);


    // Complete
    ts.Add([this]{
        scheduler->Stop();

        Log(testResultList.size(), " tests run");
        for (const auto &result : testResultList)
        {
            Log(result);
        }
        LogNL();

        ClearTestResultList();
        SetUseMarkList(false);

        RestoreFiles();

        Evm::ExitMainLoop();
    });

    // kick off sequence
    ts.TimeoutInMs(0);
    Evm::MainLoop();

    Log("TestEventInterface Done");
}















///////////////////////////////////////////////////////////////////////////////
// TestPrepareWindowSchedule
///////////////////////////////////////////////////////////////////////////////




// expected is a subset of actual, but all elements need to be found
// in the order expressed for it to be a match
static auto AssertSchedule = [](string title, vector<string> actualList, vector<string> expectedList, bool expectTxWarmup = true){
    bool retVal = true;

    vector<string> actualListCpy = actualList;

    IsSequencedSubset(expectedList, actualList);

    bool hasTxWarmup = find(actualListCpy.begin(), actualListCpy.end(), "TX_WARMUP") != actualListCpy.end();

    if (hasTxWarmup != expectTxWarmup)
    {
        retVal = false;

        LogNL();
        Log("Assert ERR: test", title);
        if (hasTxWarmup)
        {
            Log("Expected NO TX_WARMUP, but there was one");
        }
        else
        {
            Log("Expected TX_WARMUP, but there wasn't one");
        }
    }

    if (expectedList.size() != 0)
    {
        retVal = false;

        LogNL();
        Log("Assert ERR: test", title);
        Log("Actual List:");
        for (const auto &str : actualListCpy)
        {
            Log("  ", str);
        }
        Log("Expected items remain:");
        for (const auto &str : expectedList)
        {
            Log("  ", str);
        }
    }

    return retVal;
};


///////////////////////////////////////////////////////////////////////////////
// Tests with good javascript
///////////////////////////////////////////////////////////////////////////////


// default behavior, with gps lock
void TestDefaultWithGps()
{
    static Timer tTestOuter;
    tTestOuter.SetCallback([]{
        static Timer tTestInner;

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
        scheduler->PrepareWindowSchedule(0, 0);

        tTestInner.SetCallback([id]{
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

            bool testOk = AssertSchedule(title, scheduler->GetMarkList(), expectedList);
            scheduler->DestroyMarkList(id);

            LogNL();
            string result = string{"=== Test "} + (testOk ? "" : "NOT ") + "ok " + title + " ===";
            testResultList.push_back(result);
            Log(result);
            LogNL();
        });
        tTestInner.TimeoutInMs(INNER_DELAY_MS);
    });
    tTestOuter.TimeoutInMs(NextTestDuration());
}


// default behavior, with no gps lock
void TestDefaultNoGps()
{
    static Timer tTestOuter;
    tTestOuter.SetCallback([]{
        static Timer tTestInner;

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
        scheduler->PrepareWindowSchedule(0, 0);

        tTestInner.SetCallback([id]{
            string title = JustFunctionName(source_location::current().function_name());

            scheduler->SetTesting(false);

            vector<string> expectedList = {
                "TX_DISABLE_GPS_ENABLE",
                "JS_EXEC",               "SEND_NO_MSG_NONE",    // slot 1
                "JS_EXEC",               "SEND_NO_MSG_NONE",    // slot 2
                "JS_EXEC",               "SEND_NO_MSG_NONE",    // slot 3
                "JS_EXEC",               "SEND_NO_MSG_NONE",    // slot 4
                "JS_EXEC",               "SEND_NO_MSG_NONE",    // slot 5
            };

            bool testOk = AssertSchedule(title, scheduler->GetMarkList(), expectedList, false);
            scheduler->DestroyMarkList(id);

            LogNL();
            string result = string{"=== Test "} + (testOk ? "" : "NOT ") + "ok " + title + " ===";
            testResultList.push_back(result);
            Log(result);
            LogNL();
        });
        tTestInner.TimeoutInMs(INNER_DELAY_MS);
    });
    tTestOuter.TimeoutInMs(NextTestDuration());
}


// all override, with gps lock
void TestAllOverrideWithGps()
{
    static Timer tTestOuter;
    tTestOuter.SetCallback([]{
        static Timer tTestInner;

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
        scheduler->PrepareWindowSchedule(0, 0);

        tTestInner.SetCallback([id]{
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

            bool testOk = AssertSchedule(title, scheduler->GetMarkList(), expectedList);
            scheduler->DestroyMarkList(id);

            LogNL();
            string result = string{"=== Test "} + (testOk ? "" : "NOT ") + "ok " + title + " ===";
            testResultList.push_back(result);
            Log(result);
            LogNL();
        });
        tTestInner.TimeoutInMs(INNER_DELAY_MS);
    });
    tTestOuter.TimeoutInMs(NextTestDuration());
}


// all custom messages need gps, with no gps lock
void TestAllCustomMessagesNeedGpsWithNoGps()
{
    static Timer tTestOuter;
    tTestOuter.SetCallback([]{
        static Timer tTestInner;

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
        scheduler->PrepareWindowSchedule(0, 0);

        tTestInner.SetCallback([id]{
            string title = JustFunctionName(source_location::current().function_name());

            scheduler->SetTesting(false);

            vector<string> expectedList = {
                "TX_DISABLE_GPS_ENABLE",
                "JS_NO_EXEC",               "SEND_NO_MSG_NONE",    // slot 1
                "JS_NO_EXEC",               "SEND_NO_MSG_NONE",    // slot 2
                "JS_NO_EXEC",               "SEND_NO_MSG_NONE",    // slot 3
                "JS_NO_EXEC",               "SEND_NO_MSG_NONE",    // slot 4
                "JS_NO_EXEC",               "SEND_NO_MSG_NONE",    // slot 5
            };

            bool testOk = AssertSchedule(title, scheduler->GetMarkList(), expectedList, false);
            scheduler->DestroyMarkList(id);

            LogNL();
            string result = string{"=== Test "} + (testOk ? "" : "NOT ") + "ok " + title + " ===";
            testResultList.push_back(result);
            Log(result);
            LogNL();
        });
        tTestInner.TimeoutInMs(INNER_DELAY_MS);
    });
    tTestOuter.TimeoutInMs(NextTestDuration());
}


// some custom messages need gps, others don't, with gps lock
void TestSomeCustomMessagesNeedGpsSomeDontWithGps()
{
    static Timer tTestOuter;
    tTestOuter.SetCallback([]{
        static Timer tTestInner;

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
        scheduler->PrepareWindowSchedule(0, 0);

        tTestInner.SetCallback([id]{
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

            bool testOk = AssertSchedule(title, scheduler->GetMarkList(), expectedList);
            scheduler->DestroyMarkList(id);

            LogNL();
            string result = string{"=== Test "} + (testOk ? "" : "NOT ") + "ok " + title + " ===";
            testResultList.push_back(result);
            Log(result);
            LogNL();
        });
        tTestInner.TimeoutInMs(INNER_DELAY_MS);
    });
    tTestOuter.TimeoutInMs(NextTestDuration());
}



// some custom messages need gps, others don't, with no gps lock.
// expect to see earlier gps enable as a result of not sending the
// custom message that depends on gps having a lock this time.
void TestSomeCustomMessagesNeedGpsSomeDontNoGps()
{
    static Timer tTestOuter;
    tTestOuter.SetCallback([]{
        static Timer tTestInner;

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
        scheduler->PrepareWindowSchedule(0, 0);

        tTestInner.SetCallback([id]{
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

            bool testOk = AssertSchedule(title, scheduler->GetMarkList(), expectedList);
            scheduler->DestroyMarkList(id);

            LogNL();
            string result = string{"=== Test "} + (testOk ? "" : "NOT ") + "ok " + title + " ===";
            testResultList.push_back(result);
            Log(result);
            LogNL();
        });
        tTestInner.TimeoutInMs(INNER_DELAY_MS);
    });
    tTestOuter.TimeoutInMs(NextTestDuration());
}





///////////////////////////////////////////////////////////////////////////////
// Tests with bad javascript
///////////////////////////////////////////////////////////////////////////////


// default behavior, with gps lock, bad js
void TestDefaultWithGpsBadJs()
{
    static Timer tTestOuter;
    tTestOuter.SetCallback([]{
        static Timer tTestInner;

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
        scheduler->PrepareWindowSchedule(0, 0);

        tTestInner.SetCallback([id]{
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

            bool testOk = AssertSchedule(title, scheduler->GetMarkList(), expectedList);
            scheduler->DestroyMarkList(id);

            LogNL();
            string result = string{"=== Test "} + (testOk ? "" : "NOT ") + "ok " + title + " ===";
            testResultList.push_back(result);
            Log(result);
            LogNL();
        });
        tTestInner.TimeoutInMs(INNER_DELAY_MS);
    });
    tTestOuter.TimeoutInMs(NextTestDuration());
}


// default behavior, with no gps lock, bad js
void TestDefaultNoGpsBadJs()
{
    static Timer tTestOuter;
    tTestOuter.SetCallback([]{
        static Timer tTestInner;

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
        scheduler->PrepareWindowSchedule(0, 0);

        tTestInner.SetCallback([id]{
            string title = JustFunctionName(source_location::current().function_name());

            scheduler->SetTesting(false);

            vector<string> expectedList = {
                "TX_DISABLE_GPS_ENABLE",
                "JS_EXEC",               "SEND_NO_MSG_NONE",    // slot 1
                "JS_EXEC",               "SEND_NO_MSG_NONE",    // slot 2
                "JS_EXEC",               "SEND_NO_MSG_NONE",    // slot 3
                "JS_EXEC",               "SEND_NO_MSG_NONE",    // slot 4
                "JS_EXEC",               "SEND_NO_MSG_NONE",    // slot 5
            };

            bool testOk = AssertSchedule(title, scheduler->GetMarkList(), expectedList, false);
            scheduler->DestroyMarkList(id);

            LogNL();
            string result = string{"=== Test "} + (testOk ? "" : "NOT ") + "ok " + title + " ===";
            testResultList.push_back(result);
            Log(result);
            LogNL();
        });
        tTestInner.TimeoutInMs(INNER_DELAY_MS);
    });
    tTestOuter.TimeoutInMs(NextTestDuration());
}


// all override, with gps lock, bad js
void TestAllOverrideWithGpsBadJs()
{
    static Timer tTestOuter;
    tTestOuter.SetCallback([]{
        static Timer tTestInner;

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
        scheduler->PrepareWindowSchedule(0, 0);

        tTestInner.SetCallback([id]{
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

            bool testOk = AssertSchedule(title, scheduler->GetMarkList(), expectedList);
            scheduler->DestroyMarkList(id);

            LogNL();
            string result = string{"=== Test "} + (testOk ? "" : "NOT ") + "ok " + title + " ===";
            testResultList.push_back(result);
            Log(result);
            LogNL();
        });
        tTestInner.TimeoutInMs(INNER_DELAY_MS);
    });
    tTestOuter.TimeoutInMs(NextTestDuration());
}


// all custom messages need gps, with no gps lock, bad js
void TestAllCustomMessagesNeedGpsWithNoGpsBadJs()
{
    static Timer tTestOuter;
    tTestOuter.SetCallback([]{
        static Timer tTestInner;

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
        scheduler->PrepareWindowSchedule(0, 0);

        tTestInner.SetCallback([id]{
            string title = JustFunctionName(source_location::current().function_name());

            scheduler->SetTesting(false);

            vector<string> expectedList = {
                "TX_DISABLE_GPS_ENABLE",
                "JS_NO_EXEC",               "SEND_NO_MSG_NONE",    // slot 1
                "JS_NO_EXEC",               "SEND_NO_MSG_NONE",    // slot 2
                "JS_NO_EXEC",               "SEND_NO_MSG_NONE",    // slot 3
                "JS_NO_EXEC",               "SEND_NO_MSG_NONE",    // slot 4
                "JS_NO_EXEC",               "SEND_NO_MSG_NONE",    // slot 5
            };

            bool testOk = AssertSchedule(title, scheduler->GetMarkList(), expectedList, false);
            scheduler->DestroyMarkList(id);

            LogNL();
            string result = string{"=== Test "} + (testOk ? "" : "NOT ") + "ok " + title + " ===";
            testResultList.push_back(result);
            Log(result);
            LogNL();
        });
        tTestInner.TimeoutInMs(INNER_DELAY_MS);
    });
    tTestOuter.TimeoutInMs(NextTestDuration());
}


// some custom messages need gps, others don't, with gps lock, bad js
void TestSomeCustomMessagesNeedGpsSomeDontWithGpsBadJs()
{
    static Timer tTestOuter;
    tTestOuter.SetCallback([]{
        static Timer tTestInner;

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
        scheduler->PrepareWindowSchedule(0, 0);

        tTestInner.SetCallback([id]{
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

            bool testOk = AssertSchedule(title, scheduler->GetMarkList(), expectedList);
            scheduler->DestroyMarkList(id);

            LogNL();
            string result = string{"=== Test "} + (testOk ? "" : "NOT ") + "ok " + title + " ===";
            testResultList.push_back(result);
            Log(result);
            LogNL();
        });
        tTestInner.TimeoutInMs(INNER_DELAY_MS);
    });
    tTestOuter.TimeoutInMs(NextTestDuration());
}



// some custom messages need gps, others don't, with no gps lock.
// expect to see earlier gps enable as a result of not sending the
// custom message that depends on gps having a lock this time.
// bad js
void TestSomeCustomMessagesNeedGpsSomeDontNoGpsBadJs()
{
    static Timer tTestOuter;
    tTestOuter.SetCallback([]{
        static Timer tTestInner;

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
        scheduler->PrepareWindowSchedule(0, 0);

        tTestInner.SetCallback([id]{
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

            bool testOk = AssertSchedule(title, scheduler->GetMarkList(), expectedList);
            scheduler->DestroyMarkList(id);

            LogNL();
            string result = string{"=== Test "} + (testOk ? "" : "NOT ") + "ok " + title + " ===";
            testResultList.push_back(result);
            Log(result);
            LogNL();
        });
        tTestInner.TimeoutInMs(INNER_DELAY_MS);
    });
    tTestOuter.TimeoutInMs(NextTestDuration());
}


void TestOverrideBasicTelemetryButBadJs()
{
    static Timer tTestOuter;
    tTestOuter.SetCallback([]{
        static Timer tTestInner;

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
        scheduler->PrepareWindowSchedule(0, 0);

        tTestInner.SetCallback([id]{
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

            bool testOk = AssertSchedule(title, scheduler->GetMarkList(), expectedList);
            scheduler->DestroyMarkList(id);

            LogNL();
            string result = string{"=== Test "} + (testOk ? "" : "NOT ") + "ok " + title + " ===";
            testResultList.push_back(result);
            Log(result);
            LogNL();
        });
        tTestInner.TimeoutInMs(INNER_DELAY_MS);
    });
    tTestOuter.TimeoutInMs(NextTestDuration());
}


void TestOverrideBasicTelemetryNoGpsButBadJs()
{
    static Timer tTestOuter;
    tTestOuter.SetCallback([]{
        static Timer tTestInner;

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
        scheduler->PrepareWindowSchedule(0, 0);

        tTestInner.SetCallback([id]{
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

            bool testOk = AssertSchedule(title, scheduler->GetMarkList(), expectedList);
            scheduler->DestroyMarkList(id);

            LogNL();
            string result = string{"=== Test "} + (testOk ? "" : "NOT ") + "ok " + title + " ===";
            testResultList.push_back(result);
            Log(result);
            LogNL();
        });
        tTestInner.TimeoutInMs(INNER_DELAY_MS);
    });
    tTestOuter.TimeoutInMs(NextTestDuration());
}


void CopilotControlScheduler::TestPrepareWindowSchedule()
{
    scheduler = this;

    BackupFiles();

    Log("TestPrepareWindowSchedule Start");
    ResetTestDuration();

    SetUseMarkList(true);
    ClearTestResultList();


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
        static Timer tRestore;
        tRestore.SetCallback([this]{
            Log(testResultList.size(), " tests run");
            for (const auto &result : testResultList)
            {
                Log(result);
            }
            LogNL();

            ClearTestResultList();
            SetUseMarkList(false);

            RestoreFiles();
        });
        tRestore.TimeoutInMs(NextTestDuration());
    }
}




































///////////////////////////////////////////////////////////////////////////////
// TestConfigureWindowSlotBehavior
///////////////////////////////////////////////////////////////////////////////


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























///////////////////////////////////////////////////////////////////////////////
// TestCalculateTimeAtWindowStartUs
///////////////////////////////////////////////////////////////////////////////





void CopilotControlScheduler::TestCalculateTimeAtWindowStartUs(bool fullSweep)
{
    auto GpsTime = [](uint8_t gpsMin, uint8_t gpsSec, uint32_t gpsUs){
        string time = Time::MakeDateTime(0, gpsMin, gpsSec, gpsUs);
        time = time.substr(14);         // get rid of leading date and hour
        time.resize(time.size() - 3);   // get rid of microseconds, leave just ms
        return time;
    };

    auto Test = [&](uint8_t windowStartMin, uint8_t gpsMin, uint8_t gpsSec, uint32_t gpsMs, bool verbose = true) {
        uint64_t timeAtWindowStartUs = CalculateTimeAtWindowStartUs(windowStartMin, gpsMin, gpsSec, gpsMs * 1'000, 0);

        if (verbose)
        {
            string windowTimeTarget = string{" "} + to_string(windowStartMin) + ":01.000";
            string gpsTime          = GpsTime(gpsMin, gpsSec, gpsMs * 1'000);

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
    Timeline::Measure([&](auto &t){
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











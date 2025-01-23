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
        // peek first element from subset
        string subsetElement = *subsetElementList.begin();

        // remove non-matching elements from superset
        while (supersetElementList.size() && subsetElement != *supersetElementList.begin())
        {
            supersetElementList.erase(supersetElementList.begin());
        }

        // remove the element you just compared equal to (if exists)
        if (supersetElementList.size())
        {
            // remove from both subset and superset
            supersetElementList.erase(supersetElementList.begin());
            subsetElementList.erase(subsetElementList.begin());
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
// TestGpsEventInterface
///////////////////////////////////////////////////////////////////////////////


// expected is a subset of actual, but all elements need to be found
// in the order expressed for it to be a match
static auto AssertGpsEvents = [](string title, vector<string> actualList, vector<string> expectedList){
    bool retVal = true;

    vector<string> actualListCpy = actualList;

    Log("Comparing expected");
    for (const auto &str : expectedList)
    {
        Log("  ", str);
    }

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








class GpsEventsTestBuilder
{
public:
    GpsEventsTestBuilder(TimerSequence &ts, const char *fnName)
    : ts_(ts)
    , fnName_(fnName)
    {
        MakeTestEventStart();
    }


    GpsEventsTestBuilder &DoStart()
    {
        ts_.Add([]{
            scheduler->Start();
        });

        AddExpectedEventList({
            "REQ_NEW_GPS_LOCK",
        });

        return *this;
    }





    GpsEventsTestBuilder &DoLock3DPlusReqOnLockoutNo(const char *dateTime)
    {
        ts_.Add([=]{ scheduler->OnGps3DPlusLock(MakeFix3DPlus(dateTime)); });

        AddExpectedEventList({
            "ON_GPS_LOCK_3D_PLUS_APPLIED",
            "APPLY_TIME_AND_UPDATE_SCHEDULE",
            "COAST_CANCELED",
            "PREPARE_WINDOW_SCHEDULE_START",
        });

        return *this;
    }

    GpsEventsTestBuilder &DoLock3DPlusReqOnLockoutOn(const char *dateTime)
    {
        ts_.Add([=]{ scheduler->OnGps3DPlusLock(MakeFix3DPlus(dateTime)); });
        AddExpectedEvent("ON_GPS_LOCK_3D_PLUS_CACHED");

        return *this;
    }

    GpsEventsTestBuilder &DoLock3DPlusReqNoLockoutNo(const char *dateTime)
    {
        ts_.Add([=]{ scheduler->OnGps3DPlusLock(MakeFix3DPlus(dateTime)); });
        AddExpectedEvent("ON_GPS_LOCK_3D_PLUS_REQ_NO_LOCKOUT_NO");

        return *this;
    }

    GpsEventsTestBuilder &DoLock3DPlusReqNoLockoutOn(const char *dateTime)
    {
        ts_.Add([=]{ scheduler->OnGps3DPlusLock(MakeFix3DPlus(dateTime)); });
        AddExpectedEvent("ON_GPS_LOCK_3D_PLUS_REQ_NO_LOCKOUT_ON");

        return *this;
    }



    GpsEventsTestBuilder &DoLockOnTimeReqOnLockoutNo(const char *dateTime)
    {
        ts_.Add([=]{ scheduler->OnGpsTimeLock(MakeFixTime(dateTime)); });

        AddExpectedEventList({
            "ON_GPS_LOCK_TIME_APPLIED",
            "APPLY_TIME_AND_UPDATE_SCHEDULE",
            "COAST_SCHEDULED",
        });

        return *this;
    }

    GpsEventsTestBuilder &DoLockOnTimeReqOnLockoutOn(const char *dateTime)
    {
        ts_.Add([=]{ scheduler->OnGpsTimeLock(MakeFixTime(dateTime)); });
        AddExpectedEvent("ON_GPS_LOCK_TIME_CACHED");

        return *this;
    }

    GpsEventsTestBuilder &DoLockOnTimeReqNoLockoutNo(const char *dateTime)
    {
        ts_.Add([=]{ scheduler->OnGpsTimeLock(MakeFixTime(dateTime)); });
        AddExpectedEvent("ON_GPS_LOCK_TIME_REQ_NO_LOCKOUT_NO");

        return *this;
    }

    GpsEventsTestBuilder &DoLockOnTimeReqNoLockoutOn(const char *dateTime)
    {
        ts_.Add([=]{ scheduler->OnGpsTimeLock(MakeFixTime(dateTime)); });
        AddExpectedEvent("ON_GPS_LOCK_TIME_REQ_NO_LOCKOUT_ON");

        return *this;
    }






    GpsEventsTestBuilder &AddExpectedWindowLockoutStartEvent()
    {
        AddExpectedEvent("SCHEDULE_LOCK_OUT_START");

        return *this;
    }

    GpsEventsTestBuilder &AddExpectedWindowLockoutEndEvent()
    {
        AddExpectedEvent("SCHEDULE_LOCK_OUT_END");

        return *this;
    }

    GpsEventsTestBuilder &AddExpectedWindowLockoutStartEndEvents()
    {
        AddExpectedWindowLockoutStartEvent();
        AddExpectedWindowLockoutEndEvent();

        return *this;
    }

    GpsEventsTestBuilder &AddExpectedEvent(string str)
    {
        AddExpectedEventList({ str });

        return *this;
    }

    GpsEventsTestBuilder &AddExpectedEventList(initializer_list<string> stringList)
    {
        expectedList_.insert(expectedList_.end(), stringList);

        return *this;
    }


    TimerSequence &Finish()
    {
        MakeTestEventEnd();

        return ts_;
    }



    GpsEventsTestBuilder &DelayMs(uint64_t durationMs)
    {
        ts_.DelayMs(durationMs);

        return *this;
    }

    GpsEventsTestBuilder &StartAtUs(uint64_t timeAtUs)
    {
        ts_.StartAtUs(timeAtUs);

        return *this;
    }

    GpsEventsTestBuilder &StartAtUs(function<uint64_t()> fn)
    {
        ts_.StartAtUs(fn);

        return *this;
    }



private:

    void MakeTestEventStart()
    {
        ts_.Add([fnName=fnName_]{
            LogNL();
            LogNL();
            Log("==============================================");
            Log("Test ", fnName, "() pre-start: ", Time::MakeTimeFromUs(PAL.Micros()));
            LogNL();

            scheduler->SetTesting(true);
            scheduler->CreateMarkList(id);
        });
    }

    void MakeTestEventEnd()
    {
        ts_.Add([fnName=fnName_, expectedList=expectedList_]{
            scheduler->Stop();
            scheduler->SetTesting(false);

            Log("Test post-stop: ", Time::MakeTimeFromUs(PAL.Micros()));

            bool testOk = AssertGpsEvents(fnName, scheduler->GetMarkList(), expectedList);
            scheduler->DestroyMarkList(id);

            LogNL();
            uint64_t timeNowUs = PAL.Micros();
            string result = string{"["} + Time::GetNotionalTimeAtSystemUs(timeNowUs) + ", " + Time::MakeTimeFromUs(timeNowUs) + "] === Test " + (testOk ? "" : "NOT ") + "ok " + fnName + "() ===";
            testResultList.push_back(result);
            Log(result);
            Log("==============================================");
            LogNL();
        });
    }


private:

    vector<string> expectedList_;

    TimerSequence &ts_;
    const char *fnName_;
};


/////////////////////////////////////////////////
// Test Start.
/////////////////////////////////////////////////

void TestGpsEventsStart(TimerSequence &ts)
{
    GpsEventsTestBuilder test(ts, __func__);
    test.DoStart();
    test.Finish();
}


/////////////////////////////////////////////////
// Test Apply GPS period.
/////////////////////////////////////////////////

void TestGpsEventsApplyTime(TimerSequence &ts)
{
    GpsEventsTestBuilder test(ts, __func__);
    test.DoStart();
    test.DoLockOnTimeReqOnLockoutNo("2025-01-01 12:10:00.400"); // +200ms = 00.600
    test.AddExpectedEvent("COAST_TRIGGERED");
    test.AddExpectedWindowLockoutStartEndEvents();
    test.AddExpectedEvent("APPLY_CACHE_OLD_TIME");   // next window
    test.DelayMs(1'100);
    test.Finish();
}

void TestGpsEventsApplyTimeTime(TimerSequence &ts)
{
    GpsEventsTestBuilder test(ts, __func__);
    test.DoStart();
    test.DoLockOnTimeReqOnLockoutNo("2025-01-01 12:10:00.200"); // +200ms = 00.400
    test.DoLockOnTimeReqOnLockoutNo("2025-01-01 12:10:00.400"); // +100ms = 00.600
    test.AddExpectedEvent("COAST_TRIGGERED");
    test.AddExpectedWindowLockoutStartEndEvents();
    test.AddExpectedEvent("APPLY_CACHE_OLD_TIME");   // next window
    test.DelayMs(1'100);
    test.Finish();
}

void TestGpsEventsApply3d(TimerSequence &ts)
{
    GpsEventsTestBuilder test(ts, __func__);
    test.DoStart();
    test.DoLock3DPlusReqOnLockoutNo("2025-01-01 12:10:00.100"); // +400ms = 00.500
    test.AddExpectedWindowLockoutStartEndEvents();
    test.AddExpectedEvent("APPLY_CACHE_OLD_3D_PLUS");   // next window
    test.DelayMs(1'400);
    test.Finish();
}

void TestGpsEventsApplyTime3d(TimerSequence &ts)
{
    GpsEventsTestBuilder test(ts, __func__);
    test.DoStart();
    test.DoLockOnTimeReqOnLockoutNo("2025-01-01 12:10:00.000"); // +200ms = 00.400
    test.DoLock3DPlusReqOnLockoutNo("2025-01-01 12:10:00.200"); // +400ms = 00.600
    test.AddExpectedWindowLockoutStartEndEvents();
    test.AddExpectedEvent("APPLY_CACHE_OLD_3D_PLUS");   // next window
    test.DelayMs(1'300);
    test.Finish();
}


/////////////////////////////////////////////////
// Test Ignore GPS period.
/////////////////////////////////////////////////

void TestGpsEventsApply3dIgnoreTime(TimerSequence &ts)
{
    GpsEventsTestBuilder test(ts, __func__);
    test.DoStart();
    test.DoLock3DPlusReqOnLockoutNo("2025-01-01 12:10:00.100"); // +400ms = 00.400
    test.DoLockOnTimeReqNoLockoutNo("2025-01-01 12:10:00.500");
    test.AddExpectedWindowLockoutStartEndEvents();
    test.AddExpectedEvent("APPLY_CACHE_OLD_3D_PLUS");   // next window
    test.DelayMs(1'400);
    test.Finish();
}

void TestGpsEventsApply3dIgnore3d(TimerSequence &ts)
{
    GpsEventsTestBuilder test(ts, __func__);
    test.DoStart();
    test.DoLock3DPlusReqOnLockoutNo("2025-01-01 12:10:00.100"); // +400ms = 00.400
    test.DoLock3DPlusReqNoLockoutNo("2025-01-01 12:10:00.500");
    test.AddExpectedWindowLockoutStartEndEvents();
    test.AddExpectedEvent("APPLY_CACHE_OLD_3D_PLUS");   // next window
    test.DelayMs(1'400);
    test.Finish();
}


/////////////////////////////////////////////////
// Test Cache GPS period.
/////////////////////////////////////////////////

void TestGpsEventsApplyTimeCacheTime(TimerSequence &ts)
{
    GpsEventsTestBuilder test(ts, __func__);
    test.DoStart();
    test.DoLockOnTimeReqOnLockoutNo("2025-01-01 12:10:00.400"); // +200ms = 00.600
    test.DelayMs(300);                                          // +300ms = 00.700 (within target)
    test.AddExpectedWindowLockoutStartEvent();
    test.DoLockOnTimeReqOnLockoutOn("2025-01-01 12:16:00.500")
        .StartAtUs([]{
            uint64_t timeAtUs = scheduler->timerTxDisableGpsEnable_.GetTimeoutAtUs();

            Log("=============");
            Log("Notional time currently: ", Time::GetNotionalTime());
            Log("Timer notional time set: ", Time::GetNotionalTimeAtSystemUs(timeAtUs));
            Log("=============");

            return timeAtUs;
        });
    test.AddExpectedWindowLockoutEndEvent();
    test.AddExpectedEvent("APPLY_CACHE_NEW_TIME");   // next window
    test.DelayMs(1'600);
    test.Finish();
}





















void CopilotControlScheduler::TestGpsEventInterface(vector<string> testNameList)
{
    scheduler = this;
    scheduler->Stop();

    Log("TestGpsEventInterface Start");
    LogNL();

    BackupFiles();

    SetUseMarkList(true);
    ClearTestResultList();


    // setup a definite testing environment.
    // it's not important, specifically, though, since we're only interested in
    // events which occur regarding gps relating to the window and next window
    // scheduling.
    // better to have determinism, though.
    SetStartMinute(0);
    SetSlot("slot1", msgDefBlank, jsUsesNeither);
    SetSlot("slot2", msgDefBlank, jsUsesNeither);
    SetSlot("slot3", msgDefBlank, jsUsesNeither);
    SetSlot("slot4", msgDefBlank, jsUsesNeither);
    SetSlot("slot5", msgDefBlank, jsUsesNeither);
    bool hasGpsLock = true;
    PrepareWindowSlotBehavior(hasGpsLock);
    SetTestingCalculateSlotBehaviorDisabled(true);
    SetTestingJsDisabled(true);

    // Set up timer sequence
    TimerSequence ts;

    auto Run = [&](const string &testName){
        return find(testNameList.begin(), testNameList.end(), testName) != testNameList.end();
    };



    // The tests to run.
    //
    // Tests are designed to probe the behavior of the GPS events, and how
    // they relate to the phases of operation regarding what to do with
    // GPS data.
    //
    // The phases are:
    // - Apply
    // - Ignore
    // - Cache
    //
    // Apply and Cache are when there is an active GPS Req.
    // Ignore is outside of an active GPS Req.
    //
    // Apply is when there is no 3d lock.
    // Upon 3d lock, the Req is revoked.
    //
    // Cache is when there is no 3d lock, but still within the lockout period
    // (where the window is operating the radio).
    //
    //
    // 
    // Tests will also check the operation of one operational cycle to the
    // next, ie coast via old time, coast via new time, and new 3d lock.
    // 


    //
    // Test scenario scheduling:
    //
    // Tests must work at 6MHz. Helps evaluate code performance at real runtime.
    // This means running them more slowly than they could go at 125MHz.
    //
    // Scheduling to work around:
    // 00.600 - coast starts
    // 00.900   - coast scheduling complete (synchronous)
    // 01.000 - window starts (warmup and js pushed inside window during test)
    // 01.400 - window completes
    // 01.800   - cache applied, coast timer set (synchronous)
    //
    //
    // Testing activity durations (safe margins):
    // - applying a 3d   lock takes 400ms (including all scheduling, etc)
    // - caching  a 3d   lock takes  20ms
    // - applying a time lock takes 200ms (fast because it only sets up coast)
    // - caching  a time lock takes  20ms
    //
    //
    // Ok to jump into the timeline at coast start and let the timer backlog
    // execute you as soon as possible.
    //
    // You should target 01.500 to be beyond the window end.
    //
    // Assume the gps time being fed in is taken instantaneously, and time
    // your jump to 00.500 from there.
    // eg apply time of 00.500, set a 1-sec timer to get to 01.500.



    // Test Start.
    if (Run("start") || Run("all"))
    {
        TestGpsEventsStart(ts);
    }

    // Test Apply GPS period.
    // This allows us to test initially starting, then taking in gps events which happen
    // in the Apply period. The (time) event does not take us out, but (3d) does.
    if (Run("apply") || Run("all"))
    {
        TestGpsEventsApplyTime(ts);
        TestGpsEventsApplyTimeTime(ts);
        TestGpsEventsApply3d(ts);
        TestGpsEventsApplyTime3d(ts);
    }

    // Test Ignore GPS period.
    // We know from prior tests that a 3d lock will put us into the Ignore period
    // before lockout. Test GPS events in that Ignore period.
    if (Run("ignore") || Run("all"))
    {
        TestGpsEventsApply3dIgnoreTime(ts);
        TestGpsEventsApply3dIgnore3d(ts);
    }

    // Test Cache GPS period.
    if (Run("cache") || Run("all"))
    {
        TestGpsEventsApplyTimeCacheTime(ts);
    }


    // TestGpsEventsApplyTimeCacheTimeTime(ts);
    // TestGpsEventsApplyTimeCache3d(ts);
    // TestGpsEventsApplyTimeCache3d3d(ts);
    // TestGpsEventsApplyTimeCacheTime3d(ts);
    // TestGpsEventsApplyTimeCache3dTime(ts);

    // TestGpsEventsApply3dCacheTime(ts);
    // TestGpsEventsApply3dCacheTimeTime(ts);
    // TestGpsEventsApply3dCache3d(ts);
    // TestGpsEventsApply3dCache3d3d(ts);
    // TestGpsEventsApply3dCacheTime3d(ts);
    // TestGpsEventsApply3dCache3dTime(ts);







    // Coast forever?
    // In first set of tests, we did (start, time), what is the long-lasting effect of that?
    // We didn't test.
    // How to test that either time or 3d lock gets used in the next window, and all
    // windows beyond that?
    // Need to test that prior time works indefinitely for both (time) and (3d).




    uint64_t timeStartUs = PAL.Micros();

    // Complete
    ts.Add([this, timeStartUs]{
        uint64_t durationUs = PAL.Micros() - timeStartUs;

        SetTestingCalculateSlotBehaviorDisabled(false);
        SetTestingJsDisabled(false);

        Log(testResultList.size(), " tests run in ", Time::MakeDurationFromUs(durationUs));
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
    ts.Start();
    Evm::MainLoop();

    Log("TestGpsEventInterface Done");
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











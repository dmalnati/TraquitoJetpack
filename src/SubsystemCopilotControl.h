#pragma once

#include "CopilotControlConfiguration.h"
#include "CopilotControlScheduler.h"
#include "CopilotControlUtl.h"


class SubsystemCopilotControl
{
public:

    SubsystemCopilotControl()
    {
        CopilotControlConfiguration::SetupShell();
        CopilotControlConfiguration::SetupJSON();
    }

    CopilotControlScheduler &GetScheduler()
    {
        return ccs_;
    }


private:

    CopilotControlScheduler ccs_;
};
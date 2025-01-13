#pragma once

#include "CopilotControlConfiguration.h"
#include "CopilotControlScheduler.h"


class SubsystemCopilotControl
{
public:

    SubsystemCopilotControl()
    {
        CopilotControlConfiguration::SetupShell();
        CopilotControlConfiguration::SetupJSON();
    }

private:

    CopilotControlScheduler ccs_;
};
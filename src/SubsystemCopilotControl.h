#pragma once

#include "CopilotControlConfiguration.h"
#include "CopilotControlJavaScript.h"


class SubsystemCopilotControl
{
public:

    SubsystemCopilotControl()
    {
        CopilotControlConfiguration::SetupShell();
        CopilotControlConfiguration::SetupJSON();
    }


private:

    CopilotControlJavaScript ccjs_;
};
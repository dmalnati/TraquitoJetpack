#pragma once

#include "WsprMessageTelemetryExtendedUserDefined.h"

#include <string>
#include <vector>
using namespace std;


template <uint8_t FIELD_COUNT = 29>
class WsprMessageTelemetryExtendedUserDefinedDynamic
: public WsprMessageTelemetryExtendedUserDefined<FIELD_COUNT>
{
    using Base = WsprMessageTelemetryExtendedUserDefined<FIELD_COUNT>;

public:

    WsprMessageTelemetryExtendedUserDefinedDynamic()
    {
        fieldList_.reserve(FIELD_COUNT);
    }

    void ResetEverything()
    {
        Base::ResetEverything();

        fieldList_.clear();
    }

    bool DefineField(const char *fieldName,
                     double      lowValue,
                     double      highValue,
                     double      stepSize)
    {
        fieldList_.push_back(fieldName);

        bool ok = true;

        {
            string &fieldNameDynamic = fieldList_[fieldList_.size() - 1];

            ok = Base::DefineField(fieldNameDynamic.c_str(), lowValue, highValue, stepSize);
        }

        if (ok == false)
        {
            fieldList_.pop_back();
        }

        return ok;
    }

    const vector<string> &GetFieldList()
    {
        return fieldList_;
    }


private:

    vector<string> fieldList_;
};
#include "CommandHandlerStringUtils.h"
#include "ICH_PowerJunction.h"

namespace AIXO {

FString CommandHandlerStringUtils::MakeCommandHandlerString(ICommandHandler* Handler)
{
    FString str;
    ICommandHandler* FoundHandler = Handler;
    if (FoundHandler)
    {
        const ICH_PowerJunction* pj = FoundHandler->GetAsPowerJunction();
        str += "**SYSTEM:" + FoundHandler->GetSystemName() + "\n";
        if (pj && pj->IsPowerSource()) str += "POWER_SOURCE:true\n";
        else str += "POWER_SOURCE:false\n";
        if (pj) {
            switch (pj->GetStatus()) {
                case EPowerJunctionStatus::NORMAL:
                    str += "STATUS:NORMAL\n";
                    break;
                case EPowerJunctionStatus::DAMAGED50:
                    str += "STATUS:50% DAMAGED\n";
                    break;
                case EPowerJunctionStatus::DAMAGED100:
                    str += "STATUS:DAMAGED\n";
                    break;
                case EPowerJunctionStatus::DESTROYED:
                    str += "STATUS:DESTROYED\n";
                    break;
            }
            if (pj->IsPowerSource()) {
                str += FString::Printf(TEXT("POWER_AVAILABLE:%g\n"), pj->GetPowerAvailable());
            } else {
                str += FString::Printf(TEXT("POWER_USAGE:%g\n"), pj->GetCurrentPowerUsage());
            }
            str += FString::Printf(TEXT("NOISE:%g\n"), pj->GetCurrentNoiseLevel());
        }
        FString gd = FoundHandler->GetSystemGuidance();
        if (gd.Len() > 0) str += "GUIDANCE: " + gd + "\n";
        FString st = FoundHandler->GetSystemStatus();
        if (st.Len() > 0) str += "STATUS: " + st + "\n";
        TArray<FString> cm = FoundHandler->GetAvailableCommands();
        if (cm.Num() > 0) {
            str += "*COMMANDS:\n";
            for (FString& s : cm) str += s + "\n";
        }
        TArray<FString> qr = FoundHandler->GetAvailableQueries();
        if (qr.Num() > 0) {
            str += "*QUERIES:";
            for (FString& s : qr) str += " " + s;
            str += "\n";
        }
    }
    return str;
}

} // namespace AIXO 
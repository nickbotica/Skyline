#include "EuroScopePlugIn.h"
#include <array>

#pragma once
class ESHelper
{
public:
	bool static isCorrelated(EuroScopePlugIn::CRadarTarget& rt);
	bool static isCorrelated(EuroScopePlugIn::CFlightPlan& fp);
	std::array<EuroScopePlugIn::CPosition, 5> static getPreviousPositions(EuroScopePlugIn::CRadarTarget& rt);
private:
	ESHelper() {}
};
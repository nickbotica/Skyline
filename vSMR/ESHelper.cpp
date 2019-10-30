#include "stdafx.h"
#include "ESHelper.h"

using namespace EuroScopePlugIn;

bool ESHelper::isCorrelated(CRadarTarget& rt)
{
	return (rt.GetCorrelatedFlightPlan().IsValid());
}

bool ESHelper::isCorrelated(CFlightPlan& fp)
{
	return (fp.GetCorrelatedRadarTarget().IsValid());
}

std::array<CPosition, 5> ESHelper::getPreviousPositions(CRadarTarget& rt)
{
	std::array<CPosition, 5> previousPositions{};

	auto previousRtPos = rt.GetPreviousPosition(rt.GetPosition());
	previousPositions[0] = previousRtPos.GetPosition();

	for (int i = 1; i < 5; i++)
	{
		previousRtPos = rt.GetPreviousPosition(previousRtPos);
		previousPositions[i] = previousRtPos.GetPosition();
	}

	return previousPositions;
}

#include "stdafx.h"
#include "Resource.h"#include "EuroScopePlugIn.h"
#include "ESHelper.h"
#include "SMRRadar.hpp"
#include "Symbol.h"

using namespace EuroScopePlugIn;

void Symbol::render(Graphics& graphics, CSMRRadar& radar, CRadarTarget& rt, Gdiplus::Color& activeColor)
{
	if (!rt.GetPosition().IsValid())
		return;

	//debug
	auto cs = rt.GetCallsign();
	double trackHeading = rt.GetTrackHeading();

	int reportedGs = rt.GetPosition().GetReportedGS();
	CRadarTargetPositionData rtPos = rt.GetPosition();
	Point acPosPix = radar.GetPoint(rtPos.GetPosition());

	bool AcisCorrelated = radar.IsCorrelated(radar.GetPlugIn()->FlightPlanSelect(rt.GetCallsign()), rt);

	if (!AcisCorrelated && reportedGs < 1 && !radar.ReleaseInProgress && !radar.AcquireInProgress)
		return;

	// Predicted Track Line
	if (reportedGs > 50)
	{
		Pen linePen(activeColor, 1.0F);

		auto distance = (static_cast<double>(rt.GetPosition().GetReportedGS() * KNOT_TO_MS))* (radar.PredictedLenght * 60) - 10;
		const CPosition predictedEnd = BetterHarversine(rt.GetPosition().GetPosition(), rt.GetTrackHeading(), distance);

		graphics.DrawLine(&linePen, radar.GetPoint(rt.GetPosition().GetPosition()), radar.GetPoint(predictedEnd));
	}

	radar.AddScreenObject(DRAWING_AC_SYMBOL, rt.GetCallsign(), { acPosPix.X - 5, acPosPix.Y - 5, acPosPix.X + 5, acPosPix.Y + 5 }, false, AcisCorrelated ? radar.GetBottomLine(rt.GetCallsign()).c_str() : rt.GetSystemID());


	// History trail
	if (reportedGs > 0)
	{
		auto previousPositions = ESHelper::getPreviousPositions(rt);

		for each (auto pos in previousPositions)
			drawHistoryPoint(graphics, activeColor, radar.GetPoint(pos));
	}

	// Radar target
	Pen symbolPen(activeColor, 2.0F);
	auto rflags = rtPos.GetRadarFlags();
	switch (rtPos.GetRadarFlags())
	{
	case RADAR_POSITION_NONE: //0
		break;
	case RADAR_POSITION_PRIMARY: //1
		drawPrimaryTarget(graphics, symbolPen, acPosPix);
		break;
	case RADAR_POSITION_PRIMARY + RADAR_POSITION_SECONDARY_C: //3
		drawPrimaryAndSecondayTarget(graphics, symbolPen, acPosPix);
		break;
	case RADAR_POSITION_ALL: // 7
		drawPrimaryAndSecondayTarget(graphics, symbolPen, acPosPix);
		break;
	default:
		//Secondary C, S and C + S (2, 4 and 6)
		drawSecondaryTarget(graphics, symbolPen, acPosPix);
		break;
	}
}

void Symbol::drawPrimaryTarget(Graphics& graphics, Pen& pen, Point acPos)
{
	Point square[]{
		Point(acPos.X + 5, acPos.Y - 5),
		Point(acPos.X + 5, acPos.Y + 5),
		Point(acPos.X - 5, acPos.Y + 5),
		Point(acPos.X - 5, acPos.Y - 5)
	};
		
	graphics.DrawPolygon(&pen, square, 4);
}

void Symbol::drawSecondaryTarget(Graphics& graphics, Pen& pen, Point acPos)
{
	graphics.DrawEllipse(&pen, acPos.X - 6, acPos.Y - 6, 12 , 12);
}

void Symbol::drawPrimaryAndSecondayTarget(Graphics& graphics, Pen& pen, Point acPos)
{
	Point pentagon[]{
		Point(acPos.X, acPos.Y - 6),
		Point(acPos.X + 6, acPos.Y - 2),
		Point(acPos.X + 3, acPos.Y + 6),
		Point(acPos.X - 3, acPos.Y + 6),
		Point(acPos.X - 6, acPos.Y - 2),
		Point(acPos.X, acPos.Y - 6)
	};
	graphics.DrawPolygon(&pen, pentagon, 6);
}

void Symbol::drawHistoryPoint(Graphics& graphics, Color& activeColor, Point point)
{
	Pen dotPen(activeColor, 1.0F);
	graphics.DrawRectangle(&dotPen, point.X, point.Y, 1, 1);
}
#include "stdafx.h"
#include "Resource.h"#include "EuroScopePlugIn.h"
#include "SMRRadar.hpp"
#include "Symbol.h"

using namespace EuroScopePlugIn;

void Symbol::render(Graphics* graphics, CSMRRadar* radar)
{
	CRadarTarget rt;
	for (rt = radar->GetPlugIn()->RadarTargetSelectFirst(); rt.IsValid(); rt = radar->GetPlugIn()->RadarTargetSelectNext(rt))
	{
		if (!rt.GetPosition().IsValid())
			return;

		int reportedGs = rt.GetPosition().GetReportedGS();
		CRadarTargetPositionData rtPos = rt.GetPosition();
		Point acPosPix = radar->GetPoint(rtPos.GetPosition());

		Color green(131, 255, 169);

		bool AcisCorrelated = radar->IsCorrelated(radar->GetPlugIn()->FlightPlanSelect(rt.GetCallsign()), rt);

		if (!AcisCorrelated && reportedGs < 1 && !radar->ReleaseInProgress && !radar->AcquireInProgress)
			return;

		auto previousSmoothingMode = graphics->GetSmoothingMode();
		graphics->SetSmoothingMode(SmoothingModeNone);

		// Predicted Track Line
		if (reportedGs > 50)
		{
			Pen linePen(green, 1.0F);

			auto distance = (static_cast<double>(rt.GetPosition().GetReportedGS() * KNOT_TO_MS)) * (radar->PredictedLenght * 60) - 10;
			const CPosition predictedEnd = BetterHarversine(rt.GetPosition().GetPosition(), rt.GetTrackHeading(), distance);

			graphics->DrawLine(&linePen, radar->GetPoint(rt.GetPosition().GetPosition()), radar->GetPoint(predictedEnd));
		}

		radar->AddScreenObject(DRAWING_AC_SYMBOL, rt.GetCallsign(), { acPosPix.X - 5, acPosPix.Y - 5, acPosPix.X + 5, acPosPix.Y + 5 }, false, AcisCorrelated ? radar->GetBottomLine(rt.GetCallsign()).c_str() : rt.GetSystemID());
		

		// History trail
		if (reportedGs > 0)
		{
			auto previousPositions = getPreviousPositions(rt);

			for each (auto pos in previousPositions)
				drawHistoryPoint(graphics, green, radar->GetPoint(pos));
		}

		//debug
		auto cs = rt.GetCallsign();
		auto radarFlags = rtPos.GetRadarFlags();
		auto modeC = rtPos.GetTransponderC();


		// Radar target
		Pen symbolPen(green, 2.0F);

		switch (rtPos.GetRadarFlags())
		{
		case RADAR_POSITION_NONE: //0
			break;
		case RADAR_POSITION_PRIMARY: //1
			drawPrimaryTarget(graphics, &symbolPen, acPosPix);
			break;
		case RADAR_POSITION_PRIMARY + RADAR_POSITION_SECONDARY_C: //3
			drawPrimaryAndSecondayTarget(graphics, &symbolPen, acPosPix);
			break;
		case RADAR_POSITION_ALL: // 7
			drawPrimaryAndSecondayTarget(graphics, &symbolPen, acPosPix);
			break;
		default:
			//Secondary C, S and C + S (2, 4 and 6)
			drawSecondaryTarget(graphics, &symbolPen, acPosPix);
			break;
		}

		graphics->SetSmoothingMode(previousSmoothingMode);
	}
}

void Symbol::drawPrimaryTarget(Graphics* graphics, Pen* pen, Point acPos)
{
	Point square[]{
		Point(acPos.X + 5, acPos.Y - 5),
		Point(acPos.X + 5, acPos.Y + 5),
		Point(acPos.X - 5, acPos.Y + 5),
		Point(acPos.X - 5, acPos.Y - 5)
	};
		
	graphics->DrawPolygon(pen, square, 4);
}


void Symbol::drawSecondaryTarget(Graphics* graphics, Pen* pen, Point acPos)
{
	graphics->DrawEllipse(pen, acPos.X - 6, acPos.Y - 6, 12 , 12);
}


void Symbol::drawPrimaryAndSecondayTarget(Graphics* graphics, Pen* pen, Point acPos)
{
	Point pentagon[]{
		Point(acPos.X, acPos.Y - 6),
		Point(acPos.X + 6, acPos.Y - 2),
		Point(acPos.X + 3, acPos.Y + 6),
		Point(acPos.X - 3, acPos.Y + 6),
		Point(acPos.X - 6, acPos.Y - 2),
		Point(acPos.X, acPos.Y - 6)
	};
	graphics->DrawPolygon(pen, pentagon, 6);
}

void Symbol::drawHistoryPoint(Graphics* graphics, Color color, Point point)
{
	Pen dotPen(color, 1.0F);
	graphics->DrawRectangle(&dotPen, point.X, point.Y, 1, 1);
}

std::array<CPosition, 5> Symbol::getPreviousPositions(CRadarTarget rt)
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
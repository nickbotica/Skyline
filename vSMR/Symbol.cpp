#include "stdafx.h"
#include "Resource.h"#include "EuroScopePlugIn.h"
#include "SMRRadar.hpp"
#include "Symbol.h"

using namespace EuroScopePlugIn;

void Symbol::render(CDC& dc, Graphics& graphics, CSMRRadar* radar)
{
	CRadarTarget rt;
	for (rt = radar->GetPlugIn()->RadarTargetSelectFirst(); rt.IsValid(); rt = radar->GetPlugIn()->RadarTargetSelectNext(rt))
	{

		if (!rt.GetPosition().IsValid())
			return;

		int reportedGs = rt.GetPosition().GetReportedGS();
		int radarRange = radar->CurrentConfig->getActiveProfile()["filters"]["radar_range_nm"].GetInt();
		int altitudeFilter = radar->CurrentConfig->getActiveProfile()["filters"]["hide_above_alt"].GetInt();
		int speedFilter = radar->CurrentConfig->getActiveProfile()["filters"]["hide_above_spd"].GetInt();
		bool isAcDisplayed = radar->isVisible(rt);

		if (!isAcDisplayed)
			return;

		//RimcasInstance->OnRefresh(rt, this, IsCorrelated(GetPlugIn()->FlightPlanSelect(rt.GetCallsign()), rt));

		CRadarTargetPositionData RtPos = rt.GetPosition();

		POINT acPosPix = radar->ConvertCoordFromPositionToPixel(RtPos.GetPosition());

		if (rt.GetGS() > 5) {
			POINT oldacPosPix;
			CRadarTargetPositionData pAcPos = rt.GetPosition();

			for (int i = 1; i <= 2; i++) {
				oldacPosPix = radar->ConvertCoordFromPositionToPixel(pAcPos.GetPosition());
				pAcPos = rt.GetPreviousPosition(pAcPos);
				acPosPix = radar->ConvertCoordFromPositionToPixel(pAcPos.GetPosition());

				if (i == 1 && !radar->Patatoides[rt.GetCallsign()].History_one_points.empty() && radar->Afterglow && radar->CurrentConfig->getActiveProfile()["targets"]["show_primary_target"].GetBool()) {
					SolidBrush H_Brush(radar->ColorManager->get_corrected_color("afterglow",
						radar->CurrentConfig->getConfigColor(radar->CurrentConfig->getActiveProfile()["targets"]["history_one_color"])));

					PointF lpPoints[100];
					for (unsigned int i1 = 0; i1 < radar->Patatoides[rt.GetCallsign()].History_one_points.size(); i1++)
					{
						CPosition pos;
						pos.m_Latitude = radar->Patatoides[rt.GetCallsign()].History_one_points[i1].x;
						pos.m_Longitude = radar->Patatoides[rt.GetCallsign()].History_one_points[i1].y;

						lpPoints[i1] = { REAL(radar->ConvertCoordFromPositionToPixel(pos).x), REAL(radar->ConvertCoordFromPositionToPixel(pos).y) };
					}
					graphics.FillPolygon(&H_Brush, lpPoints, radar->Patatoides[rt.GetCallsign()].History_one_points.size());
				}

				if (i != 2) {
					if (!radar->Patatoides[rt.GetCallsign()].History_two_points.empty() && radar->Afterglow && radar->CurrentConfig->getActiveProfile()["targets"]["show_primary_target"].GetBool()) {
						SolidBrush H_Brush(radar->ColorManager->get_corrected_color("afterglow",
							radar->CurrentConfig->getConfigColor(radar->CurrentConfig->getActiveProfile()["targets"]["history_two_color"])));

						PointF lpPoints[100];
						for (unsigned int i1 = 0; i1 < radar->Patatoides[rt.GetCallsign()].History_two_points.size(); i1++)
						{
							CPosition pos;
							pos.m_Latitude = radar->Patatoides[rt.GetCallsign()].History_two_points[i1].x;
							pos.m_Longitude = radar->Patatoides[rt.GetCallsign()].History_two_points[i1].y;

							lpPoints[i1] = { REAL(radar->ConvertCoordFromPositionToPixel(pos).x), REAL(radar->ConvertCoordFromPositionToPixel(pos).y) };
						}
						graphics.FillPolygon(&H_Brush, lpPoints, radar->Patatoides[rt.GetCallsign()].History_two_points.size());
					}
				}

				if (i == 2 && !radar->Patatoides[rt.GetCallsign()].History_three_points.empty() && radar->Afterglow && radar->CurrentConfig->getActiveProfile()["targets"]["show_primary_target"].GetBool()) {
					SolidBrush H_Brush(radar->ColorManager->get_corrected_color("afterglow",
						radar->CurrentConfig->getConfigColor(radar->CurrentConfig->getActiveProfile()["targets"]["history_three_color"])));

					PointF lpPoints[100];
					for (unsigned int i1 = 0; i1 < radar->Patatoides[rt.GetCallsign()].History_three_points.size(); i1++)
					{
						CPosition pos;
						pos.m_Latitude = radar->Patatoides[rt.GetCallsign()].History_three_points[i1].x;
						pos.m_Longitude = radar->Patatoides[rt.GetCallsign()].History_three_points[i1].y;

						lpPoints[i1] = { REAL(radar->ConvertCoordFromPositionToPixel(pos).x), REAL(radar->ConvertCoordFromPositionToPixel(pos).y) };
					}
					graphics.FillPolygon(&H_Brush, lpPoints, radar->Patatoides[rt.GetCallsign()].History_three_points.size());
				}
			}

			int TrailNumber = radar->Trail_Gnd;
			if (reportedGs > 50)
				TrailNumber = radar->Trail_App;

			CRadarTargetPositionData previousPos = rt.GetPreviousPosition(rt.GetPosition());
			for (int j = 1; j <= TrailNumber; j++) {
				POINT pCoord = radar->ConvertCoordFromPositionToPixel(previousPos.GetPosition());

				graphics.FillRectangle(&SolidBrush(radar->ColorManager->get_corrected_color("symbol", Gdiplus::Color::White)),
					pCoord.x - 1, pCoord.y - 1, 2, 2);

				previousPos = rt.GetPreviousPosition(previousPos);
			}
		}


		if (radar->CurrentConfig->getActiveProfile()["targets"]["show_primary_target"].GetBool()) {

			SolidBrush H_Brush(radar->ColorManager->get_corrected_color("afterglow",
				radar->CurrentConfig->getConfigColor(radar->CurrentConfig->getActiveProfile()["targets"]["target_color"])));

			PointF lpPoints[100];
			for (unsigned int i = 0; i < radar->Patatoides[rt.GetCallsign()].points.size(); i++)
			{
				CPosition pos;
				pos.m_Latitude = radar->Patatoides[rt.GetCallsign()].points[i].x;
				pos.m_Longitude = radar->Patatoides[rt.GetCallsign()].points[i].y;

				lpPoints[i] = { REAL(radar->ConvertCoordFromPositionToPixel(pos).x), REAL(radar->ConvertCoordFromPositionToPixel(pos).y) };
			}

			graphics.FillPolygon(&H_Brush, lpPoints, radar->Patatoides[rt.GetCallsign()].points.size());
		}
		acPosPix = radar->ConvertCoordFromPositionToPixel(RtPos.GetPosition());

		bool AcisCorrelated = radar->IsCorrelated(radar->GetPlugIn()->FlightPlanSelect(rt.GetCallsign()), rt);

		if (!AcisCorrelated && reportedGs < 1 && !radar->ReleaseInProgress && !radar->AcquireInProgress)
			return;

		CPen qTrailPen(PS_SOLID, 1, radar->ColorManager->get_corrected_color("symbol", Gdiplus::Color::White).ToCOLORREF());
		CPen* pqOrigPen = dc.SelectObject(&qTrailPen);

		if (RtPos.GetTransponderC()) {
			dc.MoveTo({ acPosPix.x, acPosPix.y - 6 });
			dc.LineTo({ acPosPix.x - 6, acPosPix.y });
			dc.LineTo({ acPosPix.x, acPosPix.y + 6 });
			dc.LineTo({ acPosPix.x + 6, acPosPix.y });
			dc.LineTo({ acPosPix.x, acPosPix.y - 6 });
		}
		else {
			dc.MoveTo(acPosPix.x, acPosPix.y);
			dc.LineTo(acPosPix.x - 4, acPosPix.y - 4);
			dc.MoveTo(acPosPix.x, acPosPix.y);
			dc.LineTo(acPosPix.x + 4, acPosPix.y - 4);
			dc.MoveTo(acPosPix.x, acPosPix.y);
			dc.LineTo(acPosPix.x - 4, acPosPix.y + 4);
			dc.MoveTo(acPosPix.x, acPosPix.y);
			dc.LineTo(acPosPix.x + 4, acPosPix.y + 4);
		}

		// Predicted Track Line
		// It starts 20 seconds away from the ac
		if (reportedGs > 50)
		{
			double d = double(rt.GetPosition().GetReportedGS() * 0.514444) * 10;
			CPosition AwayBase = BetterHarversine(rt.GetPosition().GetPosition(), rt.GetTrackHeading(), d);

			d = double(rt.GetPosition().GetReportedGS() * 0.514444) * (radar->PredictedLenght * 60) - 10;
			CPosition PredictedEnd = BetterHarversine(AwayBase, rt.GetTrackHeading(), d);

			dc.MoveTo(radar->ConvertCoordFromPositionToPixel(AwayBase));
			dc.LineTo(radar->ConvertCoordFromPositionToPixel(PredictedEnd));
		}

		/*
		if (mouseWithin({ acPosPix.x - 5, acPosPix.y - 5, acPosPix.x + 5, acPosPix.y + 5 })) {
			dc.MoveTo(acPosPix.x, acPosPix.y - 8);
			dc.LineTo(acPosPix.x - 6, acPosPix.y - 12);
			dc.MoveTo(acPosPix.x, acPosPix.y - 8);
			dc.LineTo(acPosPix.x + 6, acPosPix.y - 12);

			dc.MoveTo(acPosPix.x, acPosPix.y + 8);
			dc.LineTo(acPosPix.x - 6, acPosPix.y + 12);
			dc.MoveTo(acPosPix.x, acPosPix.y + 8);
			dc.LineTo(acPosPix.x + 6, acPosPix.y + 12);

			dc.MoveTo(acPosPix.x - 8, acPosPix.y);
			dc.LineTo(acPosPix.x - 12, acPosPix.y - 6);
			dc.MoveTo(acPosPix.x - 8, acPosPix.y);
			dc.LineTo(acPosPix.x - 12, acPosPix.y + 6);

			dc.MoveTo(acPosPix.x + 8, acPosPix.y);
			dc.LineTo(acPosPix.x + 12, acPosPix.y - 6);
			dc.MoveTo(acPosPix.x + 8, acPosPix.y);
			dc.LineTo(acPosPix.x + 12, acPosPix.y + 6);
		}
		*/

		radar->AddScreenObject(DRAWING_AC_SYMBOL, rt.GetCallsign(), { acPosPix.x - 5, acPosPix.y - 5, acPosPix.x + 5, acPosPix.y + 5 }, false, AcisCorrelated ? radar->GetBottomLine(rt.GetCallsign()).c_str() : rt.GetSystemID());

		dc.SelectObject(pqOrigPen);
	}
};

#include "stdafx.h"
#include "Resource.h"
#include "SMRRadar.hpp"
#include "Symbol.h"
#include "Tag.h"

using namespace EuroScopePlugIn;

ULONG_PTR m_gdiplusToken;
CPoint mouseLocation(0, 0);
string TagBeingDragged;
int LeaderLineDefaultlenght = 50;

//
// Cursor Things
//

bool initCursor = true;
HCURSOR smrCursor = NULL;
bool standardCursor; // switches between mouse cursor and pointer cursor when moving tags
bool customCursor; // use SMR version or default windows mouse symbol
WNDPROC gSourceProc;
HWND pluginWindow;
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

Color GetTagColor(EuroScopePlugIn::CRadarTarget& rt);

map<string, string> CSMRRadar::vStripsStands;

map<int, CInsetWindow *> appWindows;

inline double closest(std::vector<double> const& vec, double value) {
	auto const it = std::lower_bound(vec.begin(), vec.end(), value);
	if (it == vec.end()) { return -1; }

	return *it;
};
bool CSMRRadar::IsTagBeingDragged(string c)
{
	return TagBeingDragged == c;
}
bool CSMRRadar::mouseWithin(CRect rect) {
	if (mouseLocation.x >= rect.left + 1 && mouseLocation.x <= rect.right - 1 && mouseLocation.y >= rect.top + 1 && mouseLocation.y <= rect.bottom - 1)
		return true;
	return false;
}
bool CSMRRadar::mouseWithin(Rect rect) {
	if (mouseLocation.x >= rect.GetLeft() + 1 && mouseLocation.x <= rect.GetRight() - 1 && mouseLocation.y >= rect.GetTop() + 1 && mouseLocation.y <= rect.GetBottom() - 1)
		return true;
	return false;
}

CSMRRadar::CSMRRadar()
{

	Logger::info("CSMRRadar::CSMRRadar()");

	// Initializing randomizer
	srand(static_cast<unsigned>(time(nullptr)));

	// Initialize GDI+
	GdiplusStartupInput gdiplusStartupInput;
	GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, nullptr);

	// Getting the DLL file folder
	GetModuleFileNameA(HINSTANCE(&__ImageBase), DllPathFile, sizeof(DllPathFile));
	DllPath = DllPathFile;
	DllPath.resize(DllPath.size() - strlen("vSMR.dll"));

	Logger::info("Loading callsigns");
	// Loading up the callsigns for the bottom line
	// Search for ICAO airlines file if it already exists (usually given by the VACC)
	string AirlinesPath = DllPath;
	for (int i = 0; i < 3; ++i) {
		AirlinesPath = AirlinesPath.substr(0, AirlinesPath.find_last_of("/\\"));
	}
	AirlinesPath += "\\ICAO\\ICAO_Airlines.txt";

	ifstream f(AirlinesPath.c_str());

	if (f.good()) {
		Callsigns = new CCallsignLookup(AirlinesPath);
	}
	else {
		Callsigns = new CCallsignLookup(DllPath + "\\ICAO_Airlines.txt");
	}
	f.close();

	Logger::info("Loading RIMCAS & Config");
	// Creating the RIMCAS instance
	if (RimcasInstance == nullptr)
		RimcasInstance = new CRimcas();

	// Loading up the config file
	if (CurrentConfig == nullptr)
		CurrentConfig = new CConfig(DllPath + "\\vSMR_Profiles.json");

	if (ColorManager == nullptr)
		ColorManager = new CColorManager();

	standardCursor = true;	
	ActiveAirport = "LFPG";

	// Setting up the data for the 2 approach windows
	appWindowDisplays[1] = false;
	appWindowDisplays[2] = false;
	appWindows[1] = new CInsetWindow(APPWINDOW_ONE);
	appWindows[2] = new CInsetWindow(APPWINDOW_TWO);

	Logger::info("Loading profile");

	this->CSMRRadar::LoadProfile("Default");

	this->CSMRRadar::LoadCustomFont();

	this->CSMRRadar::RefreshAirportActivity();
}

CSMRRadar::~CSMRRadar()
{
	Logger::info(string(__FUNCSIG__));
	try {
		this->OnAsrContentToBeSaved();
		//this->EuroScopePlugInExitCustom();
	}
	catch (exception &e) {
		stringstream s;
		s << e.what() << endl;
		AfxMessageBox(string("Error occured " + s.str()).c_str());
	}
	// Shutting down GDI+
	GdiplusShutdown(m_gdiplusToken);
	delete CurrentConfig;
}

void CSMRRadar::LoadCustomFont() {
	Logger::info(string(__FUNCSIG__));
	// Loading the custom font if there is one in use
	customFonts.clear();

	const Value& FSizes = CurrentConfig->getActiveProfile()["font"]["sizes"];
	string font_name = CurrentConfig->getActiveProfile()["font"]["font_name"].GetString();
	wstring buffer = wstring(font_name.begin(), font_name.end());
	Gdiplus::FontStyle fontStyle = Gdiplus::FontStyleRegular;
	if (strcmp(CurrentConfig->getActiveProfile()["font"]["weight"].GetString(), "Bold") == 0)
		fontStyle = Gdiplus::FontStyleBold;
	if (strcmp(CurrentConfig->getActiveProfile()["font"]["weight"].GetString(), "Italic") == 0)
		fontStyle = Gdiplus::FontStyleItalic;

	customFonts[1] = new Gdiplus::Font(buffer.c_str(), Gdiplus::REAL(FSizes["one"].GetInt()), fontStyle, Gdiplus::UnitPixel);
	customFonts[2] = new Gdiplus::Font(buffer.c_str(), Gdiplus::REAL(FSizes["two"].GetInt()), fontStyle, Gdiplus::UnitPixel);
	customFonts[3] = new Gdiplus::Font(buffer.c_str(), Gdiplus::REAL(FSizes["three"].GetInt()), fontStyle, Gdiplus::UnitPixel);
	customFonts[4] = new Gdiplus::Font(buffer.c_str(), Gdiplus::REAL(FSizes["four"].GetInt()), fontStyle, Gdiplus::UnitPixel);
	customFonts[5] = new Gdiplus::Font(buffer.c_str(), Gdiplus::REAL(FSizes["five"].GetInt()), fontStyle, Gdiplus::UnitPixel);
}

void CSMRRadar::LoadProfile(string profileName) {
	Logger::info(string(__FUNCSIG__));
	// Loading the new profile
	CurrentConfig->setActiveProfile(profileName);

	// Loading all the new data
	const Value &RimcasTimer = CurrentConfig->getActiveProfile()["rimcas"]["timer"];
	const Value &RimcasTimerLVP = CurrentConfig->getActiveProfile()["rimcas"]["timer_lvp"];

	vector<int> RimcasNorm;
	for (SizeType i = 0; i < RimcasTimer.Size(); i++) {
		RimcasNorm.push_back(RimcasTimer[i].GetInt());
	}

	vector<int> RimcasLVP;
	for (SizeType i = 0; i < RimcasTimerLVP.Size(); i++) {
		RimcasLVP.push_back(RimcasTimerLVP[i].GetInt());
	}
	RimcasInstance->setCountdownDefinition(RimcasNorm, RimcasLVP);
	LeaderLineDefaultlenght = CurrentConfig->getActiveProfile()["labels"]["leader_line_length"].GetInt();

	customCursor = CurrentConfig->isCustomCursorUsed();

	// Reloading the fonts
	this->LoadCustomFont();
}

void CSMRRadar::OnAsrContentLoaded(bool Loaded)
{
	Logger::info(string(__FUNCSIG__));
	const char * p_value;

	if ((p_value = GetDataFromAsr("Airport")) != NULL)
		setActiveAirport(p_value);

	if ((p_value = GetDataFromAsr("ActiveProfile")) != NULL)
		this->LoadProfile(string(p_value));

	if ((p_value = GetDataFromAsr("FontSize")) != NULL)
		currentFontSize = atoi(p_value);

	if ((p_value = GetDataFromAsr("Afterglow")) != NULL)
		Afterglow = atoi(p_value) == 1 ? true : false;

	if ((p_value = GetDataFromAsr("AppTrailsDots")) != NULL)
		Trail_App = atoi(p_value);

	if ((p_value = GetDataFromAsr("GndTrailsDots")) != NULL)
		Trail_Gnd = atoi(p_value);

	if ((p_value = GetDataFromAsr("PredictedLine")) != NULL)
		PredictedLenght = atoi(p_value);

	string temp;

	for (int i = 1; i < 3; i++)
	{
		string prefix = "SRW" + std::to_string(i);

		if ((p_value = GetDataFromAsr(string(prefix + "TopLeftX").c_str())) != NULL)
			appWindows[i]->m_Area.left = atoi(p_value);

		if ((p_value = GetDataFromAsr(string(prefix + "TopLeftY").c_str())) != NULL)
			appWindows[i]->m_Area.top = atoi(p_value);

		if ((p_value = GetDataFromAsr(string(prefix + "BottomRightX").c_str())) != NULL)
			appWindows[i]->m_Area.right = atoi(p_value);

		if ((p_value = GetDataFromAsr(string(prefix + "BottomRightY").c_str())) != NULL)
			appWindows[i]->m_Area.bottom = atoi(p_value);

		if ((p_value = GetDataFromAsr(string(prefix + "OffsetX").c_str())) != NULL)
			appWindows[i]->m_Offset.x = atoi(p_value);

		if ((p_value = GetDataFromAsr(string(prefix + "OffsetY").c_str())) != NULL)
			appWindows[i]->m_Offset.y = atoi(p_value);


		if ((p_value = GetDataFromAsr(string(prefix + "Filter").c_str())) != NULL)
			appWindows[i]->m_Filter = atoi(p_value);

		if ((p_value = GetDataFromAsr(string(prefix + "Scale").c_str())) != NULL)
			appWindows[i]->m_Scale = atoi(p_value);

		if ((p_value = GetDataFromAsr(string(prefix + "Rotation").c_str())) != NULL)
			appWindows[i]->m_Rotation = atoi(p_value);

		if ((p_value = GetDataFromAsr(string(prefix + "Display").c_str())) != NULL)
			appWindowDisplays[i] = atoi(p_value) == 1 ? true : false;
	}

	// Auto load the airport config on ASR opened.
	CSectorElement rwy;
	for (rwy = GetPlugIn()->SectorFileElementSelectFirst(SECTOR_ELEMENT_RUNWAY);
		rwy.IsValid();
		rwy = GetPlugIn()->SectorFileElementSelectNext(rwy, SECTOR_ELEMENT_RUNWAY))
	{
		if (startsWith(getActiveAirport().c_str(), rwy.GetAirportName())) {
			string name = rwy.GetRunwayName(0) + string(" / ") + rwy.GetRunwayName(1);

			if (rwy.IsElementActive(true, 0) || rwy.IsElementActive(true, 1) || rwy.IsElementActive(false, 0) || rwy.IsElementActive(false, 1)) {
				RimcasInstance->toggleMonitoredRunwayDep(name);
				if (rwy.IsElementActive(false, 0) || rwy.IsElementActive(false, 1)) {
					RimcasInstance->toggleMonitoredRunwayArr(name);
				}
			}
		}
	}

	/*
	CSectorElement radar;
	for (radar = GetPlugIn()->SectorFileElementSelectFirst(SECTOR_ELEMENT_RADARS);
		radar.IsValid();
		radar = GetPlugIn()->SectorFileElementSelectNext(radar, SECTOR_ELEMENT_RADARS))
	{
		CPosition radarPos;
		CPosition radarPos1;

		auto compName = radar.GetComponentName(0);
		auto compName1 = radar.GetComponentName(1);
		auto compName2 = radar.GetComponentName(2);
		auto name = radar.GetName();
		radar.GetPosition(&radarPos, 0);
		radar.GetPosition(&radarPos1, 1);
		auto active = radar.IsElementActive(0);
		auto active1 = radar.IsElementActive(1);
		auto temp = "temp";
	}
	*/
}

void CSMRRadar::OnAsrContentToBeSaved()
{
	Logger::info(string(__FUNCSIG__));

	SaveDataToAsr("Airport", "Active airport for RIMCAS", getActiveAirport().c_str());

	SaveDataToAsr("ActiveProfile", "vSMR active profile", CurrentConfig->getActiveProfileName().c_str());

	SaveDataToAsr("FontSize", "vSMR font size", std::to_string(currentFontSize).c_str());

	SaveDataToAsr("Afterglow", "vSMR Afterglow enabled", std::to_string(int(Afterglow)).c_str());

	SaveDataToAsr("AppTrailsDots", "vSMR APPR Trail Dots", std::to_string(Trail_App).c_str());

	SaveDataToAsr("GndTrailsDots", "vSMR GRND Trail Dots", std::to_string(Trail_Gnd).c_str());

	SaveDataToAsr("PredictedLine", "vSMR Predicted Track Lines", std::to_string(PredictedLenght).c_str());

	string temp = "";

	for (int i = 1; i < 3; i++)
	{
		string prefix = "SRW" + std::to_string(i);

		temp = std::to_string(appWindows[i]->m_Area.left);
		SaveDataToAsr(string(prefix + "TopLeftX").c_str(), "SRW position", temp.c_str());

		temp = std::to_string(appWindows[i]->m_Area.top);
		SaveDataToAsr(string(prefix + "TopLeftY").c_str(), "SRW position", temp.c_str());

		temp = std::to_string(appWindows[i]->m_Area.right);
		SaveDataToAsr(string(prefix + "BottomRightX").c_str(), "SRW position", temp.c_str());

		temp = std::to_string(appWindows[i]->m_Area.bottom);
		SaveDataToAsr(string(prefix + "BottomRightY").c_str(), "SRW position", temp.c_str());

		temp = std::to_string(appWindows[i]->m_Offset.x);
		SaveDataToAsr(string(prefix + "OffsetX").c_str(), "SRW offset", temp.c_str());

		temp = std::to_string(appWindows[i]->m_Offset.y);
		SaveDataToAsr(string(prefix + "OffsetY").c_str(), "SRW offset", temp.c_str());

		temp = std::to_string(appWindows[i]->m_Filter);
		SaveDataToAsr(string(prefix + "Filter").c_str(), "SRW filter", temp.c_str());

		temp = std::to_string(appWindows[i]->m_Scale);
		SaveDataToAsr(string(prefix + "Scale").c_str(), "SRW range", temp.c_str());

		temp = std::to_string((int)appWindows[i]->m_Rotation);
		SaveDataToAsr(string(prefix + "Rotation").c_str(), "SRW rotation", temp.c_str());

		string to_save = "0";
		if (appWindowDisplays[i])
			to_save = "1";
		SaveDataToAsr(string(prefix + "Display").c_str(), "Display Secondary Radar Window", to_save.c_str());
	}	
}

void CSMRRadar::OnMoveScreenObject(int ObjectType, const char * sObjectId, POINT Pt, RECT Area, bool Released) {
	Logger::info(string(__FUNCSIG__));

	if (ObjectType == APPWINDOW_ONE || ObjectType == APPWINDOW_TWO) {
		int appWindowId = ObjectType - APPWINDOW_BASE;

		bool toggleCursor = appWindows[appWindowId]->OnMoveScreenObject(sObjectId, Pt, Area, Released);

		if (!toggleCursor)
		{
			if (standardCursor)
			{
				if (strcmp(sObjectId, "topbar") == 0)
					smrCursor = CopyCursor((HCURSOR)::LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDC_SMRMOVEWINDOW), IMAGE_CURSOR, 0, 0, LR_SHARED));
				else if (strcmp(sObjectId, "resize") == 0)
					smrCursor = CopyCursor((HCURSOR)::LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDC_SMRRESIZE), IMAGE_CURSOR, 0, 0, LR_SHARED));

				AFX_MANAGE_STATE(AfxGetStaticModuleState());
				ASSERT(smrCursor);
				SetCursor(smrCursor);
				standardCursor = false;
			}
		} else
		{
			if (!standardCursor)
			{
				if (customCursor) {
					smrCursor = CopyCursor((HCURSOR)::LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDC_SMRCURSOR), IMAGE_CURSOR, 0, 0, LR_SHARED));
				}
				else {
					smrCursor = (HCURSOR)::LoadCursor(NULL, IDC_ARROW);
				}

				AFX_MANAGE_STATE(AfxGetStaticModuleState());
				ASSERT(smrCursor);
				SetCursor(smrCursor);
				standardCursor = true;
			}
		}
	}

	if (ObjectType == DRAWING_TAG || ObjectType == TAG_CITEM_MANUALCORRELATE ||ObjectType == TAG_CITEM_CALLSIGN || ObjectType == TAG_CITEM_FPBOX || ObjectType == TAG_CITEM_RWY || ObjectType == TAG_CITEM_SID || ObjectType == TAG_CITEM_GATE || ObjectType == TAG_CITEM_NO || ObjectType == TAG_CITEM_GROUNDSTATUS) {
		CRadarTarget rt = GetPlugIn()->RadarTargetSelect(sObjectId);

		if (!Released)
		{
			if (standardCursor)
			{
				smrCursor = CopyCursor((HCURSOR)::LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDC_SMRMOVETAG), IMAGE_CURSOR, 0, 0, LR_SHARED));
				AFX_MANAGE_STATE(AfxGetStaticModuleState());
				ASSERT(smrCursor);
				SetCursor(smrCursor);
				standardCursor = false;
			}
		}
		else
		{
			if (!standardCursor)
			{
				if (customCursor) {
					smrCursor = CopyCursor((HCURSOR)::LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDC_SMRCURSOR), IMAGE_CURSOR, 0, 0, LR_SHARED));
				}
				else {
					smrCursor = (HCURSOR)::LoadCursor(NULL, IDC_ARROW);
				}

				AFX_MANAGE_STATE(AfxGetStaticModuleState());
				ASSERT(smrCursor);
				SetCursor(smrCursor);
				standardCursor = true;
			}
		}

		if (rt.IsValid()) {
			CRect Temp = Area;
			POINT TagCenterPix = Temp.CenterPoint();
			POINT AcPosPix = ConvertCoordFromPositionToPixel(GetPlugIn()->RadarTargetSelect(sObjectId).GetPosition().GetPosition());
			POINT CustomTag = { TagCenterPix.x - AcPosPix.x, TagCenterPix.y - AcPosPix.y };

			if (CurrentConfig->getActiveProfile()["labels"]["auto_deconfliction"].GetBool())
			{
				double angle = RadToDeg(atan2(CustomTag.y, CustomTag.x));
				angle = fmod(angle + 360, 360);
				vector<double> angles;
				for (double k = 0.0; k <= 360.0; k += 22.5)
					angles.push_back(k);

				TagAngles[sObjectId] = closest(angles, angle);
				TagLeaderLineLength[sObjectId] = max(LeaderLineDefaultlenght, min(int(DistancePts(AcPosPix, TagCenterPix)), LeaderLineDefaultlenght * 2));

			}
			else
			{
				TagsOffsets[sObjectId] = CustomTag;
			}


			GetPlugIn()->SetASELAircraft(GetPlugIn()->FlightPlanSelect(sObjectId));

			if (Released)
			{
				TagBeingDragged = "";
			}
			else
			{
				TagBeingDragged = sObjectId;
			}

			RequestRefresh();
		}		
	}

	if (ObjectType == RIMCAS_IAW) {
		TimePopupAreas[sObjectId] = Area;

		if (!Released)
		{
			if (standardCursor)
			{
				smrCursor = CopyCursor((HCURSOR)::LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDC_SMRMOVEWINDOW), IMAGE_CURSOR, 0, 0, LR_SHARED));

				AFX_MANAGE_STATE(AfxGetStaticModuleState());
				ASSERT(smrCursor);
				SetCursor(smrCursor);
				standardCursor = false;
			}
		}
		else
		{
			if (!standardCursor)
			{
				if (customCursor) {
					smrCursor = CopyCursor((HCURSOR)::LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDC_SMRCURSOR), IMAGE_CURSOR, 0, 0, LR_SHARED));					
				}
				else {
					smrCursor = (HCURSOR)::LoadCursor(NULL, IDC_ARROW);
				}

				AFX_MANAGE_STATE(AfxGetStaticModuleState());
				ASSERT(smrCursor);
				SetCursor(smrCursor);
				standardCursor = true;
			}
		}
	}

	mouseLocation = Pt;
	RequestRefresh();

}

void CSMRRadar::OnOverScreenObject(int ObjectType, const char * sObjectId, POINT Pt, RECT Area)
{
	Logger::info(string(__FUNCSIG__));
	mouseLocation = Pt;
	RequestRefresh();
}

void CSMRRadar::OnClickScreenObject(int ObjectType, const char * sObjectId, POINT Pt, RECT Area, int Button)
{
	Logger::info(string(__FUNCSIG__));
	mouseLocation = Pt;

	if (ObjectType == APPWINDOW_ONE || ObjectType == APPWINDOW_TWO) {
		int appWindowId = ObjectType - APPWINDOW_BASE;

		if (strcmp(sObjectId, "close") == 0)
			appWindowDisplays[appWindowId] = false;
		if (strcmp(sObjectId, "range") == 0) {
			GetPlugIn()->OpenPopupList(Area, "SRW Zoom", 1);
			GetPlugIn()->AddPopupListElement("55", "", RIMCAS_UPDATERANGE + appWindowId, false, int(appWindows[appWindowId]->m_Scale == 55));
			GetPlugIn()->AddPopupListElement("50", "", RIMCAS_UPDATERANGE + appWindowId, false, int(appWindows[appWindowId]->m_Scale == 50));
			GetPlugIn()->AddPopupListElement("45", "", RIMCAS_UPDATERANGE + appWindowId, false, int(appWindows[appWindowId]->m_Scale == 45));
			GetPlugIn()->AddPopupListElement("40", "", RIMCAS_UPDATERANGE + appWindowId, false, int(appWindows[appWindowId]->m_Scale == 40));
			GetPlugIn()->AddPopupListElement("35", "", RIMCAS_UPDATERANGE + appWindowId, false, int(appWindows[appWindowId]->m_Scale == 35));
			GetPlugIn()->AddPopupListElement("30", "", RIMCAS_UPDATERANGE + appWindowId, false, int(appWindows[appWindowId]->m_Scale == 30));
			GetPlugIn()->AddPopupListElement("25", "", RIMCAS_UPDATERANGE + appWindowId, false, int(appWindows[appWindowId]->m_Scale == 25));
			GetPlugIn()->AddPopupListElement("20", "", RIMCAS_UPDATERANGE + appWindowId, false, int(appWindows[appWindowId]->m_Scale == 20));
			GetPlugIn()->AddPopupListElement("15", "", RIMCAS_UPDATERANGE + appWindowId, false, int(appWindows[appWindowId]->m_Scale == 15));
			GetPlugIn()->AddPopupListElement("10", "", RIMCAS_UPDATERANGE + appWindowId, false, int(appWindows[appWindowId]->m_Scale == 10));
			GetPlugIn()->AddPopupListElement("5", "", RIMCAS_UPDATERANGE + appWindowId, false, int(appWindows[appWindowId]->m_Scale == 5));
			GetPlugIn()->AddPopupListElement("1", "", RIMCAS_UPDATERANGE + appWindowId, false, int(appWindows[appWindowId]->m_Scale == 1));
			GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		}
		if (strcmp(sObjectId, "filter") == 0) {
			GetPlugIn()->OpenPopupList(Area, "SRW Filter (ft)", 1);
			GetPlugIn()->AddPopupListElement("UNL", "", RIMCAS_UPDATEFILTER + appWindowId, false, int(appWindows[appWindowId]->m_Filter == 66000));
			GetPlugIn()->AddPopupListElement("9500", "", RIMCAS_UPDATEFILTER + appWindowId, false, int(appWindows[appWindowId]->m_Filter == 9500));
			GetPlugIn()->AddPopupListElement("8500", "", RIMCAS_UPDATEFILTER + appWindowId, false, int(appWindows[appWindowId]->m_Filter == 8500));
			GetPlugIn()->AddPopupListElement("7500", "", RIMCAS_UPDATEFILTER + appWindowId, false, int(appWindows[appWindowId]->m_Filter == 7500));
			GetPlugIn()->AddPopupListElement("6500", "", RIMCAS_UPDATEFILTER + appWindowId, false, int(appWindows[appWindowId]->m_Filter == 6500));
			GetPlugIn()->AddPopupListElement("5500", "", RIMCAS_UPDATEFILTER + appWindowId, false, int(appWindows[appWindowId]->m_Filter == 5500));
			GetPlugIn()->AddPopupListElement("4500", "", RIMCAS_UPDATEFILTER + appWindowId, false, int(appWindows[appWindowId]->m_Filter == 4500));
			GetPlugIn()->AddPopupListElement("3500", "", RIMCAS_UPDATEFILTER + appWindowId, false, int(appWindows[appWindowId]->m_Filter == 3500));
			GetPlugIn()->AddPopupListElement("2500", "", RIMCAS_UPDATEFILTER + appWindowId, false, int(appWindows[appWindowId]->m_Filter == 2500));
			GetPlugIn()->AddPopupListElement("1500", "", RIMCAS_UPDATEFILTER + appWindowId, false, int(appWindows[appWindowId]->m_Filter == 1500));
			GetPlugIn()->AddPopupListElement("500", "", RIMCAS_UPDATEFILTER + appWindowId, false, int(appWindows[appWindowId]->m_Filter == 500));
			string tmp = std::to_string(GetPlugIn()->GetTransitionAltitude());
			GetPlugIn()->AddPopupListElement(tmp.c_str(), "", RIMCAS_UPDATEFILTER + appWindowId, false, 2, false, true);
			GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		}
		if (strcmp(sObjectId, "rotate") == 0) {
			GetPlugIn()->OpenPopupList(Area, "SRW Rotate (deg)", 1);
			for (int k = 0; k <= 360; k++)
			{
				string tmp = std::to_string(k);
				GetPlugIn()->AddPopupListElement(tmp.c_str(), "", RIMCAS_UPDATEROTATE + appWindowId, false, int(appWindows[appWindowId]->m_Rotation == k));
			}
			GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		}
	}

	if (ObjectType == RIMCAS_ACTIVE_AIRPORT) {
		GetPlugIn()->OpenPopupEdit(Area, RIMCAS_ACTIVE_AIRPORT_FUNC, getActiveAirport().c_str());
	}

	if (ObjectType == DRAWING_BACKGROUND_CLICK)
	{
		if (QDMSelectEnabled)
		{
			if (Button == BUTTON_LEFT)
			{
				QDMSelectPt = Pt;
				RequestRefresh();
			}

			if (Button == BUTTON_RIGHT)
			{
				QDMSelectEnabled = false;
				RequestRefresh();
			}
		}

		if (QDMenabled)
		{
			if (Button == BUTTON_RIGHT)
			{
				QDMenabled = false;
				RequestRefresh();
			}
		}
	}

	if (ObjectType == RIMCAS_MENU) {

		if (strcmp(sObjectId, "DisplayMenu") == 0) {
			Area.top = Area.top + 30;
			Area.bottom = Area.bottom + 30;

			GetPlugIn()->OpenPopupList(Area, "Display Menu", 1);
			GetPlugIn()->AddPopupListElement("QDR Fixed Reference", "", RIMCAS_QDM_TOGGLE);
			GetPlugIn()->AddPopupListElement("QDR Select Reference", "", RIMCAS_QDM_SELECT_TOGGLE);
			GetPlugIn()->AddPopupListElement("SRW 1", "", APPWINDOW_ONE, false, int(appWindowDisplays[1]));
			GetPlugIn()->AddPopupListElement("SRW 2", "", APPWINDOW_TWO, false, int(appWindowDisplays[2]));
			GetPlugIn()->AddPopupListElement("Profiles", "", RIMCAS_OPEN_LIST);
			GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		}

		if (strcmp(sObjectId, "TargetMenu") == 0) {
			Area.top = Area.top + 30;
			Area.bottom = Area.bottom + 30;

			GetPlugIn()->OpenPopupList(Area, "Target", 1);
			GetPlugIn()->AddPopupListElement("Label Font Size", "", RIMCAS_OPEN_LIST);
			GetPlugIn()->AddPopupListElement("Afterglow", "", RIMCAS_UPDATE_AFTERGLOW, false, int(Afterglow));
			GetPlugIn()->AddPopupListElement("GRND Trail Dots", "", RIMCAS_OPEN_LIST);
			GetPlugIn()->AddPopupListElement("APPR Trail Dots", "", RIMCAS_OPEN_LIST);
			GetPlugIn()->AddPopupListElement("Predicted Track Line", "", RIMCAS_OPEN_LIST);
			GetPlugIn()->AddPopupListElement("Acquire", "", RIMCAS_UPDATE_ACQUIRE);
			GetPlugIn()->AddPopupListElement("Release", "", RIMCAS_UPDATE_RELEASE);
			GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		}

		if (strcmp(sObjectId, "MapMenu") == 0) {
			Area.top = Area.top + 30;
			Area.bottom = Area.bottom + 30;

			GetPlugIn()->OpenPopupList(Area, "Maps", 1);
			GetPlugIn()->AddPopupListElement("Airport Maps", "", RIMCAS_OPEN_LIST);
			GetPlugIn()->AddPopupListElement("Custom Maps", "", RIMCAS_OPEN_LIST);
			GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		}

		if (strcmp(sObjectId, "ColourMenu") == 0) {
			Area.top = Area.top + 30;
			Area.bottom = Area.bottom + 30;

			GetPlugIn()->OpenPopupList(Area, "Colours", 1);
			GetPlugIn()->AddPopupListElement("Colour Settings", "", RIMCAS_OPEN_LIST);
			GetPlugIn()->AddPopupListElement("Brightness", "", RIMCAS_OPEN_LIST);
			GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		}

		if (strcmp(sObjectId, "RIMCASMenu") == 0) {
			Area.top = Area.top + 30;
			Area.bottom = Area.bottom + 30;

			GetPlugIn()->OpenPopupList(Area, "Alerts", 1);
			GetPlugIn()->AddPopupListElement("Conflict Alert ARR", "", RIMCAS_OPEN_LIST);
			GetPlugIn()->AddPopupListElement("Conflict Alert DEP", "", RIMCAS_OPEN_LIST);
			GetPlugIn()->AddPopupListElement("Runway closed", "", RIMCAS_OPEN_LIST);
			GetPlugIn()->AddPopupListElement("Visibility", "", RIMCAS_OPEN_LIST);
			GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		}

		if (strcmp(sObjectId, "/") == 0)
		{
			if (Button == BUTTON_LEFT)
			{
				DistanceToolActive = !DistanceToolActive;
				if (!DistanceToolActive)
					ActiveDistance = pair<string, string>("", "");

				if (DistanceToolActive)
				{
					QDMenabled = false;
					QDMSelectEnabled = false;
				}
			}
			if (Button == BUTTON_RIGHT)
			{
				DistanceToolActive = false;
				ActiveDistance = pair<string, string>("", "");
				DistanceTools.clear();
			}

		}

	}

	if (ObjectType == DRAWING_TAG || ObjectType == DRAWING_AC_SYMBOL) {		
		CRadarTarget rt = GetPlugIn()->RadarTargetSelect(sObjectId);
		// GetPlugIn()->SetASELAircraft(rt); // NOTE: This does NOT work eventhough the api says it should?
		GetPlugIn()->SetASELAircraft(GetPlugIn()->FlightPlanSelect(sObjectId));  // make sure the correct aircraft is selected before calling 'StartTagFunction'
		
		if (rt.GetCorrelatedFlightPlan().IsValid()) {
			StartTagFunction(rt.GetCallsign(), NULL, TAG_ITEM_TYPE_CALLSIGN, rt.GetCallsign(), NULL, TAG_ITEM_FUNCTION_NO, Pt, Area);
		}		

		// Release & correlate actions

		if (ReleaseInProgress || AcquireInProgress)
		{
			if (ReleaseInProgress)
			{
				ReleaseInProgress = NeedCorrelateCursor = false;

				ReleasedTracks.push_back(rt.GetSystemID());

				if (std::find(ManuallyCorrelated.begin(), ManuallyCorrelated.end(), rt.GetSystemID()) != ManuallyCorrelated.end())
					ManuallyCorrelated.erase(std::find(ManuallyCorrelated.begin(), ManuallyCorrelated.end(), rt.GetSystemID()));
			}

			if (AcquireInProgress)
			{
				AcquireInProgress = NeedCorrelateCursor = false;

				ManuallyCorrelated.push_back(rt.GetSystemID());

				if (std::find(ReleasedTracks.begin(), ReleasedTracks.end(), rt.GetSystemID()) != ReleasedTracks.end())
					ReleasedTracks.erase(std::find(ReleasedTracks.begin(), ReleasedTracks.end(), rt.GetSystemID()));
			}


			if (NeedCorrelateCursor)
			{
				if (standardCursor)
				{
					smrCursor = CopyCursor((HCURSOR)::LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDC_SMRCORRELATE), IMAGE_CURSOR, 0, 0, LR_SHARED));
					
					AFX_MANAGE_STATE(AfxGetStaticModuleState());
					ASSERT(smrCursor);
					SetCursor(smrCursor);
					standardCursor = false;
				}
			}
			else
			{
				if (!standardCursor)
				{
					if (customCursor) {
						smrCursor = CopyCursor((HCURSOR)::LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDC_SMRCURSOR), IMAGE_CURSOR, 0, 0, LR_SHARED));						
					}
					else {
						smrCursor = (HCURSOR)::LoadCursor(NULL, IDC_ARROW);
					}
					
					AFX_MANAGE_STATE(AfxGetStaticModuleState());
					ASSERT(smrCursor);
					SetCursor(smrCursor);
					standardCursor = true;
				}
			}

			return;
		}

		if (ObjectType == DRAWING_AC_SYMBOL)
		{
			if (QDMSelectEnabled)
			{
				if (Button == BUTTON_LEFT)
				{
					QDMSelectPt = Pt;
					RequestRefresh();
				}
			}
			else if (DistanceToolActive) {
				if (ActiveDistance.first == "")
				{
					ActiveDistance.first = sObjectId;
				}
				else if (ActiveDistance.second == "")
				{
					ActiveDistance.second = sObjectId;
					DistanceTools.insert(ActiveDistance);
					ActiveDistance = pair<string, string>("", "");
					DistanceToolActive = false;
				}
				RequestRefresh();
			}
			else
			{
				if (TagsOffsets.find(sObjectId) != TagsOffsets.end())
					TagsOffsets.erase(sObjectId);

				if (Button == BUTTON_LEFT)
				{
					if (TagAngles.find(sObjectId) == TagAngles.end())
					{
						TagAngles[sObjectId] = 0;
					} else
					{
						TagAngles[sObjectId] = fmod(TagAngles[sObjectId] - 22.5, 360);
					}
				}

				if (Button == BUTTON_RIGHT)
				{
					if (TagAngles.find(sObjectId) == TagAngles.end())
					{
						TagAngles[sObjectId] = 0;
					}
					else
					{
						TagAngles[sObjectId] = fmod(TagAngles[sObjectId] + 22.5, 360);
					}
				}

				RequestRefresh();
			}
		}
	}

	if (ObjectType == DRAWING_AC_SYMBOL_APPWINDOW1 || ObjectType == DRAWING_AC_SYMBOL_APPWINDOW2)
	{
		if (DistanceToolActive) {
			if (ActiveDistance.first == "")
			{
				ActiveDistance.first = sObjectId;
			}
			else if (ActiveDistance.second == "")
			{
				ActiveDistance.second = sObjectId;
				DistanceTools.insert(ActiveDistance);
				ActiveDistance = pair<string, string>("", "");
				DistanceToolActive = false;
			}
			RequestRefresh();
		} else
		{
			if (ObjectType == DRAWING_AC_SYMBOL_APPWINDOW1)
				appWindows[1]->OnClickScreenObject(sObjectId, Pt, Button);

			if (ObjectType == DRAWING_AC_SYMBOL_APPWINDOW2)
				appWindows[2]->OnClickScreenObject(sObjectId, Pt, Button);
		}
	}

	if (ObjectType == TAG_CITEM_CALLSIGN || ObjectType == TAG_CITEM_FPBOX || ObjectType == TAG_CITEM_RWY || ObjectType == TAG_CITEM_SID || ObjectType == TAG_CITEM_GATE || ObjectType == TAG_CITEM_NO || ObjectType == TAG_CITEM_GROUNDSTATUS) {

		CRadarTarget rt = GetPlugIn()->RadarTargetSelect(sObjectId);
		GetPlugIn()->SetASELAircraft(GetPlugIn()->FlightPlanSelect(sObjectId));

		switch (ObjectType) {
		case TAG_CITEM_CALLSIGN:
			if (Button == BUTTON_LEFT || Button == BUTTON_MIDDLE)
				StartTagFunction(rt.GetCallsign(), NULL, TAG_CITEM_CALLSIGN, rt.GetCallsign(), NULL, TAG_ITEM_FUNCTION_HANDOFF_POPUP_MENU, Pt, Area);
			else if (Button == BUTTON_RIGHT)
				StartTagFunction(rt.GetCallsign(), NULL, TAG_CITEM_CALLSIGN, rt.GetCallsign(), NULL, TAG_ITEM_FUNCTION_COMMUNICATION_POPUP, Pt, Area);

			break;

		case TAG_CITEM_FPBOX:
			if (Button == BUTTON_RIGHT)
				StartTagFunction(rt.GetCallsign(), NULL, TAG_CITEM_FPBOX, rt.GetCallsign(), NULL, TAG_ITEM_FUNCTION_OPEN_FP_DIALOG, Pt, Area);

			break;

		case TAG_CITEM_RWY:
			if (Button == BUTTON_RIGHT)
				StartTagFunction(rt.GetCallsign(), NULL, TAG_CITEM_RWY, rt.GetCallsign(), NULL, TAG_ITEM_FUNCTION_ASSIGNED_RUNWAY, Pt, Area);

			break;

		case TAG_CITEM_SID:
			if (Button == BUTTON_RIGHT)
				StartTagFunction(rt.GetCallsign(), NULL, TAG_CITEM_SID, rt.GetCallsign(), NULL, TAG_ITEM_FUNCTION_ASSIGNED_SID, Pt, Area);

			break;

		case TAG_CITEM_GATE:
			if (Button == BUTTON_RIGHT)
				StartTagFunction(rt.GetCallsign(), NULL, TAG_CITEM_GATE, rt.GetCallsign(), NULL, TAG_ITEM_FUNCTION_EDIT_SCRATCH_PAD, Pt, Area);

			break;

		case TAG_CITEM_GROUNDSTATUS:
			if (Button == BUTTON_RIGHT)
				StartTagFunction(rt.GetCallsign(), NULL, TAG_CITEM_GROUNDSTATUS, rt.GetCallsign(), NULL, TAG_ITEM_FUNCTION_SET_GROUND_STATUS, Pt, Area);
			
			break;
		}
	
	}	

	if (ObjectType == RIMCAS_DISTANCE_TOOL)
	{
		vector<string> s = split(sObjectId, ',');
		pair<string, string> toRemove = pair<string, string>(s.front(), s.back());

		typedef multimap<string, string>::iterator iterator;
		std::pair<iterator, iterator> iterpair = DistanceTools.equal_range(toRemove.first);

		iterator it = iterpair.first;
		for (; it != iterpair.second; ++it) {
			if (it->second == toRemove.second) {
				it = DistanceTools.erase(it);
				break;
			}
		}

	}

	RequestRefresh();
};

void CSMRRadar::OnFunctionCall(int FunctionId, const char * sItemString, POINT Pt, RECT Area) {
	Logger::info(string(__FUNCSIG__));
	mouseLocation = Pt;
	if (FunctionId == APPWINDOW_ONE || FunctionId == APPWINDOW_TWO) {
		int id = FunctionId - APPWINDOW_BASE;
		appWindowDisplays[id] = !appWindowDisplays[id];
	}

	if (FunctionId == RIMCAS_ACTIVE_AIRPORT_FUNC) {
		setActiveAirport(sItemString);
		SaveDataToAsr("Airport", "Active airport", getActiveAirport().c_str());
	}

	if (FunctionId == RIMCAS_UPDATE_FONTS) {
		if (strcmp(sItemString, "Size 1") == 0)
			currentFontSize = 1;
		if (strcmp(sItemString, "Size 2") == 0)
			currentFontSize = 2;
		if (strcmp(sItemString, "Size 3") == 0)
			currentFontSize = 3;
		if (strcmp(sItemString, "Size 4") == 0)
			currentFontSize = 4;
		if (strcmp(sItemString, "Size 5") == 0)
			currentFontSize = 5;

		ShowLists["Label Font Size"] = true;
	}

	if (FunctionId == RIMCAS_QDM_TOGGLE) {
		QDMenabled = !QDMenabled;
		QDMSelectEnabled = false;
	}

	if (FunctionId == RIMCAS_QDM_SELECT_TOGGLE)
	{
		if (!QDMSelectEnabled)
		{
			QDMSelectPt = ConvertCoordFromPositionToPixel(AirportPositions[getActiveAirport()]);
		}
		QDMSelectEnabled = !QDMSelectEnabled;
		QDMenabled = false;
	}

	if (FunctionId == RIMCAS_UPDATE_PROFILE) {
		this->CSMRRadar::LoadProfile(sItemString);
		LoadCustomFont();
		SaveDataToAsr("ActiveProfile", "vSMR active profile", sItemString);

		ShowLists["Profiles"] = true;
	}

	if (FunctionId == RIMCAS_UPDATEFILTER1 || FunctionId == RIMCAS_UPDATEFILTER2) {
		int id = FunctionId - RIMCAS_UPDATEFILTER;
		if (startsWith("UNL", sItemString))
			sItemString = "66000";
		appWindows[id]->m_Filter = atoi(sItemString);
	}

	if (FunctionId == RIMCAS_UPDATERANGE1 || FunctionId == RIMCAS_UPDATERANGE2) {
		int id = FunctionId - RIMCAS_UPDATERANGE;
		appWindows[id]->m_Scale = atoi(sItemString);
	}

	if (FunctionId == RIMCAS_UPDATEROTATE1 || FunctionId == RIMCAS_UPDATEROTATE2) {
		int id = FunctionId - RIMCAS_UPDATEROTATE;
		appWindows[id]->m_Rotation = atoi(sItemString);
	}

	if (FunctionId == RIMCAS_UPDATE_BRIGHNESS) {
		if (strcmp(sItemString, "Day") == 0)
			ColorSettingsDay = true;
		else
			ColorSettingsDay = false;

		ShowLists["Colour Settings"] = true;

		RequestRefresh();
	}

	if (FunctionId == RIMCAS_CA_ARRIVAL_FUNC) {
		RimcasInstance->toggleMonitoredRunwayArr(string(sItemString));

		ShowLists["Conflict Alert ARR"] = true;

		RequestRefresh();
	}

	if (FunctionId == RIMCAS_CA_MONITOR_FUNC) {
		RimcasInstance->toggleMonitoredRunwayDep(string(sItemString));

		ShowLists["Conflict Alert DEP"] = true;

		RequestRefresh();
	}

	if (FunctionId == RIMCAS_CLOSED_RUNWAYS_FUNC) {
		RimcasInstance->toggleClosedRunway(string(sItemString));

		ShowLists["Runway closed"] = true;

		RequestRefresh();
	}

	if (FunctionId == RIMCAS_OPEN_LIST) {

		ShowLists[string(sItemString)] = true;
		ListAreas[string(sItemString)] = Area;

		RequestRefresh();
	}

	if (FunctionId == RIMCAS_UPDATE_LVP) {
		if (strcmp(sItemString, "Normal") == 0)
			isLVP = false;
		if (strcmp(sItemString, "Low") == 0)
			isLVP = true;

		ShowLists["Visibility"] = true;

		RequestRefresh();
	}

	if (FunctionId == RIMCAS_UPDATE_AFTERGLOW)
	{
		Afterglow = !Afterglow;
	}

	if (FunctionId == RIMCAS_UPDATE_GND_TRAIL)
	{
		Trail_Gnd = atoi(sItemString);

		ShowLists["GRND Trail Dots"] = true;
	}

	if (FunctionId == RIMCAS_UPDATE_APP_TRAIL)
	{
		Trail_App = atoi(sItemString);

		ShowLists["APPR Trail Dots"] = true;
	}

	if (FunctionId == RIMCAS_UPDATE_PTL)
	{
		PredictedLenght = atoi(sItemString);

		ShowLists["Predicted Track Line"] = true;
	}

	if (FunctionId == RIMCAS_BRIGHTNESS_LABEL)
	{
		ColorManager->update_brightness("label", std::atoi(sItemString));
		ShowLists["Label"] = true;
	}

	if (FunctionId == RIMCAS_BRIGHTNESS_AFTERGLOW)
	{
		ColorManager->update_brightness("afterglow", std::atoi(sItemString));
		ShowLists["Afterglow"] = true;
	}

	if (FunctionId == RIMCAS_BRIGHTNESS_SYMBOL)
	{
		ColorManager->update_brightness("symbol", std::atoi(sItemString));
		ShowLists["Symbol"] = true;
	}

	if (FunctionId == RIMCAS_UPDATE_RELEASE)
	{
		ReleaseInProgress = !ReleaseInProgress;
		if (ReleaseInProgress)
			AcquireInProgress = false;
		NeedCorrelateCursor = ReleaseInProgress;

		if (NeedCorrelateCursor)
		{
			if (standardCursor)
			{
				smrCursor = CopyCursor((HCURSOR)::LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDC_SMRCORRELATE), IMAGE_CURSOR, 0, 0, LR_SHARED));
				
				AFX_MANAGE_STATE(AfxGetStaticModuleState());
				ASSERT(smrCursor);
				SetCursor(smrCursor);
				standardCursor = false;
			}
		}
		else
		{
			if (!standardCursor)
			{
				if (customCursor) {
					smrCursor = CopyCursor((HCURSOR)::LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDC_SMRCURSOR), IMAGE_CURSOR, 0, 0, LR_SHARED));
				}
				else {
					smrCursor = (HCURSOR)::LoadCursor(NULL, IDC_ARROW);
				}
								
				AFX_MANAGE_STATE(AfxGetStaticModuleState());
				ASSERT(smrCursor);
				SetCursor(smrCursor);
				standardCursor = true;
			}
		}
	}

	if (FunctionId == RIMCAS_UPDATE_ACQUIRE)
	{
		AcquireInProgress = !AcquireInProgress;
		if (AcquireInProgress)
			ReleaseInProgress = false;
		NeedCorrelateCursor = AcquireInProgress;

		if (NeedCorrelateCursor)
		{
			if (standardCursor)
			{
				smrCursor = CopyCursor((HCURSOR)::LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDC_SMRCORRELATE), IMAGE_CURSOR, 0, 0, LR_SHARED));
				
				AFX_MANAGE_STATE(AfxGetStaticModuleState());
				ASSERT(smrCursor);
				SetCursor(smrCursor);
				standardCursor = false;
			}
		}
		else
		{
			if (!standardCursor)
			{
				if (customCursor) {
					smrCursor = CopyCursor((HCURSOR)::LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDC_SMRCURSOR), IMAGE_CURSOR, 0, 0, LR_SHARED));
				}
				else {
					smrCursor = (HCURSOR)::LoadCursor(NULL, IDC_ARROW);
				}
				
				AFX_MANAGE_STATE(AfxGetStaticModuleState());
				ASSERT(smrCursor);
				SetCursor(smrCursor);
				standardCursor = true;
			}
		}
	}
}

void CSMRRadar::RefreshAirportActivity(void) {
	Logger::info(string(__FUNCSIG__));
	//
	// Getting the depatures and arrivals airports
	//

	Active_Arrivals.clear();
	CSectorElement airport;
	for (airport = GetPlugIn()->SectorFileElementSelectFirst(SECTOR_ELEMENT_AIRPORT);
		airport.IsValid();
		airport = GetPlugIn()->SectorFileElementSelectNext(airport, SECTOR_ELEMENT_AIRPORT))
	{
		if (airport.IsElementActive(false)) {
			string s = airport.GetName();
			s = s.substr(0, 4);
			transform(s.begin(), s.end(), s.begin(), ::toupper);
			Active_Arrivals.push_back(s);
		}
	}
}

void CSMRRadar::OnRadarTargetPositionUpdate(CRadarTarget RadarTarget)
{
	Logger::info(string(__FUNCSIG__));
	if (!RadarTarget.IsValid() || !RadarTarget.GetPosition().IsValid())
		return;

	CRadarTargetPositionData RtPos = RadarTarget.GetPosition();

	Patatoides[RadarTarget.GetCallsign()].points.clear();

	CFlightPlan fp = GetPlugIn()->FlightPlanSelect(RadarTarget.GetCallsign());

	// All units in M
	float width = 34.0f;
	float cabin_width = 4.0f;
	float lenght = 38.0f;

	if (fp.IsValid()) {
		char wtc = fp.GetFlightPlanData().GetAircraftWtc();

		if (wtc == 'L') {
			width = 13.0f;
			cabin_width = 2.0f;
			lenght = 12.0f;
		}

		if (wtc == 'H') {
			width = 61.0f;
			cabin_width = 7.0f;
			lenght = 64.0f;
		}

		if (wtc == 'J') {
			width = 80.0f;
			cabin_width = 7.0f;
			lenght = 73.0f;
		}
	}


	width = width + float((rand() % 5) - 2);
	cabin_width = cabin_width + float((rand() % 3) - 1);
	lenght = lenght + float((rand() % 5) - 2);


	float trackHead = float(RadarTarget.GetPosition().GetReportedHeadingTrueNorth());
	float inverseTrackHead = float(fmod(trackHead + 180.0f, 360));
	float leftTrackHead = float(fmod(trackHead - 90.0f, 360));
	float rightTrackHead = float(fmod(trackHead + 90.0f, 360));

	float HalfLenght = lenght / 2.0f;
	float HalfCabWidth = cabin_width / 2.0f;
	float HalfSpanWidth = width / 2.0f;

	// Base shape is like a deformed cross


	CPosition topMiddle = Haversine(RtPos.GetPosition(), trackHead, HalfLenght);
	CPosition topLeft = Haversine(topMiddle, leftTrackHead, HalfCabWidth);
	CPosition topRight = Haversine(topMiddle, rightTrackHead, HalfCabWidth);

	CPosition bottomMiddle = Haversine(RtPos.GetPosition(), inverseTrackHead, HalfLenght);
	CPosition bottomLeft = Haversine(bottomMiddle, leftTrackHead, HalfCabWidth);
	CPosition bottomRight = Haversine(bottomMiddle, rightTrackHead, HalfCabWidth);

	CPosition middleTopLeft = Haversine(topLeft, float(fmod(inverseTrackHead + 25.0f, 360)), 0.8f*HalfLenght);
	CPosition middleTopRight = Haversine(topRight, float(fmod(inverseTrackHead - 25.0f, 360)), 0.8f*HalfLenght);
	CPosition middleBottomLeft = Haversine(bottomLeft, float(fmod(trackHead - 15.0f, 360)), 0.8f*HalfLenght);
	CPosition middleBottomRight = Haversine(bottomRight, float(fmod(trackHead + 15.0f, 360)), 0.8f*HalfLenght);

	CPosition rightTop = Haversine(middleBottomRight, rightTrackHead, 0.7f*HalfSpanWidth);
	CPosition rightBottom = Haversine(rightTop, inverseTrackHead, cabin_width);

	CPosition leftTop = Haversine(middleBottomLeft, leftTrackHead, 0.7f*HalfSpanWidth);
	CPosition leftBottom = Haversine(leftTop, inverseTrackHead, cabin_width);

	CPosition basePoints[12];
	basePoints[0] = topLeft;
	basePoints[1] = middleTopLeft;
	basePoints[2] = leftTop;
	basePoints[3] = leftBottom;
	basePoints[4] = middleBottomLeft;
	basePoints[5] = bottomLeft;
	basePoints[6] = bottomRight;
	basePoints[7] = middleBottomRight;
	basePoints[8] = rightBottom;
	basePoints[9] = rightTop;
	basePoints[10] = middleTopRight;
	basePoints[11] = topRight;

	// 12 points total, so 11 from 0
	// ------

	// Random points between points of base shape

	for (int i = 0; i < 12; i++){

		CPosition newPoint, lastPoint, endPoint, startPoint;

		startPoint = basePoints[i];
		if (i == 11) endPoint = basePoints[0];
		else endPoint = basePoints[i + 1];

		double dist, rndHeading;
		dist = startPoint.DistanceTo(endPoint);

		Patatoides[RadarTarget.GetCallsign()].points[i * 7] = { startPoint.m_Latitude, startPoint.m_Longitude };
		lastPoint = startPoint;

		for (int k = 1; k < 7; k++){

			rndHeading = float(fmod(lastPoint.DirectionTo(endPoint) + (-25.0 + (rand() % 50 + 1)), 360));
			newPoint = Haversine(lastPoint, rndHeading, dist * 200);
			Patatoides[RadarTarget.GetCallsign()].points[(i * 7) + k] = { newPoint.m_Latitude, newPoint.m_Longitude };
			lastPoint = newPoint;
		}
	}
}

string CSMRRadar::GetBottomLine(const char * Callsign) {
	Logger::info(string(__FUNCSIG__));

	CFlightPlan fp = GetPlugIn()->FlightPlanSelect(Callsign);
	string to_render = "";
	if (fp.IsValid()) {
		to_render += fp.GetCallsign();

		string callsign_code = fp.GetCallsign();
		callsign_code = callsign_code.substr(0, 3);
		to_render += " (" + Callsigns->getCallsign(callsign_code) + ")";

		to_render += " (";
		to_render += fp.GetPilotName();
		to_render += "): ";
		to_render += fp.GetFlightPlanData().GetAircraftFPType();
		to_render += " ";

		if (fp.GetFlightPlanData().IsReceived()) {
			const char * assr = fp.GetControllerAssignedData().GetSquawk();
			const char * ssr = GetPlugIn()->RadarTargetSelect(fp.GetCallsign()).GetPosition().GetSquawk();
			if (strlen(assr) != 0 && !startsWith(ssr, assr)) {
				to_render += assr;
				to_render += ":";
				to_render += ssr;
			}
			else {
				to_render += "I:";
				to_render += ssr;
			}

			to_render += " ";
			to_render += fp.GetFlightPlanData().GetOrigin();
			to_render += "==>";
			to_render += fp.GetFlightPlanData().GetDestination();
			to_render += " (";
			to_render += fp.GetFlightPlanData().GetAlternate();
			to_render += ")";

			to_render += " at ";
			int rfl = fp.GetControllerAssignedData().GetFinalAltitude();
			string rfl_s;
			if (rfl == 0)
				rfl = fp.GetFlightPlanData().GetFinalAltitude();
			if (rfl > GetPlugIn()->GetTransitionAltitude())
				rfl_s = "FL" + std::to_string(rfl / 100);
			else
				rfl_s = std::to_string(rfl) + "ft";

			to_render += rfl_s;
			to_render += " Route: ";
			to_render += fp.GetFlightPlanData().GetRoute();
		}
	}

	return to_render;
}

bool CSMRRadar::OnCompileCommand(const char * sCommandLine)
{
	Logger::info(string(__FUNCSIG__));
	if (strcmp(sCommandLine, ".smr reload") == 0) {
		CurrentConfig = new CConfig(DllPath + "\\vSMR_Profiles.json");
		LoadProfile(CurrentConfig->getActiveProfileName());
		return true;
	}

	return false;
}

void CSMRRadar::OnFlightPlanDisconnect(CFlightPlan FlightPlan)
{
	Logger::info(string(__FUNCSIG__));
	string callsign = string(FlightPlan.GetCallsign());

	for (multimap<string, string>::iterator itr = DistanceTools.begin(); itr != DistanceTools.end(); ++itr) {
		if (itr->first == callsign || itr->second == callsign)
			itr = DistanceTools.erase(itr);
	}
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_SETCURSOR:
		SetCursor(smrCursor);
		return true;
	default:
		return CallWindowProc(gSourceProc, hwnd, uMsg, wParam, lParam);
	}
}

void CSMRRadar::OnRefresh(HDC hDC, int Phase)
{
	// Phase:
	// REFRESH_PHASE_BACK_BITMAP = 0;
	// REFRESH_PHASE_BEFORE_TAGS = 1;
	// REFRESH_PHASE_AFTER_TAGS = 2;
	// REFRESH_PHASE_AFTER_LISTS = 3;

	Logger::info(string(__FUNCSIG__));
	// Changing the mouse cursor
	if (initCursor)
	{
		if (customCursor) {
			smrCursor = CopyCursor((HCURSOR)::LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDC_SMRCURSOR), IMAGE_CURSOR, 0, 0, LR_SHARED));
			// This got broken because of threading as far as I can tell
			// The cursor does change for some milliseconds but gets reset almost instantly by external MFC code
		}
		else {
			smrCursor = (HCURSOR)::LoadCursor(NULL, IDC_ARROW);
		}

		if (smrCursor != nullptr)
		{		
			pluginWindow = GetActiveWindow();
			gSourceProc = (WNDPROC)SetWindowLong(pluginWindow, GWL_WNDPROC, (LONG)WindowProc);
		}
		initCursor = false;
	}

	if (Phase == REFRESH_PHASE_AFTER_LISTS) {
		Logger::info("Phase == REFRESH_PHASE_AFTER_LISTS");
		if (!ColorSettingsDay) {
			// Creating the gdi+ graphics
			Graphics graphics(hDC);
			graphics.SetPageUnit(Gdiplus::UnitPixel);

			graphics.SetSmoothingMode(SmoothingModeAntiAlias);

			SolidBrush AlphaBrush(Color(CurrentConfig->getActiveProfile()["filters"]["night_alpha_setting"].GetInt(), 0, 0, 0));

			CRect RadarArea(GetRadarArea());
			RadarArea.top = RadarArea.top - 1;
			RadarArea.bottom = GetChatArea().bottom;

			graphics.FillRectangle(&AlphaBrush, CopyRect(CRect(RadarArea)));

			graphics.ReleaseHDC(hDC);
		}

		Logger::info("break Phase == REFRESH_PHASE_AFTER_LISTS");
		return;
	}

	if (Phase != REFRESH_PHASE_BEFORE_TAGS)
		return;

	Logger::info("Phase != REFRESH_PHASE_BEFORE_TAGS");

	struct Utils {
		static RECT GetAreaFromText(CDC * dc, string text, POINT Pos) {
			RECT Area = { Pos.x, Pos.y, Pos.x + dc->GetTextExtent(text.c_str()).cx, Pos.y + dc->GetTextExtent(text.c_str()).cy };
			return Area;
		}
	};

	// Timer each seconds
	clock_final = clock() - clock_init;
	double delta_t = (double)clock_final / ((double)CLOCKS_PER_SEC);
	if (delta_t >= 1) {
		clock_init = clock();
		BLINK = !BLINK;
		RefreshAirportActivity();
	}

	if (!QDMenabled && !QDMSelectEnabled)
	{
		POINT p;
		if (GetCursorPos(&p)) {
			if (ScreenToClient(GetActiveWindow(), &p)) {
				mouseLocation = p;
			}
		}
	}

	CDC dc;
	dc.Attach(hDC);

	// Creating the gdi+ graphics
	Graphics graphics(hDC);
	graphics.SetPageUnit(Gdiplus::UnitPixel);
	graphics.SetSmoothingMode(SmoothingModeNone);
	graphics.SetTextRenderingHint(TextRenderingHintSingleBitPerPixel);

	RECT RadarArea = GetRadarArea();
	RECT ChatArea = GetChatArea();
	RadarArea.bottom = ChatArea.top;

	AirportPositions.clear();

	CSectorElement apt;
	for (apt = GetPlugIn()->SectorFileElementSelectFirst(SECTOR_ELEMENT_AIRPORT);
		apt.IsValid();
		apt = GetPlugIn()->SectorFileElementSelectNext(apt, SECTOR_ELEMENT_AIRPORT))
	{
		CPosition Pos;
		apt.GetPosition(&Pos, 0);
		AirportPositions[string(apt.GetName())] = Pos;
	}

#pragma region RIMCAS

	RimcasInstance->RunwayAreas.clear();

#pragma endregion

	if (QDMSelectEnabled || QDMenabled)
	{
		CRect R(GetRadarArea());
		R.top += 20;
		R.bottom = GetChatArea().top;

		R.NormalizeRect();
		AddScreenObject(DRAWING_BACKGROUND_CLICK, "", R, false, "");
	}

#pragma region RIMCAS

	Logger::info("Runway loop");
	CSectorElement rwy;
	for (rwy = GetPlugIn()->SectorFileElementSelectFirst(SECTOR_ELEMENT_RUNWAY);
		rwy.IsValid();
		rwy = GetPlugIn()->SectorFileElementSelectNext(rwy, SECTOR_ELEMENT_RUNWAY))
	{
		if (startsWith(getActiveAirport().c_str(), rwy.GetAirportName())) {

			CPosition Left;
			rwy.GetPosition(&Left, 1);
			CPosition Right;
			rwy.GetPosition(&Right, 0);

			string runway_name = rwy.GetRunwayName(0);
			string runway_name2 = rwy.GetRunwayName(1);

			double bearing1 = TrueBearing(Left, Right);
			double bearing2 = TrueBearing(Right, Left);

			const Value& CustomMap = CurrentConfig->getAirportMapIfAny(getActiveAirport());

			vector<CPosition> def;

			if (CurrentConfig->isCustomRunwayAvail(getActiveAirport(), runway_name, runway_name2)) {
				const Value& Runways = CustomMap["runways"];

				if (Runways.IsArray()) {
					for (SizeType i = 0; i < Runways.Size(); i++) {
						if (startsWith(runway_name.c_str(), Runways[i]["runway_name"].GetString()) ||
							startsWith(runway_name2.c_str(), Runways[i]["runway_name"].GetString())) {

							string path_name = "path";

							if (isLVP)
								path_name = "path_lvp";

							const Value& Path = Runways[i][path_name.c_str()];
							for (SizeType j = 0; j < Path.Size(); j++) {
								CPosition position;
								position.LoadFromStrings(Path[j][(SizeType)1].GetString(), Path[j][(SizeType)0].GetString());

								def.push_back(position);
							}

						}
					}
				}
			}
			else {
				def = RimcasInstance->GetRunwayArea(Left, Right);
			}

			RimcasInstance->AddRunwayArea(this, runway_name, runway_name2, def);

			string RwName = runway_name + " / " + runway_name2;

			if (RimcasInstance->ClosedRunway.find(RwName) != RimcasInstance->ClosedRunway.end()) {
				if (RimcasInstance->ClosedRunway[RwName]) {

					CPen RedPen(PS_SOLID, 2, RGB(150, 0, 0));
					CPen * oldPen = dc.SelectObject(&RedPen);

					if (CurrentConfig->isCustomRunwayAvail(getActiveAirport(), runway_name, runway_name2)) {
						const Value& Runways = CustomMap["runways"];

						if (Runways.IsArray()) {
							for (SizeType i = 0; i < Runways.Size(); i++) {
								if (startsWith(runway_name.c_str(), Runways[i]["runway_name"].GetString()) ||
									startsWith(runway_name2.c_str(), Runways[i]["runway_name"].GetString())) {

									string path_name = "path";

									if (isLVP)
										path_name = "path_lvp";

									const Value& Path = Runways[i][path_name.c_str()];

									PointF lpPoints[5000];

									int k = 1;
									int l = 0;
									for (SizeType w = 0; w < Path.Size(); w++) {
										CPosition position;
										position.LoadFromStrings(Path[w][static_cast<SizeType>(1)].GetString(), Path[w][static_cast<SizeType>(0)].GetString());

										POINT cv = ConvertCoordFromPositionToPixel(position);
										lpPoints[l] = { REAL(cv.x), REAL(cv.y) };

										k++;
										l++;
									}

									graphics.FillPolygon(&SolidBrush(Color(150, 0, 0)), lpPoints, k - 1);

									break;
								}
							}
						}

					}
					else {
						vector<CPosition> Area = RimcasInstance->GetRunwayArea(Left, Right);

						PointF lpPoints[5000];
						int w = 0;
						for(auto &Point : Area)
						{
							POINT toDraw = ConvertCoordFromPositionToPixel(Point);

							lpPoints[w] = { REAL(toDraw.x), REAL(toDraw.y) };
							w++;
						}

						graphics.FillPolygon(&SolidBrush(Color(150, 0, 0)), lpPoints, w);
					}

					dc.SelectObject(oldPen);
				}
			}
		}
	}

	RimcasInstance->OnRefreshBegin(isLVP);

#pragma endregion


	CRadarTarget rt;
	for (rt = GetPlugIn()->RadarTargetSelectFirst(); rt.IsValid(); rt = GetPlugIn()->RadarTargetSelectNext(rt))
	{
		Color tagColor = GetTagColor(rt);

		Symbol::render(graphics, *this, rt, tagColor);

		Tag::render(graphics, *this, rt, tagColor);
	}

#pragma region RIMCAS
	TimePopupData.clear();
	AcOnRunway.clear();
	ColorAC.clear();
	tagAreas.clear();

	RimcasInstance->OnRefreshEnd(this, CurrentConfig->getActiveProfile()["rimcas"]["rimcas_stage_two_speed_threshold"].GetInt());

#pragma endregion


	

#pragma region RIMCAS
	CBrush BrushGrey(RGB(150, 150, 150));
	COLORREF oldColor = dc.SetTextColor(RGB(33, 33, 33));

	int TextHeight = dc.GetTextExtent("60").cy;
	Logger::info("RIMCAS Loop");
	for (std::map<string, bool>::iterator it = RimcasInstance->MonitoredRunwayArr.begin(); it != RimcasInstance->MonitoredRunwayArr.end(); ++it)
	{
		if (!it->second || RimcasInstance->TimeTable[it->first].empty())
			continue;

		vector<int> TimeDefinition = RimcasInstance->CountdownDefinition;
		if (isLVP)
			TimeDefinition = RimcasInstance->CountdownDefinitionLVP;

		if (TimePopupAreas.find(it->first) == TimePopupAreas.end())
			TimePopupAreas[it->first] = { 300, 300, 430, 300+LONG(TextHeight*(TimeDefinition.size()+1)) };

		CRect CRectTime = TimePopupAreas[it->first];
		CRectTime.NormalizeRect();

		dc.FillRect(CRectTime, &BrushGrey);

		// Drawing the runway name
		string tempS = it->first;
		dc.TextOutA(CRectTime.left + CRectTime.Width() / 2 - dc.GetTextExtent(tempS.c_str()).cx / 2, CRectTime.top, tempS.c_str());

		int TopOffset = TextHeight;
		// Drawing the times
		for (auto &Time : TimeDefinition)
		{
			dc.SetTextColor(RGB(33, 33, 33));

			tempS = std::to_string(Time) + ": " + RimcasInstance->TimeTable[it->first][Time];
			if (RimcasInstance->AcColor.find(RimcasInstance->TimeTable[it->first][Time]) != RimcasInstance->AcColor.end())
			{
				CBrush RimcasBrush(RimcasInstance->GetAircraftColor(RimcasInstance->TimeTable[it->first][Time],
					Color::Black,
					Color::Black,
					CurrentConfig->getConfigColor(CurrentConfig->getActiveProfile()["rimcas"]["background_color_stage_one"]),
					CurrentConfig->getConfigColor(CurrentConfig->getActiveProfile()["rimcas"]["background_color_stage_two"])).ToCOLORREF()
					);

				CRect TempRect = { CRectTime.left, CRectTime.top + TopOffset, CRectTime.right, CRectTime.top + TopOffset + TextHeight };
				TempRect.NormalizeRect();

				dc.FillRect(TempRect, &RimcasBrush);
				dc.SetTextColor(RGB(238, 238, 208));
			}

			dc.TextOutA(CRectTime.left, CRectTime.top + TopOffset, tempS.c_str());

			TopOffset += TextHeight;
		}

		AddScreenObject(RIMCAS_IAW, it->first.c_str(), CRectTime, true, "");

	}

#pragma endregion

#pragma region ToolbarLists

	Logger::info("Menu bar lists");

	if (ShowLists["Conflict Alert ARR"]) {
		GetPlugIn()->OpenPopupList(ListAreas["Conflict Alert ARR"], "CA Arrival", 1);
		for (std::map<string, CRimcas::RunwayAreaType>::iterator it = RimcasInstance->RunwayAreas.begin(); it != RimcasInstance->RunwayAreas.end(); ++it)
		{
			GetPlugIn()->AddPopupListElement(it->first.c_str(), "", RIMCAS_CA_ARRIVAL_FUNC, false, RimcasInstance->MonitoredRunwayArr[it->first.c_str()]);
		}
		GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		ShowLists["Conflict Alert ARR"] = false;
	}

	if (ShowLists["Conflict Alert DEP"]) {
		GetPlugIn()->OpenPopupList(ListAreas["Conflict Alert DEP"], "CA Departure", 1);
		for (std::map<string, CRimcas::RunwayAreaType>::iterator it = RimcasInstance->RunwayAreas.begin(); it != RimcasInstance->RunwayAreas.end(); ++it)
		{
			GetPlugIn()->AddPopupListElement(it->first.c_str(), "", RIMCAS_CA_MONITOR_FUNC, false, RimcasInstance->MonitoredRunwayDep[it->first.c_str()]);
		}
		GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		ShowLists["Conflict Alert DEP"] = false;
	}

	if (ShowLists["Runway closed"]) {
		GetPlugIn()->OpenPopupList(ListAreas["Runway closed"], "Runway Closed", 1);
		for (std::map<string, CRimcas::RunwayAreaType>::iterator it = RimcasInstance->RunwayAreas.begin(); it != RimcasInstance->RunwayAreas.end(); ++it)
		{
			GetPlugIn()->AddPopupListElement(it->first.c_str(), "", RIMCAS_CLOSED_RUNWAYS_FUNC, false, RimcasInstance->ClosedRunway[it->first.c_str()]);
		}
		GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		ShowLists["Runway closed"] = false;
	}

	if (ShowLists["Visibility"]) {
		GetPlugIn()->OpenPopupList(ListAreas["Visibility"], "Visibility", 1);
		GetPlugIn()->AddPopupListElement("Normal", "", RIMCAS_UPDATE_LVP, false, int(!isLVP));
		GetPlugIn()->AddPopupListElement("Low", "", RIMCAS_UPDATE_LVP, false, int(isLVP));
		GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		ShowLists["Visibility"] = false;
	}

	if (ShowLists["Profiles"]) {
		GetPlugIn()->OpenPopupList(ListAreas["Profiles"], "Profiles", 1);
		vector<string> allProfiles = CurrentConfig->getAllProfiles();
		for (std::vector<string>::iterator it = allProfiles.begin(); it != allProfiles.end(); ++it) {
			GetPlugIn()->AddPopupListElement(it->c_str(), "", RIMCAS_UPDATE_PROFILE, false, int(CurrentConfig->isItActiveProfile(it->c_str())));
		}
		GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		ShowLists["Profiles"] = false;
	}

	if (ShowLists["Colour Settings"]) {
		GetPlugIn()->OpenPopupList(ListAreas["Colour Settings"], "Colour Settings", 1);
		GetPlugIn()->AddPopupListElement("Day", "", RIMCAS_UPDATE_BRIGHNESS, false, int(ColorSettingsDay));
		GetPlugIn()->AddPopupListElement("Night", "", RIMCAS_UPDATE_BRIGHNESS, false, int(!ColorSettingsDay));
		GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		ShowLists["Colour Settings"] = false;
	}

	if (ShowLists["Label Font Size"]) {
		GetPlugIn()->OpenPopupList(ListAreas["Label Font Size"], "Label Font Size", 1);
		GetPlugIn()->AddPopupListElement("Size 1", "", RIMCAS_UPDATE_FONTS, false, int(bool(currentFontSize == 1)));
		GetPlugIn()->AddPopupListElement("Size 2", "", RIMCAS_UPDATE_FONTS, false, int(bool(currentFontSize == 2)));
		GetPlugIn()->AddPopupListElement("Size 3", "", RIMCAS_UPDATE_FONTS, false, int(bool(currentFontSize == 3)));
		GetPlugIn()->AddPopupListElement("Size 4", "", RIMCAS_UPDATE_FONTS, false, int(bool(currentFontSize == 4)));
		GetPlugIn()->AddPopupListElement("Size 5", "", RIMCAS_UPDATE_FONTS, false, int(bool(currentFontSize == 5)));
		GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		ShowLists["Label Font Size"] = false;
	}

	if (ShowLists["GRND Trail Dots"]) {
		GetPlugIn()->OpenPopupList(ListAreas["GRND Trail Dots"], "GRND Trail Dots", 1);
		GetPlugIn()->AddPopupListElement("0", "", RIMCAS_UPDATE_GND_TRAIL, false, int(bool(Trail_Gnd == 0)));
		GetPlugIn()->AddPopupListElement("2", "", RIMCAS_UPDATE_GND_TRAIL, false, int(bool(Trail_Gnd == 2)));
		GetPlugIn()->AddPopupListElement("4", "", RIMCAS_UPDATE_GND_TRAIL, false, int(bool(Trail_Gnd == 4)));
		GetPlugIn()->AddPopupListElement("8", "", RIMCAS_UPDATE_GND_TRAIL, false, int(bool(Trail_Gnd == 8)));
		GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		ShowLists["GRND Trail Dots"] = false;
	}

	if (ShowLists["APPR Trail Dots"]) {
		GetPlugIn()->OpenPopupList(ListAreas["APPR Trail Dots"], "APPR Trail Dots", 1);
		GetPlugIn()->AddPopupListElement("0", "", RIMCAS_UPDATE_APP_TRAIL, false, int(bool(Trail_App == 0)));
		GetPlugIn()->AddPopupListElement("4", "", RIMCAS_UPDATE_APP_TRAIL, false, int(bool(Trail_App == 4)));
		GetPlugIn()->AddPopupListElement("8", "", RIMCAS_UPDATE_APP_TRAIL, false, int(bool(Trail_App == 8)));
		GetPlugIn()->AddPopupListElement("12", "", RIMCAS_UPDATE_APP_TRAIL, false, int(bool(Trail_App == 12)));
		GetPlugIn()->AddPopupListElement("16", "", RIMCAS_UPDATE_APP_TRAIL, false, int(bool(Trail_App == 16)));
		GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		ShowLists["APPR Trail Dots"] = false;
	}

	if (ShowLists["Predicted Track Line"]) {
		GetPlugIn()->OpenPopupList(ListAreas["Predicted Track Line"], "Predicted Track Line", 1);
		GetPlugIn()->AddPopupListElement("0", "", RIMCAS_UPDATE_PTL, false, int(bool(PredictedLenght == 0)));
		GetPlugIn()->AddPopupListElement("1", "", RIMCAS_UPDATE_PTL, false, int(bool(PredictedLenght == 1)));
		GetPlugIn()->AddPopupListElement("2", "", RIMCAS_UPDATE_PTL, false, int(bool(PredictedLenght == 2)));
		GetPlugIn()->AddPopupListElement("3", "", RIMCAS_UPDATE_PTL, false, int(bool(PredictedLenght == 3)));
		GetPlugIn()->AddPopupListElement("4", "", RIMCAS_UPDATE_PTL, false, int(bool(PredictedLenght == 4)));
		GetPlugIn()->AddPopupListElement("5", "", RIMCAS_UPDATE_PTL, false, int(bool(PredictedLenght == 5)));
		GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		ShowLists["Predicted Track Line"] = false;
	}

	if (ShowLists["Brightness"])
	{
		GetPlugIn()->OpenPopupList(ListAreas["Brightness"], "Brightness", 1);
		GetPlugIn()->AddPopupListElement("Label", "", RIMCAS_OPEN_LIST, false);
		GetPlugIn()->AddPopupListElement("Symbol", "", RIMCAS_OPEN_LIST, false);
		GetPlugIn()->AddPopupListElement("Afterglow", "", RIMCAS_OPEN_LIST, false);
		GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		ShowLists["Brightness"] = false;
	}

	if (ShowLists["Label"])
	{
		GetPlugIn()->OpenPopupList(ListAreas["Label"], "Label Brightness", 1);
		for(int i = CColorManager::bounds_low(); i <= CColorManager::bounds_high(); i +=10)
			GetPlugIn()->AddPopupListElement(std::to_string(i).c_str(), "", RIMCAS_BRIGHTNESS_LABEL, false, int(bool(i == ColorManager->get_brightness("label"))));

		GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		ShowLists["Label"] = false;
	}

	if (ShowLists["Symbol"])
	{
		GetPlugIn()->OpenPopupList(ListAreas["Symbol"], "Symbol Brightness", 1);
		for (int i = CColorManager::bounds_low(); i <= CColorManager::bounds_high(); i += 10)
			GetPlugIn()->AddPopupListElement(std::to_string(i).c_str(), "", RIMCAS_BRIGHTNESS_SYMBOL, false, int(bool(i == ColorManager->get_brightness("symbol"))));

		GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		ShowLists["Symbol"] = false;
	}

	if (ShowLists["Afterglow"])
	{
		GetPlugIn()->OpenPopupList(ListAreas["Afterglow"], "Afterglow Brightness", 1);
		for (int i = CColorManager::bounds_low(); i <= CColorManager::bounds_high(); i += 10)
			GetPlugIn()->AddPopupListElement(std::to_string(i).c_str(), "", RIMCAS_BRIGHTNESS_AFTERGLOW, false, int(bool(i == ColorManager->get_brightness("afterglow"))));

		GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		ShowLists["Afterglow"] = false;
	}

#pragma endregion

#pragma region QRD

	Logger::info("QRD");

	//---------------------------------
	// QRD
	//---------------------------------

	if (QDMenabled || QDMSelectEnabled || (DistanceToolActive && ActiveDistance.first != "")) {
		CPen Pen(PS_SOLID, 1, RGB(255, 255, 255));
		CPen *oldPen = dc.SelectObject(&Pen);

		POINT AirportPos = ConvertCoordFromPositionToPixel(AirportPositions[getActiveAirport()]);
		CPosition AirportCPos = AirportPositions[getActiveAirport()];
		if (QDMSelectEnabled)
		{
			AirportPos = QDMSelectPt;
			AirportCPos = ConvertCoordFromPixelToPosition(QDMSelectPt);
		}
		if (DistanceToolActive)
		{
			CPosition r = GetPlugIn()->RadarTargetSelect(ActiveDistance.first.c_str()).GetPosition().GetPosition();
			AirportPos = ConvertCoordFromPositionToPixel(r);
			AirportCPos = r;
		}
		dc.MoveTo(AirportPos);
		POINT point = mouseLocation;
		dc.LineTo(point);

		CPosition CursorPos = ConvertCoordFromPixelToPosition(point);
		double Distance = AirportCPos.DistanceTo(CursorPos);
		double Bearing = AirportCPos.DirectionTo(CursorPos);
	
		Gdiplus::Pen WhitePen(Color::White);
		graphics.DrawEllipse(&WhitePen, point.x - 5, point.y - 5, 10, 10);

		Distance = Distance / 0.00053996f;

		Distance = round(Distance * 10) / 10;

		Bearing = round(Bearing * 10) / 10;

		POINT TextPos = { point.x + 20, point.y };

		if (!DistanceToolActive)
		{
			string distances = std::to_string(Distance);
			size_t decimal_pos = distances.find(".");
			distances = distances.substr(0, decimal_pos + 2);

			string bearings = std::to_string(Bearing);
			decimal_pos = bearings.find(".");
			bearings = bearings.substr(0, decimal_pos + 2);

			string text = bearings;
			text += "° / ";
			text += distances;
			text += "m";
			COLORREF old_color = dc.SetTextColor(RGB(255, 255, 255));
			dc.TextOutA(TextPos.x, TextPos.y, text.c_str());
			dc.SetTextColor(old_color);
		}

		dc.SelectObject(oldPen);
		RequestRefresh();
	}

#pragma endregion

#pragma region DistanceTools

	// Distance tools here
	for (auto&& kv : DistanceTools)
	{
		CRadarTarget one = GetPlugIn()->RadarTargetSelect(kv.first.c_str());
		CRadarTarget two = GetPlugIn()->RadarTargetSelect(kv.second.c_str());

		if (!isVisible(one) || !isVisible(two))
			continue;

		CPen Pen(PS_SOLID, 1, RGB(255, 255, 255));
		CPen *oldPen = dc.SelectObject(&Pen);

		POINT onePoint = ConvertCoordFromPositionToPixel(one.GetPosition().GetPosition());
		POINT twoPoint = ConvertCoordFromPositionToPixel(two.GetPosition().GetPosition());

		dc.MoveTo(onePoint);
		dc.LineTo(twoPoint);

		POINT TextPos = { twoPoint.x + 20, twoPoint.y };

		double Distance = one.GetPosition().GetPosition().DistanceTo(two.GetPosition().GetPosition());
		double Bearing = one.GetPosition().GetPosition().DirectionTo(two.GetPosition().GetPosition());

		string distances = std::to_string(Distance);
		size_t decimal_pos = distances.find(".");
		distances = distances.substr(0, decimal_pos + 2);

		string bearings = std::to_string(Bearing);
		decimal_pos = bearings.find(".");
		bearings = bearings.substr(0, decimal_pos + 2);

		string text = bearings;
		text += "° / ";
		text += distances;
		text += "nm";
		COLORREF old_color = dc.SetTextColor(RGB(0, 0, 0));

		CRect ClickableRect = { TextPos.x - 2, TextPos.y, TextPos.x + dc.GetTextExtent(text.c_str()).cx + 2, TextPos.y + dc.GetTextExtent(text.c_str()).cy };
		graphics.FillRectangle(&SolidBrush(Color(127, 122, 122)), CopyRect(ClickableRect));
		dc.Draw3dRect(ClickableRect, RGB(75, 75, 75), RGB(45, 45, 45));
		dc.TextOutA(TextPos.x, TextPos.y, text.c_str());

		AddScreenObject(RIMCAS_DISTANCE_TOOL, string(kv.first+","+kv.second).c_str(), ClickableRect, false, "");

		dc.SetTextColor(old_color);

		dc.SelectObject(oldPen);
	}

#pragma endregion

#pragma region Toolbar

	//---------------------------------
	// Drawing the toolbar
	//---------------------------------

	Logger::info("Menu Bar");

	COLORREF qToolBarColor = RGB(127, 122, 122);

	// Drawing the toolbar on the top
	CRect ToolBarAreaTop(RadarArea.left, RadarArea.top, RadarArea.right, RadarArea.top + 20);
	dc.FillSolidRect(ToolBarAreaTop, qToolBarColor);

	COLORREF oldTextColor = dc.SetTextColor(RGB(0, 0, 0));

	int offset = 2;
	dc.TextOutA(ToolBarAreaTop.left + offset, ToolBarAreaTop.top + 4, getActiveAirport().c_str());
	AddScreenObject(RIMCAS_ACTIVE_AIRPORT, "ActiveAirport", { ToolBarAreaTop.left + offset, ToolBarAreaTop.top + 4, ToolBarAreaTop.left + offset + dc.GetTextExtent(getActiveAirport().c_str()).cx, ToolBarAreaTop.top + 4 + dc.GetTextExtent(getActiveAirport().c_str()).cy }, false, "Active Airport");

	offset += dc.GetTextExtent(getActiveAirport().c_str()).cx + 10;
	dc.TextOutA(ToolBarAreaTop.left + offset, ToolBarAreaTop.top + 4, "Display");
	AddScreenObject(RIMCAS_MENU, "DisplayMenu", { ToolBarAreaTop.left + offset, ToolBarAreaTop.top + 4, ToolBarAreaTop.left + offset + dc.GetTextExtent("Display").cx, ToolBarAreaTop.top + 4 + dc.GetTextExtent("Display").cy }, false, "Display menu");

	offset += dc.GetTextExtent("Display").cx + 10;
	dc.TextOutA(ToolBarAreaTop.left + offset, ToolBarAreaTop.top + 4, "Target");
	AddScreenObject(RIMCAS_MENU, "TargetMenu", { ToolBarAreaTop.left + offset, ToolBarAreaTop.top + 4, ToolBarAreaTop.left + offset + dc.GetTextExtent("Target").cx, ToolBarAreaTop.top + 4 + dc.GetTextExtent("Target").cy }, false, "Target menu");

	offset += dc.GetTextExtent("Target").cx + 10;
	dc.TextOutA(ToolBarAreaTop.left + offset, ToolBarAreaTop.top + 4, "Colours");
	AddScreenObject(RIMCAS_MENU, "ColourMenu", { ToolBarAreaTop.left + offset, ToolBarAreaTop.top + 4, ToolBarAreaTop.left + offset + dc.GetTextExtent("Colour").cx, ToolBarAreaTop.top + 4 + dc.GetTextExtent("Colour").cy }, false, "Colour menu");

	offset += dc.GetTextExtent("Colours").cx + 10;
	dc.TextOutA(ToolBarAreaTop.left + offset, ToolBarAreaTop.top + 4, "Alerts");
	AddScreenObject(RIMCAS_MENU, "RIMCASMenu", { ToolBarAreaTop.left + offset, ToolBarAreaTop.top + 4, ToolBarAreaTop.left + offset + dc.GetTextExtent("Alerts").cx, ToolBarAreaTop.top + 4 + +dc.GetTextExtent("Alerts").cy }, false, "RIMCAS menu");

	offset += dc.GetTextExtent("Alerts").cx + 10;
	dc.TextOutA(ToolBarAreaTop.left + offset, ToolBarAreaTop.top + 4, "/");
	CRect barDistanceRect = { ToolBarAreaTop.left + offset - 2, ToolBarAreaTop.top + 4, ToolBarAreaTop.left + offset + dc.GetTextExtent("/").cx, ToolBarAreaTop.top + 4 + +dc.GetTextExtent("/").cy };
	if (DistanceToolActive)
	{
		graphics.DrawRectangle(&Pen(Color::White), CopyRect(barDistanceRect));
	}
	AddScreenObject(RIMCAS_MENU, "/", barDistanceRect, false, "Distance tool");

	dc.SetTextColor(oldTextColor);

#pragma endregion

#pragma region AppWindows

	//
	// App windows
	//

	Logger::info("App window rendering");

	for (std::map<int, bool>::iterator it = appWindowDisplays.begin(); it != appWindowDisplays.end(); ++it)
	{
		if (!it->second)
			continue;

		int appWindowId = it->first;
		appWindows[appWindowId]->render(hDC, this, &graphics, mouseLocation, DistanceTools);
	}

#pragma endregion

	graphics.ReleaseHDC(hDC);
	dc.Detach();

	Logger::info("END "+ string(__FUNCSIG__));

}

Color GetTagColor(CRadarTarget& rt)
{
	auto fp = rt.GetCorrelatedFlightPlan();
	if (fp.IsValid())
	{
		int sectorEntry = fp.GetSectorEntryMinutes();

		if (sectorEntry >= 0 && sectorEntry <= 10)
		{
			// Green
			return Color(108, 245, 113);
		}

		if (sectorEntry > 10 && sectorEntry <= 20)
		{
			//Blue
			return Color(137, 207, 240);
		}

	}
	// Default
	return Color::White;
}

//---EuroScopePlugInExitCustom-----------------------------------------------

void CSMRRadar::EuroScopePlugInExitCustom()
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState())

		if (smrCursor != nullptr && smrCursor != NULL)
		{
			SetWindowLong(pluginWindow, GWL_WNDPROC, (LONG)gSourceProc);
		}
}
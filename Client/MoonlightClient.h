#pragma once
#include "pch.h"
#include "Common/DeviceResources.h"
#include <Streaming\FramePacer.h>
#include <Client/StreamConfiguration.h>
#include "State\MoonlightApp.h"

extern "C" {
	#include <libavcodec/avcodec.h>
	#include <Limelight.h>
	#include<libgamestream/client.h>
}

typedef void(*MoonlightErrorCallback)(const char *msg);
namespace moonlight_xbox_dx {
	class MoonlightClient
	{
	public:
		MoonlightClient();
		int StartStreaming(std::shared_ptr<DX::DeviceResources> res, StreamConfiguration ^config);
		int Connect(const char* hostname);
		bool IsPaired();
		int Pair();
		char *GeneratePIN();
		std::vector<MoonlightApp^> MoonlightClient::GetApplications();
		void SendGamepadReading(short controllerNumber, Windows::Gaming::Input::GamepadReading reading);
		void SendMousePosition(float x, float y);
		void SendMousePressed(int button);
		void SendMouseReleased(int button);
		void SendScroll(float value);
		void SetSoftwareEncoder(bool value);
		void SetGamepadCount(short count);
		int GetRunningAppID();
		void StopStreaming();
		void StopApp();
		void Unpair();
		FramePacer *pacer;
		Platform::String^ GetInstanceID();
	private:
		SERVER_DATA serverData;
		char* connectionPin = NULL;
		char* hostname = NULL;
		bool useSoftwareEncoder = false;
		int activeGamepadMask = 0;
	};
}
#pragma once
#include <string>
ref class StreamConfiguration {
public:
	property Platform::String^ hostname;
	property int appID;
	property int width;
	property int height;
	property int bitrate;
	property int FPS;
	property bool supportsHevc;
    property Platform::String^ audioConfig;
	property Platform::String^ videoCodec;
};

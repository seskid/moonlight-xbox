#pragma once
#include <string>


namespace moonlight_xbox_dx
{

	ref class XboxKeyboard {
	public:
		property int keyCode;
	    property bool pressed;
		property int modifiers;
	};
}
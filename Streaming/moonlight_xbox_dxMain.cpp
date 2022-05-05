#include "pch.h"
#include "moonlight_xbox_dxMain.h"
#include "Common\DirectXHelper.h"
#include "Keyboard.h"
#include "Utils.hpp"
using namespace DirectX;
using namespace Windows::Gaming::Input;


using namespace moonlight_xbox_dx;
using namespace Windows::Foundation;
using namespace Windows::System::Threading;
using namespace Windows::UI::Core;
using namespace Concurrency;

// Loads and initializes application assets when the application is loaded.
moonlight_xbox_dxMain::moonlight_xbox_dxMain(const std::shared_ptr<DX::DeviceResources>& deviceResources, Windows::UI::Xaml::FrameworkElement^ flyoutButton, Windows::UI::Xaml::Controls::MenuFlyout^ flyout, Windows::UI::Core::CoreDispatcher^ dispatcher, MoonlightClient* client, StreamConfiguration^ configuration) :

	m_deviceResources(deviceResources), m_pointerLocationX(0.0f), m_flyoutButton(flyoutButton), m_dispatcher(dispatcher), m_flyout(flyout), moonlightClient(client)
{
	// Register to be notified if the Device is lost or recreated
	m_deviceResources->RegisterDeviceNotify(this);

	m_sceneRenderer = std::unique_ptr<VideoRenderer>(new VideoRenderer(m_deviceResources, moonlightClient, configuration));

	m_fpsTextRenderer = std::unique_ptr<LogRenderer>(new LogRenderer(m_deviceResources));

	m_statsTextRenderer = std::unique_ptr<StatsRenderer>(new StatsRenderer(m_deviceResources));

	// TODO: Change the timer settings if you want something other than the default variable timestep mode.
	// e.g. for 60 FPS fixed timestep update logic, call:
	/*
	*/
	m_timer.SetFixedTimeStep(false);
	//m_timer.SetTargetElapsedSeconds(1.0 / 60);

	auto coreWindow = CoreWindow::GetForCurrentThread();
	m_keyboard = std::make_unique<Keyboard>();
	m_keyboard->SetWindow(reinterpret_cast<ABI::Windows::UI::Core::ICoreWindow*>(coreWindow));

	xboxKeyboard = ref new XboxKeyboard();
}

moonlight_xbox_dxMain::~moonlight_xbox_dxMain()
{
	// Deregister device notification
	m_deviceResources->RegisterDeviceNotify(nullptr);
}

// Updates application state when the window size changes (e.g. device orientation change)
void moonlight_xbox_dxMain::CreateWindowSizeDependentResources()
{
	// TODO: Replace this with the size-dependent initialization of your app's content.
	m_sceneRenderer->CreateWindowSizeDependentResources();
}

void moonlight_xbox_dxMain::StartRenderLoop()
{
	// If the animation render loop is already running then do not start another thread.
	if (m_renderLoopWorker != nullptr && m_renderLoopWorker->Status == AsyncStatus::Started)
	{
		return;
	}

	// Create a task that will be run on a background thread.
	auto workItemHandler = ref new WorkItemHandler([this](IAsyncAction^ action)
		{
			// Calculate the updated frame and render once per vertical blanking interval.
			while (action->Status == AsyncStatus::Started)
			{
				critical_section::scoped_lock lock(m_criticalSection);
				int t1 = GetTickCount64();
				Update();
				if (Render())
				{
					m_deviceResources->Present();
				}
			}
		});
	m_renderLoopWorker = ThreadPool::RunAsync(workItemHandler, WorkItemPriority::High, WorkItemOptions::TimeSliced);
	if (m_inputLoopWorker != nullptr && m_inputLoopWorker->Status == AsyncStatus::Started) {
		return;
	}
	auto inputItemHandler = ref new WorkItemHandler([this](IAsyncAction^ action)
		{
			// Calculate the updated frame and render once per vertical blanking interval.
			while (action->Status == AsyncStatus::Started)
			{
				ProcessInput();
				Sleep(8); // 8ms = about 120Hz of polling rate (i guess)
			}
		});

	// Run task on a dedicated high priority background thread.
	m_inputLoopWorker = ThreadPool::RunAsync(inputItemHandler, WorkItemPriority::High, WorkItemOptions::TimeSliced);
}

void moonlight_xbox_dxMain::StopRenderLoop()
{
	m_renderLoopWorker->Cancel();
	m_inputLoopWorker->Cancel();
}

// Updates the application state once per frame.
void moonlight_xbox_dxMain::Update()
{

	// Update scene objects.
	m_timer.Tick([&]()
		{
			m_sceneRenderer->Update(m_timer);
			m_fpsTextRenderer->Update(m_timer);
			m_statsTextRenderer->Update(m_timer);
		});
}

// Process all input from the user before updating game state
void moonlight_xbox_dxMain::ProcessInput()
{

	auto gamepads = Windows::Gaming::Input::Gamepad::Gamepads;
	//if (gamepads->Size == 0)return;
	moonlightClient->SetGamepadCount(gamepads->Size);
	for (int i = 0; i < gamepads->Size; i++) {
		Windows::Gaming::Input::Gamepad^ gamepad = gamepads->GetAt(i);
		auto reading = gamepad->GetCurrentReading();
		//If this combination is pressed on gamed we should handle some magic things :)
		GamepadButtons magicKey[] = { GamepadButtons::Menu,GamepadButtons::View };
		bool isCurrentlyPressed = true;
		for (auto k : magicKey) {
			if ((reading.Buttons & k) != k) {
				isCurrentlyPressed = false;
				break;
			}
		}
		if (isCurrentlyPressed) {
			m_dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([this]() {
				Windows::UI::Xaml::Controls::Flyout::ShowAttachedFlyout(m_flyoutButton);
				}));
			insideFlyout = true;
		}
		if (insideFlyout)return;
		//If mouse mode is enabled the gamepad acts as a mouse, instead we pass the raw events to the host
		if (mouseMode) {
			//Position

			moonlightClient->SendMousePosition(reading.LeftThumbstickX * 5, reading.LeftThumbstickY * -5);
			//Left Click
			if ((reading.Buttons & GamepadButtons::A) == GamepadButtons::A) {
				if (!leftMouseButtonPressed) {
					leftMouseButtonPressed = true;
					moonlightClient->SendMousePressed(BUTTON_LEFT);
				}
			}
			else if (leftMouseButtonPressed) {
				leftMouseButtonPressed = false;
				moonlightClient->SendMouseReleased(BUTTON_LEFT);
			}
			//Right Click
			if ((reading.Buttons & GamepadButtons::X) == GamepadButtons::X) {
				if (!rightMouseButtonPressed) {
					rightMouseButtonPressed = true;
					moonlightClient->SendMousePressed(BUTTON_RIGHT);
				}
			}
			else if (rightMouseButtonPressed) {
				rightMouseButtonPressed = false;
				moonlightClient->SendMouseReleased(BUTTON_RIGHT);
			}
			//Scroll
			moonlightClient->SendScroll(reading.RightThumbstickY);
		}
		else {
			moonlightClient->SendGamepadReading(i, reading);
		}
	}
    
	auto kb = m_keyboard->GetState();
    Keyboard::KeyboardStateTracker tracker;
    tracker.Update(kb);
	
	xboxKeyboard->pressed = false;
	xboxKeyboard->modifiers = 0;

	if (kb.Space) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_SPACE;
	}

	if (kb.Enter) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_RETURN;
	}
	if (kb.Tab) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_TAB;
	}
	if (kb.Back) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_BACK;
	}
	if (kb.Escape) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_ESCAPE;
	}
	if (kb.Left) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_LEFT;
	}
	if (kb.Right) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_RIGHT;
	}
	if (kb.Down) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_DOWN;
	}
	if (kb.Up) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_UP;
	}

	//OEM's
	if (kb.OemSemicolon) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_OEM_1;
	}
	if (kb.OemPlus) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_OEM_PLUS;
	}
	if (kb.OemMinus) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_OEM_MINUS;
	}
	if (kb.OemComma) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_OEM_COMMA;
	}
	if (kb.OemPeriod) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_OEM_PERIOD;
	}
	if (kb.OemQuestion) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_OEM_2;
	}
	if (kb.OemTilde) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_OEM_3;
	}
	if (kb.OemOpenBrackets) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_OEM_4;
	}
	if (kb.OemPipe) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_OEM_5;
	}
	if (kb.OemBackslash) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_OEM_102;
	}
	if (kb.OemCloseBrackets) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_OEM_6;
	}
	if (kb.OemQuotes) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_OEM_7;
	}

	//numeric
	if (kb.D0) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X30;
	}
	if (kb.D1) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X31;
	}
	if (kb.D2) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X32;
	}
	if (kb.D3) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X33;
	}
	if (kb.D4) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X34;
	}
	if (kb.D5) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X35;
	}
	if (kb.D6) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X36;
	}
	if (kb.D7) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X37;
	}
	if (kb.D8) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X38;
	}
	if (kb.D9) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X39;
	}

	if (kb.NumPad0) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_NUMPAD0;
	}
	if (kb.NumPad1) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_NUMPAD1;
	}
	if (kb.NumPad2) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_NUMPAD2;
	}
	if (kb.NumPad3) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_NUMPAD3;
	}
	if (kb.NumPad4) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_NUMPAD4;
	}
	if (kb.NumPad5) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_NUMPAD5;
	}
	if (kb.NumPad6) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_NUMPAD6;
	}
	if (kb.NumPad7) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_NUMPAD7;
	}
	if (kb.NumPad8) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_NUMPAD8;
	}
	if (kb.NumPad9) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_NUMPAD9;
	}

	//function keys
	if (kb.F1) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_F1;
	}
	if (kb.F2) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_F2;
	}
	if (kb.F1) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_F3;
	}
	if (kb.F4) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_F4;
	}
	if (kb.F5) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_F5;
	}
	if (kb.F6) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_F6;
	}
	if (kb.F7) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_F7;
	}
	if (kb.F8) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_F8;
	}
	if (kb.F9) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_F9;
	}
	if (kb.F10) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_F10;
	}
	if (kb.F11) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_F11;
	}
	if (kb.F12) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_F12;
	}
	


	
	//letters
	if (kb.A) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X41;
	}

	if (kb.B) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X42;
	}
	if (kb.C) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X43;
	}
	if (kb.D) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X44;
	}

	if (kb.E) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X45;
	}
	if (kb.F) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X46;
	}
	if (kb.G) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X47;
	}

	if (kb.H) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X48;
	}
	if (kb.I) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X49;
	}
	if (kb.J) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X4A;
	}

	if (kb.K) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X4B;
	}
	if (kb.L) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X4C;
	}
	if (kb.M) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X4D;
	}

	if (kb.N) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X4E;
	}
	if (kb.O) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X4F;
	}
	if (kb.P) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X50;
	}

	if (kb.Q) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X51;
	}
	if (kb.R) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X52;
	}
	if (kb.S) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X53;
	}
	if (kb.T) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X54;
	}
	if (kb.U) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X55;
	}
	if (kb.V) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X56;
	}
	if (kb.W) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X57;
	}
	if (kb.X) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X58;
	}
	if (kb.Y) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X59;
	}
	if (kb.Z) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = 0X5A;
	}
	
	//modifies
	if (kb.CapsLock) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_CAPITAL;
	}
	if (kb.NumLock) {
		xboxKeyboard->pressed = true;
		xboxKeyboard->keyCode = VK_NUMLOCK;
	}
	if (kb.LeftShift) {
		xboxKeyboard->keyCode = VK_LSHIFT;
		xboxKeyboard->modifiers =1;
	}
	if (kb.RightShift) {
		xboxKeyboard->keyCode = VK_RSHIFT;
		xboxKeyboard->modifiers =1;
	}
	
	if (kb.LeftAlt) {
		xboxKeyboard->keyCode = 0XA4;
		xboxKeyboard->modifiers = 1;
	}
	if (kb.RightAlt) {
	
		xboxKeyboard->keyCode = 0XA5;
		xboxKeyboard->modifiers=1;
	}
	if (kb.LeftControl) {
		xboxKeyboard->keyCode = VK_LCONTROL;
		xboxKeyboard->modifiers =1;
	}
	if (kb.RightControl) {
		xboxKeyboard->keyCode = VK_RCONTROL;
		xboxKeyboard->modifiers =1;
	}

	moonlightClient->SendKeyBoardEvent(xboxKeyboard);
}


// Renders the current frame according to the current application state.
// Returns true if the frame was rendered and is ready to be displayed.
bool moonlight_xbox_dxMain::Render()
{
	// Don't try to render anything before the first Update.
	if (m_timer.GetFrameCount() == 0)
	{
		return false;
	}

	auto context = m_deviceResources->GetD3DDeviceContext();

	// Reset the viewport to target the whole screen.
	auto viewport = m_deviceResources->GetScreenViewport();
	context->RSSetViewports(1, &viewport);

	// Reset render targets to the screen.
	ID3D11RenderTargetView* const targets[1] = { m_deviceResources->GetBackBufferRenderTargetView() };
	context->OMSetRenderTargets(1, targets, m_deviceResources->GetDepthStencilView());

	// Clear the back buffer and depth stencil view.
	context->ClearRenderTargetView(m_deviceResources->GetBackBufferRenderTargetView(), DirectX::Colors::CornflowerBlue);
	context->ClearDepthStencilView(m_deviceResources->GetDepthStencilView(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	// Render the scene objects.
	m_sceneRenderer->Render();
	m_fpsTextRenderer->Render();
	m_statsTextRenderer->Render();

	return true;
}

// Notifies renderers that device resources need to be released.
void moonlight_xbox_dxMain::OnDeviceLost()
{
	m_sceneRenderer->ReleaseDeviceDependentResources();
	m_fpsTextRenderer->ReleaseDeviceDependentResources();
	m_statsTextRenderer->ReleaseDeviceDependentResources();
}

// Notifies renderers that device resources may now be recreated.
void moonlight_xbox_dxMain::OnDeviceRestored()
{
	m_sceneRenderer->CreateDeviceDependentResources();
	m_fpsTextRenderer->CreateDeviceDependentResources();
	m_statsTextRenderer->CreateDeviceDependentResources();
	CreateWindowSizeDependentResources();
}

void moonlight_xbox_dxMain::SetFlyoutOpened(bool value) {
	insideFlyout = value;
}

void moonlight_xbox_dxMain::Disconnect() {
	moonlightClient->StopStreaming();
}

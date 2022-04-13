﻿#include "pch.h"
#include "moonlight_xbox_dxMain.h"
#include "Common\DirectXHelper.h"
#include "Utils.hpp"
using namespace Windows::Gaming::Input;


using namespace moonlight_xbox_dx;
using namespace Windows::Foundation;
using namespace Windows::System::Threading;
using namespace Concurrency;

// Loads and initializes application assets when the application is loaded.
moonlight_xbox_dxMain::moonlight_xbox_dxMain(const std::shared_ptr<DX::DeviceResources>& deviceResources, Windows::UI::Xaml::FrameworkElement^ flyoutButton, Windows::UI::Xaml::Controls::MenuFlyout^ flyout, Windows::UI::Core::CoreDispatcher^ dispatcher, MoonlightClient* client,StreamConfiguration ^configuration):

	m_deviceResources(deviceResources), m_pointerLocationX(0.0f),m_flyoutButton(flyoutButton),m_dispatcher(dispatcher),m_flyout(flyout),moonlightClient(client)
{
	// Register to be notified if the Device is lost or recreated
	m_deviceResources->RegisterDeviceNotify(this);

	m_sceneRenderer = std::unique_ptr<VideoRenderer>(new (m_deviceResources,moonlightClient,configuration));

	m_fpsTextRenderer = std::unique_ptr<LogRenderer>(new LogRenderer(m_deviceResources));

	m_statsTextRenderer = std::unique_ptr<StatsRenderer>(new StatsRenderer(m_deviceResources));

	// TODO: Change the timer settings if you want something other than the default variable timestep mode.
	// e.g. for 60 FPS fixed timestep update logic, call:
	/*
	*/
	m_timer.SetFixedTimeStep(false);
	//m_timer.SetTargetElapsedSeconds(1.0 / 60);
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
	auto workItemHandler = ref new WorkItemHandler([this](IAsyncAction ^ action)
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
	if (gamepads->Size == 0)return;
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
	ID3D11RenderTargetView *const targets[1] = { m_deviceResources->GetBackBufferRenderTargetView() };
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
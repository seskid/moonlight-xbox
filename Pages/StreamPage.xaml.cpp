﻿//
// DirectXPage.xaml.cpp
// Implementation of the DirectXPage class.
//

#include "pch.h"
#include "StreamPage.xaml.h"
#include <Utils.hpp>

using namespace moonlight_xbox_dx;

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Graphics::Display;
using namespace Windows::System::Threading;
using namespace Windows::UI::Core;
using namespace Windows::UI::Input;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Controls::Primitives;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Navigation;
using namespace concurrency;

StreamPage::StreamPage():
	m_windowVisible(true),
	m_coreInput(nullptr)
{
	auto navigation = Windows::UI::Core::SystemNavigationManager::GetForCurrentView();
	navigation->BackRequested += ref new EventHandler<BackRequestedEventArgs^>(this, &StreamPage::OnBackRequested);
	InitializeComponent();

	DisplayInformation^ currentDisplayInformation = DisplayInformation::GetForCurrentView();

	swapChainPanel->SizeChanged +=
		ref new SizeChangedEventHandler(this, &StreamPage::OnSwapChainPanelSizeChanged);

	//Resize to make fullscreen on Xbox
	Windows::UI::ViewManagement::ApplicationView::GetForCurrentView()->SetDesiredBoundsMode(Windows::UI::ViewManagement::ApplicationViewBoundsMode::UseCoreWindow);
}


StreamPage::~StreamPage()
{
	// Stop rendering and processing events on destruction.
	m_main->StopRenderLoop();
}

void StreamPage::OnBackRequested(Platform::Object^ e,Windows::UI::Core::BackRequestedEventArgs^ args)
{
	// UWP on Xbox One triggers a back request whenever the B
	// button is pressed which can result in the app being
	// suspended if unhandled
	args->Handled = true;
}

void StreamPage::Page_Loaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	try {
		// At this point we have access to the device. 
		// We can create the device-dependent resources.
		m_deviceResources = std::make_shared<DX::DeviceResources>();
		m_deviceResources->SetSwapChainPanel(swapChainPanel);
		m_main = std::unique_ptr<moonlight_xbox_dxMain>(new moonlight_xbox_dxMain(m_deviceResources,this->flyoutButton,this->ActionsFlyout,Dispatcher,new MoonlightClient(),configuration));
		m_main->CreateWindowSizeDependentResources();
		m_main->StartRenderLoop();
	}
	catch (const std::exception & ex) {
		Windows::UI::Xaml::Controls::ContentDialog^ dialog = ref new Windows::UI::Xaml::Controls::ContentDialog();
		dialog->Content = Utils::StringPrintf(ex.what());
		dialog->CloseButtonText = L"OK";
		dialog->ShowAsync();
	}
	catch (const std::string & string) {
		Windows::UI::Xaml::Controls::ContentDialog^ dialog = ref new Windows::UI::Xaml::Controls::ContentDialog();
		dialog->Content = Utils::StringPrintf(string.c_str());
		dialog->CloseButtonText = L"OK";
		dialog->ShowAsync();
	}
	catch (Platform::Exception ^ e) {
		Windows::UI::Xaml::Controls::ContentDialog^ dialog = ref new Windows::UI::Xaml::Controls::ContentDialog();
		Platform::String^ errorMsg = ref new Platform::String();
		errorMsg = errorMsg->Concat(L"Exception: ", e->Message);
		errorMsg = errorMsg->Concat(errorMsg,Utils::StringPrintf("%x",e->HResult));
		dialog->Content = errorMsg;
		dialog->CloseButtonText = L"OK";
		dialog->ShowAsync();
	}
	catch (...) {
		std::exception_ptr eptr;
		eptr = std::current_exception(); // capture
		Windows::UI::Xaml::Controls::ContentDialog^ dialog = ref new Windows::UI::Xaml::Controls::ContentDialog();
		dialog->Content = L"Generic Exception";
		dialog->CloseButtonText = L"OK";
		dialog->ShowAsync();
	}
}

void StreamPage::OnSwapChainPanelSizeChanged(Object^ sender, Windows::UI::Xaml::SizeChangedEventArgs^ e)
{
	if (m_main == nullptr || m_deviceResources == nullptr)return;
	critical_section::scoped_lock lock(m_main->GetCriticalSection());
	m_deviceResources->SetLogicalSize(e->NewSize);
	m_main->CreateWindowSizeDependentResources();
}


void moonlight_xbox_dx::StreamPage::flyoutButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	Windows::UI::Xaml::Controls::Flyout::ShowAttachedFlyout((FrameworkElement^)sender);
	m_main->SetFlyoutOpened(true);
}


void moonlight_xbox_dx::StreamPage::ActionsFlyout_Closed(Platform::Object^ sender, Platform::Object^ e)
{
	if(m_main != nullptr) m_main->SetFlyoutOpened(false);
}


void moonlight_xbox_dx::StreamPage::toggleMouseButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	m_main->mouseMode = !m_main->mouseMode;
	this->toggleMouseButton->Text = m_main->mouseMode ? "Exit Mouse Mode" : "Toggle Mouse Mode";
}


void moonlight_xbox_dx::StreamPage::toggleLogsButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	Utils::showLogs = !Utils::showLogs;
	this->toggleLogsButton->Text = Utils::showLogs ? "Hide Logs" : "Show Logs";
}

void StreamPage::OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs^ e) {
	configuration = dynamic_cast<StreamConfiguration^>(e->Parameter);
	SetStreamConfig(configuration);
	if (configuration == nullptr)return;
}

void moonlight_xbox_dx::StreamPage::toggleStatsButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	Utils::showStats = !Utils::showStats;
	this->toggleStatsButton->Text = Utils::showStats ? "Hide Stats" : "Show Stats";
}


void moonlight_xbox_dx::StreamPage::disonnectButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	this->m_main->StopRenderLoop();
	this->m_main->Disconnect();
	this->Frame->GoBack();
}


void moonlight_xbox_dx::StreamPage::flyoutButton_Click_1(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{

}

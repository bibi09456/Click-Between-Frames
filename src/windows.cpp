#include <queue>
#include <algorithm>
#include <limits>
#include <mutex>

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingEvent.hpp>

#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/EndLevelLayer.hpp>

#include <geode.custom-keybinds/include/Keybinds.hpp>

#include "platform.hpp"

using namespace geode::prelude;

enum GameAction : int {
	p1Jump = 0,
	p1Left = 1,
	p1Right = 2,
	p2Jump = 3,
	p2Left = 4,
	p2Right = 5
};

std::unordered_set<size_t> inputBinds[6];
std::unordered_set<USHORT> heldInputs;

cbf::TimestampType cbf::getCurrentTime() {
	LARGE_INTEGER time;
	QueryPerformanceCounter(&time);
	return time.QuadPart;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	LARGE_INTEGER time;
	PlayerButton inputType;
	cbf::InputState inputState;
	cbf::Player player;
	auto& manager = cbf::Manager::get();

	QueryPerformanceCounter(&time);
	
	LPVOID pData;
	switch (uMsg) {
	case WM_INPUT: {
		UINT dwSize;
		GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER));

		auto lpb = std::make_unique<BYTE[]>(dwSize);
		if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, lpb.get(), &dwSize, sizeof(RAWINPUTHEADER)) != dwSize) {
			log::debug("GetRawInputData does not return correct size");
		}

		RAWINPUT* raw = (RAWINPUT*)lpb.get();
		switch (raw->header.dwType) {
		case RIM_TYPEKEYBOARD: {
			USHORT vkey = raw->data.keyboard.VKey;
			inputState = static_cast<cbf::InputState>(raw->data.keyboard.Flags & RI_KEY_BREAK);

			// cocos2d::enumKeyCodes corresponds directly to vkeys
			if (heldInputs.contains(vkey)) {
				if (!inputState) return 0;
				else heldInputs.erase(vkey);
			}
			
			bool shouldEmplace = true;
			player = cbf::Player::Player1;

			{
				std::lock_guard lock(manager.keybindsLock);

				if (inputBinds[p1Jump].contains(vkey)) inputType = PlayerButton::Jump;
				else if (inputBinds[p1Left].contains(vkey)) inputType = PlayerButton::Left;
				else if (inputBinds[p1Right].contains(vkey)) inputType = PlayerButton::Right;
				else {
					player = cbf::Player::Player2;
					if (inputBinds[p2Jump].contains(vkey)) inputType = PlayerButton::Jump;
					else if (inputBinds[p2Left].contains(vkey)) inputType = PlayerButton::Left;
					else if (inputBinds[p2Right].contains(vkey)) inputType = PlayerButton::Right;
					else shouldEmplace = false;
				}
				if (!inputState) heldInputs.emplace(vkey);
			}

			if (!shouldEmplace) return 0; // has to be done outside of the critical section
			break;
		}
		case RIM_TYPEMOUSE: {
			USHORT flags = raw->data.mouse.usButtonFlags;
			bool shouldEmplace = true;
			player = cbf::Player::Player1;
			inputType = PlayerButton::Jump;

			bool rc;
			{
				std::lock_guard lock(manager.keybindsLock);
				rc = manager.enableRightClick;
			}

			if (flags & RI_MOUSE_BUTTON_1_DOWN) inputState = cbf::InputState::Press;
			else if (flags & RI_MOUSE_BUTTON_1_UP) inputState = cbf::InputState::Release;
			else {
				player = cbf::Player::Player2;
				if (!rc) return 0;
				if (flags & RI_MOUSE_BUTTON_2_DOWN) inputState = cbf::InputState::Press;
				else if (flags & RI_MOUSE_BUTTON_2_UP) inputState = cbf::InputState::Release;
				else return 0;
			}
			break;
		}
		default:
			return 0;
		}
		break;
	} 
	default:
		return DefWindowProcA(hwnd, uMsg, wParam, lParam);
	}

    manager.addInput(cbf::Input{ time.QuadPart, inputType, inputState, player });

	// prevent input from going through
	return 0;
}

void inputThread() {
	WNDCLASS wc = {};
	wc.lpfnWndProc = WindowProc;
	wc.hInstance = GetModuleHandleA(NULL);
	wc.lpszClassName = "Click Between Frames";

	RegisterClass(&wc);
	HWND hwnd = CreateWindow("Click Between Frames", "Raw Input Window", 0, 0, 0, 0, 0, HWND_MESSAGE, 0, wc.hInstance, 0);
	if (!hwnd) {
		const DWORD err = GetLastError();
		log::error("Failed to create raw input window: {}", err);
		return;
	}

	RAWINPUTDEVICE dev[2];
	dev[0].usUsagePage = 0x01;        // generic desktop controls
	dev[0].usUsage = 0x06;            // keyboard
	dev[0].dwFlags = RIDEV_INPUTSINK; // allow inputs without being in the foreground
	dev[0].hwndTarget = hwnd;         // raw input window

	dev[1].usUsagePage = 0x01;
	dev[1].usUsage = 0x02;            // mouse
	dev[1].dwFlags = RIDEV_INPUTSINK;
	dev[1].hwndTarget = hwnd;

	if (!RegisterRawInputDevices(dev, 2, sizeof(dev[0]))) {
		log::error("Failed to register raw input devices");
		return;
	}

	MSG msg;
	while (GetMessage(&msg, hwnd, 0, 0)) {
		DispatchMessage(&msg);
	}
}

$on_mod(Loaded) {
    std::thread(inputThread).detach();
}

void updateKeybinds() {
	std::vector<geode::Ref<keybinds::Bind>> v;
	auto& manager = cbf::Manager::get();

	std::lock_guard lock(manager.keybindsLock);

	manager.enableRightClick = Mod::get()->getSettingValue<bool>("right-click");
	inputBinds->clear();

	v = keybinds::BindManager::get()->getBindsFor("robtop.geometry-dash/jump-p1");
	for (int i = 0; i < v.size(); i++) inputBinds[p1Jump].emplace(v[i]->getHash());

	v = keybinds::BindManager::get()->getBindsFor("robtop.geometry-dash/move-left-p1");
	for (int i = 0; i < v.size(); i++) inputBinds[p1Left].emplace(v[i]->getHash());

	v = keybinds::BindManager::get()->getBindsFor("robtop.geometry-dash/move-right-p1");
	for (int i = 0; i < v.size(); i++) inputBinds[p1Right].emplace(v[i]->getHash());

	v = keybinds::BindManager::get()->getBindsFor("robtop.geometry-dash/jump-p2");
	for (int i = 0; i < v.size(); i++) inputBinds[p2Jump].emplace(v[i]->getHash());

	v = keybinds::BindManager::get()->getBindsFor("robtop.geometry-dash/move-left-p2");
	for (int i = 0; i < v.size(); i++) inputBinds[p2Left].emplace(v[i]->getHash());

	v = keybinds::BindManager::get()->getBindsFor("robtop.geometry-dash/move-right-p2");
	for (int i = 0; i < v.size(); i++) inputBinds[p2Right].emplace(v[i]->getHash());
}

class $modify(PlayLayer) {
	bool init(GJGameLevel *level, bool useReplay, bool dontCreateObjects) {
		updateKeybinds();
		return PlayLayer::init(level, useReplay, dontCreateObjects);
	}
};
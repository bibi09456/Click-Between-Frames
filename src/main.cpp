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

#include "platform.hpp"

using namespace geode::prelude;

void updateInputQueueAndTime(int stepCount) {
	PlayLayer* playLayer = PlayLayer::get();
	auto& manager = cbf::Manager::get();
	if (!playLayer 
		|| GameManager::sharedState()->getEditorLayer() 
		|| playLayer->m_player1->m_isDead) 
	{
		manager.enableInput = true;
		manager.firstFrame = true;
		manager.skipUpdate = true;
		return;
	}
	else {
		manager.nextInput = {};
		manager.lastFrameTime = manager.lastPhysicsFrameTime;
		manager.stepQueue = {};
		// std::queue<struct step>().swap(stepQueue); // just in case

		{
			std::lock_guard lock(manager.inputQueueLock);
			
			if (manager.lateCutoff) {
				manager.currentFrameTime = cbf::getCurrentTime();
				// QueryPerformanceCounter(&currentFrameTime); // done within the critical section to prevent a race condition which could cause dropped inputs
				manager.inputQueueCopy = manager.inputQueue;
				manager.inputQueue = {};
				// std::queue<struct inputEvent>().swap(inputQueue);
			}
			else {
				while (!manager.inputQueue.empty() && manager.inputQueue.front().time <= manager.currentFrameTime) {
					manager.inputQueueCopy.push(manager.inputQueue.front());
					manager.inputQueue.pop();
				}
			}
		}

		manager.lastPhysicsFrameTime = manager.currentFrameTime;

		if (!manager.firstFrame) manager.skipUpdate = false;
		else {
			manager.skipUpdate = true;
			manager.firstFrame = false;
			if (!manager.lateCutoff) std::queue<cbf::Input>().swap(manager.inputQueueCopy);
			return;
		}

		cbf::TimestampType deltaTime = manager.currentFrameTime - manager.lastFrameTime;
		cbf::TimestampType stepDelta = (deltaTime / stepCount) + 1; // the +1 is to prevent dropped inputs caused by integer division

		constexpr double smallestFloat = std::numeric_limits<float>::min(); // ensures deltaFactor can never be 0, even after being converted to float
		for (int i = 0; i < stepCount; i++) {
			double lastDFactor = 0.0;
			while (true) {
				cbf::Input front;
				if (!manager.inputQueueCopy.empty()) {
					front = manager.inputQueueCopy.front();
					if (front.time - manager.lastFrameTime < stepDelta * (i + 1)) {
						double dFactor = static_cast<double>((front.time - manager.lastFrameTime) % stepDelta) / stepDelta;
						manager.stepQueue.emplace(cbf::Step { front, std::clamp(dFactor - lastDFactor, smallestFloat, 1.0), false });
						lastDFactor = dFactor;
						manager.inputQueueCopy.pop();
						continue;
					}
				}
				front = manager.nextInput;
				manager.stepQueue.emplace(cbf::Step { front, std::max(smallestFloat, 1.0 - lastDFactor), true });
				break;
			}
		}
	}
}

// bool enableP1CollisionAndRotation = true;
// bool enableP2CollisionAndRotation = true;

cbf::Step updateDeltaFactorAndInput() {
	auto& manager = cbf::Manager::get();
	manager.enableInput = false;

	if (manager.stepQueue.empty()) return {};

	auto front = manager.stepQueue.front();
	double deltaFactor = front.deltaFactor;

	if (manager.nextInput.time != 0) {
		PlayLayer* playLayer = PlayLayer::get();

		manager.enableInput = true;
		playLayer->handleButton(!manager.nextInput.state, (int)manager.nextInput.type, manager.nextInput.player == cbf::Player::Player1);
		manager.enableInput = false;
	}

	manager.nextInput = front.input;
	manager.stepQueue.pop();

	return front;
}

void clearQueuesBeforeLoop() {
	PlayLayer* playLayer = PlayLayer::get();
	CCNode* par;
	auto& manager = cbf::Manager::get();

	if (!manager.lateCutoff) manager.currentFrameTime = cbf::getCurrentTime();

	if (manager.softToggle 
		|| !playLayer 
		|| !(par = playLayer->getParent()) 
		|| (getChildOfType<PauseLayer>(par, 0) != nullptr)) 
	{
		manager.firstFrame = true;
		manager.skipUpdate = true;
		manager.enableInput = true;

		std::queue<cbf::Input>().swap(manager.inputQueueCopy);

		{
			std::lock_guard lock(manager.inputQueueLock);
			manager.inputQueue = {};
			// std::queue<struct inputEvent>().swap(inputQueue);
		}
	}
}
#ifndef GEODE_IS_WINDOWS
#include <Geode/modify/CCDirector.hpp>
// CCDirector::setDeltaTime is too small to hook on android
// and setDeltaTime is not found on mac..
class $modify(CCDirector) {
	void drawScene() {
		clearQueuesBeforeLoop();
		CCDirector::drawScene();
	}
};
#else
class $modify(CCDirector) {
	void setDeltaTime(float dTime) {
		clearQueuesBeforeLoop();
		CCDirector::setDeltaTime(dTime);
	}
};
#endif

void newResetCollisionLog(PlayerObject* p) { // inlined in 2.206...
	(*(CCDictionary**)((char*)p + 0x5b0))->removeAllObjects();
	(*(CCDictionary**)((char*)p + 0x5b8))->removeAllObjects();
	(*(CCDictionary**)((char*)p + 0x5c0))->removeAllObjects();
	(*(CCDictionary**)((char*)p + 0x5c8))->removeAllObjects();
	*(unsigned long*)((char*)p + 0x5e0) = *(unsigned long*)((char*)p + 0x5d0);
	*(long long*)((char*)p + 0x5d0) = -1;
}

// float p1CollisionDelta;
// float p2CollisionDelta;
// bool actualDelta;

class $modify(GJBaseGameLayer) {
	static void onModify(auto& self) {
		(void) self.setHookPriority("GJBaseGameLayer::handleButton", INT_MIN);
		(void) self.setHookPriority("GJBaseGameLayer::getModifiedDelta", INT_MIN);
	}

	void handleButton(bool down, int button, bool isPlayer1) {
		auto& manager = cbf::Manager::get();
		if (manager.enableInput) GJBaseGameLayer::handleButton(down, button, isPlayer1);
	}

	float getModifiedDelta(float delta) {
		auto& manager = cbf::Manager::get();
		float modifiedDelta = GJBaseGameLayer::getModifiedDelta(delta);

		PlayLayer* pl = PlayLayer::get();
		if (pl) {
			const float timewarp = pl->m_gameState.m_timeWarp;
			if (manager.actualDelta) modifiedDelta = CCDirector::sharedDirector()->getActualDeltaTime() * timewarp;
			
			const int stepCount = std::round(std::max(1.0, ((modifiedDelta * 60.0) / std::min(1.0f, timewarp)) * 4)); // not sure if this is different from (delta * 240) / timewarp

			if (modifiedDelta > 0.0) updateInputQueueAndTime(stepCount);
			else manager.skipUpdate = true;
		}
		
		return modifiedDelta;
	}
};

class $modify(PlayerObject) {
	void update(float timeFactor) {

		PlayLayer* pl = PlayLayer::get();
		auto& manager = cbf::Manager::get();

		if (manager.skipUpdate 
			|| !pl 
			|| !(this == pl->m_player1 || this == pl->m_player2))
		{
			PlayerObject::update(timeFactor);
			return;
		}

		PlayerObject* p2 = pl->m_player2;
		if (this == p2) return;

		bool p1NotBuffering = this->m_isOnGround
			|| this->m_touchingRings->count()
			|| (this->m_isDart || this->m_isBird || this->m_isShip || this->m_isSwing);

		bool p2NotBuffering = p2->m_isOnGround
			|| p2->m_touchingRings->count()
			|| (p2->m_isDart || p2->m_isBird || p2->m_isShip || p2->m_isSwing);

		bool isDual = pl->m_gameState.m_isDualMode;

		manager.p1Pos = PlayerObject::getPosition();
		manager.p2Pos = p2->getPosition();

		cbf::Step step;
		manager.midStep = true;

		do {
			step = updateDeltaFactorAndInput();

			const float newTimeFactor = timeFactor * step.deltaFactor;
			manager.rotationDelta = newTimeFactor;

			if (p1NotBuffering) {
				if (step.deltaFactor != 1.0)
					log::debug("inserting new time step at {:.3f} - delta {:.5f}", newTimeFactor, step.deltaFactor);
				PlayerObject::update(newTimeFactor);
				if (!step.endStep) {
					manager.p1CollisionDelta = newTimeFactor;
					pl->checkCollisions(this, 0.0f, true);
					PlayerObject::updateRotation(newTimeFactor);
					newResetCollisionLog(this);
				}
			}
			else if (step.endStep) { // disable cbf for buffers, revert to click-on-steps mode 
				PlayerObject::update(timeFactor);
			}

			if (isDual) {
				if (p2NotBuffering) {
					p2->update(newTimeFactor);
					if (!step.endStep) {
						manager.p2CollisionDelta = newTimeFactor;
						pl->checkCollisions(p2, 0.0f, true);
						p2->updateRotation(newTimeFactor);
						newResetCollisionLog(p2);
					}
				}
				else if (step.endStep) {
					p2->update(timeFactor);
				}
			}

		} while (!step.endStep);

		manager.midStep = false;
	}

	void updateRotation(float t) {
		auto& manager = cbf::Manager::get();
		PlayLayer* pl = PlayLayer::get();
		if (!manager.skipUpdate && pl && this == pl->m_player1) {
			PlayerObject::updateRotation(manager.rotationDelta);

			if (manager.p1Pos.x && !manager.midStep) { // to happen only when GJBGL::update() calls updateRotation after an input
				this->m_lastPosition = manager.p1Pos;
				manager.p1Pos.setPoint(0.f, 0.f);
			}
		}
		else if (!manager.skipUpdate && pl && this == pl->m_player2) {
			PlayerObject::updateRotation(manager.rotationDelta);

			if (manager.p2Pos.x && !manager.midStep) {
				pl->m_player2->m_lastPosition = manager.p2Pos;
				manager.p2Pos.setPoint(0.f, 0.f);
			}
		}
		else PlayerObject::updateRotation(t);
	}
};

class $modify(EndLevelLayer) {
	void customSetup() {
		auto& manager = cbf::Manager::get();
		EndLevelLayer::customSetup();

		if (!manager.softToggle || manager.actualDelta) {
			std::string text;

			if (manager.softToggle && manager.actualDelta) text = "PB";
			else if (manager.actualDelta) text = "CBF+PB";
			else text = "CBF";

			cocos2d::CCSize size = cocos2d::CCDirector::sharedDirector()->getWinSize();
			CCLabelBMFont* indicator = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");

			indicator->setPosition({ size.width, size.height });
			indicator->setAnchorPoint({ 1.0f, 1.0f });
			indicator->setOpacity(30);
			indicator->setScale(0.2f);

			this->addChild(indicator);
		}
	}
};

Patch* patch = nullptr;

void toggleMod(bool disable) {
	auto& manager = cbf::Manager::get();
#ifdef GEODE_IS_WINDOWS
	void* addr = reinterpret_cast<void*>(geode::base::get() + 0x5ec8e8);

	if (!patch) patch = Mod::get()->patch(addr, { 0x29, 0x5c, 0x4f, 0x3f }).value_or(nullptr);
	// patch may fail, for whatever reason
	if (patch) {
		if (disable) patch->disable();
		else patch->enable();
	}
#endif

	manager.softToggle = disable;
}

$on_mod(Loaded) {
	auto& manager = cbf::Manager::get();
	toggleMod(Mod::get()->getSettingValue<bool>("soft-toggle"));
	listenForSettingChanges("soft-toggle", toggleMod);

	manager.lateCutoff = Mod::get()->getSettingValue<bool>("late-cutoff");
	listenForSettingChanges("late-cutoff", +[](bool enable) {
		cbf::Manager::get().lateCutoff = enable;
	});

	manager.actualDelta = Mod::get()->getSettingValue<bool>("actual-delta");
	listenForSettingChanges("actual-delta", +[](bool enable) {
		cbf::Manager::get().actualDelta = enable;
	});
}

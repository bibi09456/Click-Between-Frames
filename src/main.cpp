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

	if (manager.nextInput.time != 0) {
		manager.enableP1CollisionAndRotation = false;
		manager.enableP2CollisionAndRotation = false;
	}

	return front;
}

// bool softToggle; // cant just disable all hooks bc thatll cause a memory leak with inputQueue, may improve this in the future

class $modify(CCDirector) {
	void setDeltaTime(float dTime) {
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

		CCDirector::setDeltaTime(dTime);
	}
};

// int lastP1CollisionCheck = 0;
// int lastP2CollisionCheck = 0;
// bool actualDelta;

class $modify(GJBaseGameLayer) {
	static void onModify(auto & self) {
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

	int checkCollisions(PlayerObject *p, float t, bool d) {
		auto& manager = cbf::Manager::get();
		if (p == this->m_player1) {
			if (manager.enableP1CollisionAndRotation || manager.skipUpdate) manager.lastP1CollisionCheck = GJBaseGameLayer::checkCollisions(p, t, d);
			return manager.lastP1CollisionCheck;
		}
		else if (p == this->m_player2) {
			if (manager.enableP2CollisionAndRotation || manager.skipUpdate) manager.lastP2CollisionCheck = GJBaseGameLayer::checkCollisions(p, t, d);
			return manager.lastP2CollisionCheck;
		}
		else return GJBaseGameLayer::checkCollisions(p, t, d);
	}
};

// CCPoint p1Pos = { NULL, NULL };
// CCPoint p2Pos = { NULL, NULL };

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

		if (this == pl->m_player2) return;

		PlayerObject* p2 = pl->m_player2;

		bool isDual = pl->m_gameState.m_isDualMode;
		bool isPlatformer = this->m_isPlatformer;
		bool firstLoop = true;

		bool p1StartedOnGround = this->m_isOnGround;
		bool p2StartedOnGround = p2->m_isOnGround;

		bool p1NotBuffering = p1StartedOnGround
			|| this->m_touchingRings->count()
			|| (this->m_isDart || this->m_isBird || this->m_isShip || this->m_isSwing);

		bool p2NotBuffering = p2StartedOnGround
			|| p2->m_touchingRings->count()
			|| (p2->m_isDart || p2->m_isBird || p2->m_isShip || p2->m_isSwing);

		manager.enableP1CollisionAndRotation = true;
		manager.enableP2CollisionAndRotation = true;
		manager.skipUpdate = true; // enable collision & rotation checks for the duration of the step update-collision-rotation loop

		manager.p1Pos = PlayerObject::getPosition();
		manager.p2Pos = p2->getPosition();

		cbf::Step step;

		do {
			step = updateDeltaFactorAndInput();
			const float newTimeFactor = timeFactor * step.deltaFactor;

			if (p1NotBuffering) {
				PlayerObject::update(newTimeFactor);
				if (!isPlatformer && !manager.enableP1CollisionAndRotation) {
					pl->checkCollisions(this, newTimeFactor, true);
					PlayerObject::updateRotation(newTimeFactor);
				}
				else if (isPlatformer && step.deltaFactor != 1.0) {  // checking collision extra times in platformer breaks moving platforms so this is a scuffed temporary fix
					if (firstLoop) this->m_isOnGround = p1StartedOnGround;
					else this->m_isOnGround = false;

					manager.enableP1CollisionAndRotation = true;
				}
			}
			else if (step.endStep) { // disable cbf for buffers, revert to click-on-steps mode 
				PlayerObject::update(timeFactor);
				manager.enableP1CollisionAndRotation = true;
			}

			if (isDual) {
				if (p2NotBuffering) {
					p2->update(newTimeFactor);
					if (!isPlatformer && !manager.enableP2CollisionAndRotation) {
						pl->checkCollisions(p2, newTimeFactor, true);
						p2->updateRotation(newTimeFactor);
					}
					else if (isPlatformer && step.deltaFactor != 1.0) {
						if (firstLoop) p2->m_isOnGround = p2StartedOnGround;
						else p2->m_isOnGround = false;

						manager.enableP2CollisionAndRotation = true;
					}
				}
				else if (step.endStep) {
					p2->update(timeFactor);
					manager.enableP2CollisionAndRotation = true;
				}
			}

			firstLoop = false;

		} while (!step.endStep);

		manager.skipUpdate = false;
	}

	void updateRotation(float t) {
		auto& manager = cbf::Manager::get();
		PlayLayer* pl = PlayLayer::get();
		if (pl && this == pl->m_player1) {
			if (manager.enableP1CollisionAndRotation || manager.skipUpdate) PlayerObject::updateRotation(t);

			if (manager.p1Pos.x && !manager.skipUpdate) { // to happen only when GJBGL::update() calls updateRotation after an input
				this->m_lastPosition = manager.p1Pos;
				manager.p1Pos.setPoint(NULL, NULL);
			}
		}
		else if (pl && this == pl->m_player2) {
			if (manager.enableP2CollisionAndRotation || manager.skipUpdate) PlayerObject::updateRotation(t);

			if (manager.p2Pos.x && !manager.skipUpdate) {
				pl->m_player2->m_lastPosition = manager.p2Pos;
				manager.p2Pos.setPoint(NULL, NULL);
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
			CCLabelBMFont *indicator = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");

			indicator->setPosition({ size.width, size.height });
			indicator->setAnchorPoint({ 1.0f, 1.0f });
			indicator->setOpacity(90);
			indicator->setScale(0.2f);

			this->addChild(indicator);
		}
	}
};

Patch *patch = nullptr;

void toggleMod(bool disable) {
	auto& manager = cbf::Manager::get();
	void* addr = reinterpret_cast<void*>(geode::base::get() + 0x5ec8e8);

	if (!patch) patch = Mod::get()->patch(addr, { 0x29, 0x5c, 0x4f, 0x3f }).value_or(nullptr);
	// patch may fail, for whatever reason
	if (patch) {
		if (disable) patch->disable();
		else patch->enable();
	}

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

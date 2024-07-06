#pragma once

#include <stdint.h>
#include <Geode/Geode.hpp>
#include <queue>

namespace cbf {

using TimestampType = int64_t;

TimestampType getCurrentTime();

enum class Player : bool {
    Player1 = 0,
    Player2 = 1,
};

enum InputState : bool {
    Press = 0,
    Release = 1
};

struct Input {
    TimestampType time = 0;
    PlayerButton type = PlayerButton::Jump;
    InputState state = InputState::Press;
    Player player = Player::Player1;
};

struct Step {
    // input that caused this new physics step
    Input input;
    double deltaFactor = 1.0;
    bool endStep = true;
};

struct Manager {
    std::queue<Input> inputQueue;

    std::mutex inputQueueLock;
    std::mutex keybindsLock;

    bool enableRightClick = false;

    std::queue<Input> inputQueueCopy;
    std::queue<Step> stepQueue;

    Input nextInput;

    TimestampType lastFrameTime;
    TimestampType lastPhysicsFrameTime;
    TimestampType currentFrameTime;

    bool firstFrame = true;
    bool skipUpdate = true;
    bool enableInput = false;
    bool lateCutoff = false;

    bool enableP1CollisionAndRotation = true;
    bool enableP2CollisionAndRotation = true;

    float p1CollisionDelta;
    float p2CollisionDelta;
    bool actualDelta = false;

    bool softToggle = false; // cant just disable all hooks bc thatll cause a memory leak with inputQueue, may improve this in the future

    cocos2d::CCPoint p1Pos = { 0.f, 0.f };
    cocos2d::CCPoint p2Pos = { 0.f, 0.f };

    float p1RotationDelta;
    float p2RotationDelta;

    inline static Manager& get() {
        static Manager instance;
        return instance;
    }

    inline void addInput(Input ipt) {
		std::lock_guard lock(inputQueueLock);
        inputQueue.emplace(std::move(ipt));
    }
private:
    Manager() = default;
    Manager(const Manager&) = delete;
    Manager& operator=(const Manager&) = delete;
};

}
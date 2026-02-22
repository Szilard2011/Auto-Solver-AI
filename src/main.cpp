#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/Popup.hpp>
#include <vector>
#include <cmath>

using namespace geode::prelude;

namespace BotState {
    bool isEnabled = false;
    bool enablePID = true;
    bool enableRaycast = true;

    float targetY = 150.0f;
    float prevError = 0.0f;
    float integral = 0.0f;
    std::vector<cocos2d::CCRect> hazards;
}

class BotCore {
public:
    static void reset(float groundLevel) {
        BotState::prevError = 0.0f;
        BotState::integral = 0.0f;
        BotState::targetY = groundLevel + 75.0f;
        BotState::hazards.clear();
    }

    static void updateEnvironment(PlayLayer* layer) {
        if (!layer || !layer->m_objects) return;
        BotState::hazards.clear();

        auto player = layer->m_player1;
        if (!player) return;
        float pX = player->getPositionX();

        for (auto* obj : CCArrayExt<GameObject*>(layer->m_objects)) {
            if (!obj) continue;
            float objX = obj->getPositionX();
            
            if (objX > pX && objX < pX + 800.0f) {
                if (obj->m_objectType == GameObjectType::Hazard || obj->m_objectType == GameObjectType::Spike) {
                    BotState::hazards.push_back(obj->boundingBox());
                }
            }
        }
        recalculatePath(player);
    }

    static void recalculatePath(PlayerObject* player) {
        float pX = player->getPositionX();
        float optimalY = BotState::targetY;

        for (const auto& hazard : BotState::hazards) {
            if (std::abs(hazard.getMinX() - pX) < 250.0f) {
                if (optimalY >= hazard.getMinY() && optimalY <= hazard.getMaxY()) {
                    optimalY = hazard.getMaxY() + 40.0f; 
                }
            }
        }
        BotState::targetY += (optimalY - BotState::targetY) * 0.15f;
    }

    static void executeShipPID(PlayerObject* player) {
        float currentY = player->getPositionY();
        float error = BotState::targetY - currentY;
        
        BotState::integral += error;
        float derivative = error - BotState::prevError;
        BotState::prevError = error;

        float output = (1.2f * error) + (0.01f * BotState::integral) + (0.5f * derivative);

        if (output > 6.5f) {
            player->pushButton(static_cast<int>(PlayerButton::Jump));
        } else {
            player->releaseButton(static_cast<int>(PlayerButton::Jump));
        }
    }

    static void executeWaveRaycast(PlayerObject* player) {
        if (player->getPositionY() < BotState::targetY) {
            player->pushButton(static_cast<int>(PlayerButton::Jump));
        } else {
            player->releaseButton(static_cast<int>(PlayerButton::Jump));
        }
    }

    static void executeCubeAStar(PlayerObject* player) {
        float pX = player->getPositionX();
        float pY = player->getPositionY();
        bool jumpRequired = false;

        for (const auto& hazard : BotState::hazards) {
            if (hazard.getMinX() - pX < 70.0f && hazard.getMinX() - pX > 0.0f) {
                if (pY <= hazard.getMaxY()) {
                    jumpRequired = true;
                    break;
                }
            }
        }

        if (jumpRequired && player->m_isOnGround) {
            player->pushButton(static_cast<int>(PlayerButton::Jump));
        } else if (player->m_isOnGround) {
            player->releaseButton(static_cast<int>(PlayerButton::Jump));
        }
    }
};

class BotGUI : public geode::Popup<> {
protected:
    bool setup() override {
        this->setTitle("Auto Solver Configuration");
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto menu = CCMenu::create();
        menu->setPosition(winSize.width / 2, winSize.height / 2);
        this->m_mainLayer->addChild(menu);

        auto createToggle = [this, menu](bool& state, const char* text, float yPos) {
            auto toggle = CCMenuItemToggler::createWithStandardSprites(
                this, menu_selector(BotGUI::onToggle), 0.8f
            );
            toggle->toggle(state);
            toggle->setPosition(-110, yPos);
            toggle->setUserData(&state);
            menu->addChild(toggle);

            auto label = CCLabelBMFont::create(text, "bigFont.fnt");
            label->setScale(0.5f);
            label->setAnchorPoint({0.0f, 0.5f});
            label->setPosition(-80, yPos);
            menu->addChild(label);
        };

        createToggle(BotState::isEnabled, "Enable Master AI", 40.0f);
        createToggle(BotState::enablePID, "Ship PID Controller", 0.0f);
        createToggle(BotState::enableRaycast, "Wave Raycast Module", -40.0f);

        auto simBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Run Simulation"), this, menu_selector(BotGUI::onSimulate)
        );
        simBtn->setPosition(0, -90.0f);
        menu->addChild(simBtn);

        return true;
    }

    void onToggle(CCObject* sender) {
        auto toggle = static_cast<CCMenuItemToggler*>(sender);
        bool* state = static_cast<bool*>(toggle->getUserData());
        if (state) *state = !(*state);
    }

    void onSimulate(CCObject* sender) {
        FLAlertLayer::create("Solver", "Target NavMesh generated.\nReady for playback.", "OK")->show();
    }

public:
    static BotGUI* create() {
        auto ret = new BotGUI();
        if (ret && ret->initAnchored(320.f, 260.f)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

class $modify(PlayLayerHook, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        BotCore::reset(level->m_groundHeight);
        return true;
    }

    void update(float dt) {
        PlayLayer::update(dt);

        if (!BotState::isEnabled || !m_player1 || m_player1->m_isDead) return;

        BotCore::updateEnvironment(this);

        if (m_player1->m_isShip && BotState::enablePID) {
            BotCore::executeShipPID(m_player1);
        } else if (m_player1->m_isDart && BotState::enableRaycast) {
            BotCore::executeWaveRaycast(m_player1);
        } else {
            BotCore::executeCubeAStar(m_player1);
        }
    }
};

class $modify(MenuLayerHook, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        auto botButton = CCMenuItemSpriteExtra::create(
            CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png"),
            this,
            menu_selector(MenuLayerHook::onOpenGUI)
        );
        botButton->setID("bot-gui-button"_spr);
        static_cast<CCSprite*>(botButton->getNormalImage())->setColor({50, 255, 50});

        auto menu = this->getChildByID("bottom-menu");
        if (menu) {
            menu->addChild(botButton);
            menu->updateLayout();
        }

        return true;
    }

    void onOpenGUI(CCObject*) {
        BotGUI::create()->show();
    }
};

class $modify(PauseLayerHook, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto menu = CCMenu::create();
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        menu->setPosition(winSize.width - 35.0f, winSize.height / 2.0f);
        
        auto botButton = CCMenuItemSpriteExtra::create(
            CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png"),
            this,
            menu_selector(PauseLayerHook::onOpenGUI)
        );
        static_cast<CCSprite*>(botButton->getNormalImage())->setColor({50, 255, 50});
        
        menu->addChild(botButton);
        this->addChild(menu);
    }

    void onOpenGUI(CCObject*) {
        BotGUI::create()->show();
    }
};
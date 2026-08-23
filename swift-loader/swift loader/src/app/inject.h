#pragma once

namespace app {

enum class Stage { Idle, WaitingForGame, Loading, Done };

class InjectFlow {
public:
    void start();
    void update(float dt);

    bool active() const { return m_stage != Stage::Idle; }
    bool running() const { return active() && m_stage != Stage::Done; }

private:
    Stage m_stage = Stage::Idle;
    int m_index = 0;
    float m_elapsed = 0.f;
};

}

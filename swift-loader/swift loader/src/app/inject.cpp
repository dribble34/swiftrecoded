#include "app/inject.h"
#include <windows.h>
namespace app {
struct StageInfo{Stage stage;float d;};
static const StageInfo kS[]={{Stage::WaitingForGame,6.5f},{Stage::Loading,9.0f}};
void InjectFlow::start(){m_stage=kS[0].stage;m_index=0;m_elapsed=0.f;}
void InjectFlow::update(float dt){if(!running())return;m_elapsed+=dt;if(m_elapsed<kS[m_index].d)return;m_elapsed=0.f;if(++m_index>=2){m_stage=Stage::Done;return;}m_stage=kS[m_index].stage;}
}

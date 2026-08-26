#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/random/random.hpp>
#include <utilities/logging/logging.hpp>

#include "../systems.hpp"

namespace systems {

	
	
	
	
	
	

	
	
	
	
	
	

	void legit_input::add_mouse_delta( float pitch_degrees, float yaw_degrees )
	{
		this->m_pending_pitch += pitch_degrees;
		this->m_pending_yaw += yaw_degrees;
	}

	
	
	
	
	
	
	
	
	
	
	
	
	

	
	
	
	

	

	
	
	
	
	
	
	

	
	
	
	

	
	
	
	
	
	
	

	
	
	
	
	
	
	
	
	

	

	

	
	

	
	
	
	

	
	

	
	

	
	
	
	
	
	
	

	
	
	
	
	
	
	

	
	
	

	
	

	
	
	
	
	
	
	

	
	
	
	
	
	
	

	
	
	

	
	
	

	void legit_input::on_process_input_event( std::uintptr_t csgo_input, int slot )
	{
		if ( slot != 0 )
		{
			return;
		}

		if ( this->m_pending_pitch == 0.0f && this->m_pending_yaw == 0.0f )
		{
			return;
		}

		auto& va_pitch = *reinterpret_cast< float* >( csgo_input + 1672 );
		auto& va_yaw = *reinterpret_cast< float* >( csgo_input + 1676 );

		va_pitch += this->m_pending_pitch;
		va_yaw += this->m_pending_yaw;
		va_pitch = std::clamp( va_pitch, -89.0f, 89.0f );

		this->m_pending_pitch = 0.0f;
		this->m_pending_yaw = 0.0f;
	}

	
	
	
	
	
	

	
	
	
	

	
	
	
	

	
	

} 
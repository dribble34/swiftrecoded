#pragma once

union cvvalue_t
{
	bool i1;
	short i16;
	int i32;
	int64_t i64;
	float fl;
	double db;
	const char* sz;
};

class c_convar
{
public:
	const char* m_name; 
	const void* m_default_value_ptr; 
	const void* m_min_value; 
	const void* m_max_value; 
	const char* m_description; 
	int16_t m_type; 
	char _pad_01 [0x2]; 
	uint32_t m_change_count; 
	uint64_t m_flags; 
	char _pad_02 [0x20]; 
	cvvalue_t m_value; 
	char _value_tail [0x8]; 

	template<typename T>
	T get() {
		if constexpr (std::is_same_v<T, bool>)
			return m_value.i1;
		else if constexpr (std::is_same_v<T, short>)
			return m_value.i16;
		else if constexpr (std::is_same_v<T, int>)
			return m_value.i32;
		else if constexpr (std::is_same_v<T, int64_t>)
			return m_value.i64;
		else if constexpr (std::is_same_v<T, float>)
			return m_value.fl;
		else
			return T {};
	}
};

namespace interfaces {

	class c_engine_cvar
	{
	public:
		struct cvar_container_t
		{
			c_convar* m_cvar;		
			uint16_t m_generation;	
			uint16_t m_next_index;	
			uint32_t m_links;		
		};

		char _pad0 [0x4A]; 
		uint16_t m_allocation_count; 
		char _pad1 [0x4]; 
		cvar_container_t* m_container; 
		uint16_t m_head; 

		[[nodiscard]] c_convar* find (std::uint32_t name_hash);

		bool unlock_all ();
	};

}

static_assert( offsetof( c_convar, m_flags ) == 0x30 );
static_assert( offsetof( c_convar, m_value ) == 0x58 );
static_assert( sizeof( interfaces::c_engine_cvar::cvar_container_t ) == 0x10 );
static_assert( offsetof( interfaces::c_engine_cvar, m_container ) == 0x50 );
static_assert( offsetof( interfaces::c_engine_cvar, m_head ) == 0x58 );

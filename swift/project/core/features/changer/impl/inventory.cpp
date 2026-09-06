#include <utilities/memory/memory.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>

namespace features::changer {

	// CCSPlayerInventory::GetItemInLoadout hands back a CEconItemView whose item
	// definition index lives at +0x1BA. the game's CEconItemView::GetStaticData
	// re-resolves the definition from that field on every call, so overwriting it
	// in place is enough to make the LOADOUT picker (and the spawned weapon) use
	// another knife. only a slot whose value we first saw as a *base* knife
	// (weapon_knife 42 / weapon_knife_t 59) is ever touched, so a genuinely
	// owned item is never modified. every read/write is SEH-guarded.
	namespace {

		constexpr std::size_t k_def_index_offset = 0x1BA;

		constexpr bool is_base_knife( std::uint16_t def_index )
		{
			return def_index == 42 || def_index == 59;
		}

	}

	std::uintptr_t inventory::on_get_item_in_loadout( std::uint32_t team, [[maybe_unused]] std::uint32_t slot, std::uintptr_t original )
	{
		if ( !original || team >= 4 )
		{
			return original;
		}

		const auto current = memory::safe_read<std::uint16_t>( original + k_def_index_offset );
		if ( !current.has_value( ) )
		{
			return original;
		}

		const auto current_def = g_econ_item_system.find_def( static_cast< std::int16_t >( *current ) );
		if ( !current_def || current_def->category != econ_item_system::item_category::knife )
		{
			return original;
		}

		auto& state = this->m_slot[ team ];
		const auto adopted = state.active && state.address == original;

		// a melee slot we have not adopted and that is not a base knife right now
		// is an actually-owned item - leave it untouched
		if ( !adopted && !is_base_knife( *current ) )
		{
			return original;
		}

		// the knife picked in the skin changer, if any (shares the map the
		// entity-side knife changer reads, so that selection is the only input)
		const econ_item_system::item_def* knife_def{ nullptr };

		for ( const auto& entry : settings::g_changer.skins.data )
		{
			const auto def = g_econ_item_system.find_def( entry.first );
			if ( def && def->category == econ_item_system::item_category::knife )
			{
				knife_def = def;
				break;
			}
		}

		if ( !knife_def )
		{
			if ( adopted )
			{
				static_cast< void >( memory::safe_write<std::uint16_t>( original + k_def_index_offset, state.original_def ) );
			}

			state = {};
			return original;
		}

		if ( !adopted )
		{
			state.address = original;
			state.original_def = *current;
			state.active = true;
		}

		const auto target = static_cast< std::uint16_t >( knife_def->def_index );
		if ( *current != target )
		{
			static_cast< void >( memory::safe_write<std::uint16_t>( original + k_def_index_offset, target ) );
		}

		return original;
	}

}

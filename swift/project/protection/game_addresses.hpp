#ifndef GAME_ADDRESSES_HPP
#define GAME_ADDRESSES_HPP
#include <cstdint>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/fnv1a.hpp>

/*
* @info
* All patterns live in protection/patterns.hpp - update signatures there 1:1.
* PATTERN ( patterns::name )
* MODULE ( "MODULE" )
* INTERFACE_ ( "MODULE:INTERFACE_NAME" )
* CONVAR ( "CONVAR_NAME" )
*
* bad:
* detail::address_1 = PATTERN (patterns::name); then in another function call detail::address_1
* good:
* call (PATTERN (patterns::name))
*/

#ifndef PATTERN
#define PATTERN(entry) \
    ([]() -> std::uintptr_t { \
        static const auto val = memory::resolve_pattern(entry); \
        return val; \
    }())
#endif

#ifndef INTERFACE_
#define INTERFACE_(str) \
    []() -> std::uintptr_t { \
        static const auto val = memory::get_module_interface(str); \
        return val; \
    }()
#endif
#ifndef CONVAR
#define CONVAR(str) \
    []() -> c_convar* { \
        static const auto val = addresses::globals::cvar->find(fnv1a::hash(str, sizeof(str) - 1)); \
        return val; \
    }()
#endif
#ifndef MODULE_BASE
#define MODULE_BASE(str) \
    []() -> std::uintptr_t { \
        static const auto val = memory::get_module_base(str); \
        return val; \
    }()
#endif
#ifndef MODULE_EXPORT
#define MODULE_EXPORT(str) \
    ([]() -> std::uintptr_t { \
        static std::uintptr_t val{}; \
        if ( !val ) { \
            val = memory::get_module_export( str ); \
        } \
        return val; \
    }())
#endif
#include <protection/patterns.hpp>
#endif

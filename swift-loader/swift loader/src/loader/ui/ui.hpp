#pragma once
#include <string>
#include <functional>

namespace ui
{
    // Call from the background loader thread to push a status line.
    void log( const char* fmt, ... );

    // Run the UI (blocking). Spawns `work` on a background thread.
    // Returns when the work thread finishes.
    void run( std::function<void()> work );
}

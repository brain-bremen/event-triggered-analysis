/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugin TriggeredCoherence.
    Copyright (C) 2026 Joscha Schmiedt, Universität Bremen

    ------------------------------------------------------------------

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/
#include "TriggeredCoherenceNode.h"

#include <PluginInfo.h>
#include <PluginVersion.h>

#ifdef WIN32
#include <Windows.h>
#define EXPORT __declspec (dllexport)
#else
#define EXPORT __attribute__ ((visibility ("default")))
#endif

using namespace Plugin;

#define NUM_PLUGINS 1

extern "C" EXPORT void getLibInfo (Plugin::LibraryInfo* info)
{
    // Must match the GUI's PLUGIN_API_VER exactly; the host refuses to load a
    // plugin built against a different API version.
    info->apiVersion = PLUGIN_API_VER;
    info->name = "Triggered Coherence";
    info->libVersion = PLUGIN_VERSION_STRING;
    info->numPlugins = NUM_PLUGINS;
}

extern "C" EXPORT int getPluginInfo (int index, Plugin::PluginInfo* info)
{
    switch (index)
    {
        case 0:
            info->type = Plugin::Type::PROCESSOR;
            info->processor.name = "Triggered Coherence";
            info->processor.type = Processor::Type::SINK;
            info->processor.creator =
                &(Plugin::createProcessor<EventTriggered::TriggeredCoherenceNode>);
            break;

        default:
            return -1;
    }

    return 0;
}

#ifdef WIN32
BOOL WINAPI DllMain (IN HINSTANCE hDllHandle, IN DWORD nReason, IN LPVOID Reserved)
{
    return TRUE;
}
#endif

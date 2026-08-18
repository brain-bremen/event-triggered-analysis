/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI Plugin Receptive Field Mapper
    Copyright (C) 2025-2026 Joscha Schmiedt, Universität Bremen

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
#include "ReceptiveFieldNode.h"

#include <PluginInfo.h>

#ifdef WIN32
#include <Windows.h>
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__ ((visibility ("default")))
#endif

#include "PluginVersion.h"

using namespace Plugin;

extern "C" EXPORT void getLibInfo (Plugin::LibraryInfo* info)
{
    info->apiVersion = PLUGIN_API_VER;
    info->name = "Receptive Field";
    info->libVersion = PLUGIN_VERSION_STRING;
    info->numPlugins = 1;
}

extern "C" EXPORT int getPluginInfo (int index, Plugin::PluginInfo* info)
{
    switch (index)
    {
        case 0:
            info->type = Plugin::Type::PROCESSOR;
            info->processor.name = "Receptive Field";
            info->processor.type = Plugin::Processor::SINK;
            info->processor.creator = &(Plugin::createProcessor<EventTriggered::ReceptiveFieldNode>);
            return 0;

        default:
            return -1;
    }
}

#ifdef WIN32
BOOL WINAPI DllMain (IN HINSTANCE, IN DWORD, IN LPVOID)
{
    return TRUE;
}
#endif

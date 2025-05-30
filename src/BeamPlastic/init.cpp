/******************************************************************************
*                               BeamPlastic plugin                            *
*                  (c) 2024 Universite Clermont Auvergne (UCA)                *
*                                                                             *
* This program is free software; you can redistribute it and/or modify it     *
* under the terms of the GNU Lesser General Public License as published by    *
* the Free Software Foundation; either version 2.1 of the License, or (at     *
* your option) any later version.                                             *
*                                                                             *
* This program is distributed in the hope that it will be useful, but WITHOUT *
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or       *
* FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License *
* for more details.                                                           *
*                                                                             *
* You should have received a copy of the GNU Lesser General Public License    *
* along with this program. If not, see <http://www.gnu.org/licenses/>.        *
*******************************************************************************
* Authors: The SOFA Team and external contributors (see Authors.txt)          *
*                                                                             *
* Contact information: contact@sofa-framework.org                             *
******************************************************************************/
#include <BeamPlastic/init.h>

#include <sofa/core/ObjectFactory.h>
#include <sofa/helper/system/PluginManager.h>

namespace beamplastic
{

namespace forcefield
{

extern void registerBeamPlasticFEMForceField(sofa::core::ObjectFactory* factory);

}

extern "C" {
    BEAMPLASTIC_API void initExternalModule();
    BEAMPLASTIC_API const char* getModuleName();
    BEAMPLASTIC_API const char* getModuleVersion();
    BEAMPLASTIC_API const char* getModuleLicense();
    BEAMPLASTIC_API const char* getModuleDescription();
    BEAMPLASTIC_API void registerObjects(sofa::core::ObjectFactory* factory);
}

void initExternalModule()
{
    init();
}

void init()
{
    static bool first = true;
    if (first)
    {
        first = false;
    }
}

const char* getModuleName()
{
    return MODULE_NAME;
}

const char* getModuleVersion()
{
    return MODULE_VERSION;
}

const char* getModuleLicense()
{
    return "LGPL";
}

const char* getModuleDescription()
{
    return "This plugin provides all necessary tools for stent expansion simulation";
}

void registerObjects(sofa::core::ObjectFactory* factory)
{
    forcefield::registerBeamPlasticFEMForceField(factory);
}

} // namespace beamplastic

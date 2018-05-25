/*
 * Copyright 2018 WolkAbout Technology s.r.o.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef CONFIGURATIONPROVIDERPERDEVICE_H
#define CONFIGURATIONPROVIDERPERDEVICE_H

#include "model/ConfigurationItem.h"

#include <string>
#include <vector>

namespace wolkabout
{
class ConfigurationProviderPerDevice
{
public:
    /**
     * @brief Device configuration provider callback
     *        Reads device configuration and returns it as
     *        vector of wolkabout::ConfigurationItem.<br>
     *
     *        Must be implemented as non blocking<br>
     *        Must be implemented as thread safe
     * @param deviceKey Device key
     * @return Device configuration as std::vector<ConfigurationItem>
     */
    virtual std::vector<ConfigurationItem> getConfiguration(const std::string& deviceKey) = 0;

    virtual ~ConfigurationProviderPerDevice() = default;
};
}    // namespace wolkabout

#endif    // CONFIGURATIONPROVIDERPERDEVICE_H
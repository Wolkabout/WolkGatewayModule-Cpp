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

#include "model/Device.h"
#include "model/DeviceTemplate.h"
#include <string>
#include <vector>

namespace wolkabout
{
enum class ValueGenerator
{
    RANDOM = 0,
    INCEREMENTAL
};

class DeviceConfiguration
{
public:
    DeviceConfiguration(std::string localMqttUri, unsigned interval, std::vector<wolkabout::Device> devices,
                        ValueGenerator generator);

    const std::string& getLocalMqttUri() const;

    unsigned getInterval() const;

    ValueGenerator getValueGenerator() const;

    const std::vector<wolkabout::Device>& getDevices() const;

    static wolkabout::DeviceConfiguration fromJson(const std::string& deviceConfigurationFile);

private:
    std::string m_localMqttUri;

    unsigned m_interval;

    std::vector<wolkabout::Device> m_devices;

    ValueGenerator m_valueGenerator;
};
}    // namespace wolkabout

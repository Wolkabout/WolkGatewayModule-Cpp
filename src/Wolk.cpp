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

#include "Wolk.h"

#include "ActuationHandlerPerDevice.h"
#include "ActuatorStatusProviderPerDevice.h"
#include "InboundGatewayMessageHandler.h"
#include "WolkBuilder.h"
#include "core/connectivity/ConnectivityService.h"
#include "core/model/ActuatorStatus.h"
#include "core/model/SubdeviceUpdateRequest.h"
#include "core/protocol/DataProtocol.h"
#include "core/protocol/RegistrationProtocol.h"
#include "core/protocol/StatusProtocol.h"
#include "core/protocol/json/JsonDFUProtocol.h"
#include "core/utilities/Logger.h"
#include "core/utilities/StringUtils.h"
#include "model/Device.h"
#include "service/DataService.h"
#include "service/DeviceRegistrationService.h"
#include "service/DeviceStatusService.h"
#include "service/FirmwareUpdateService.h"

#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#define INSTANTIATE_ADD_SENSOR_READING_FOR(x)                                                                    \
    template void Wolk::addSensorReading<x>(const std::string& deviceKey, const std::string& reference, x value, \
                                            unsigned long long rtc);                                             \
    template void Wolk::addSensorReading<x>(const std::string& deviceKey, const std::string& reference,          \
                                            std::initializer_list<x> value, unsigned long long int rtc);         \
    template void Wolk::addSensorReading<x>(const std::string& deviceKey, const std::string& reference,          \
                                            const std::vector<x> values, unsigned long long int rtc)

namespace wolkabout
{
WolkBuilder Wolk::newBuilder()
{
    return WolkBuilder();
}

template <>
void Wolk::addSensorReading(const std::string& deviceKey, const std::string& reference, std::string value,
                            unsigned long long rtc)
{
    addToCommandBuffer([=]() -> void {
        if (!deviceExists(deviceKey))
        {
            LOG(ERROR) << "Device does not exist: " << deviceKey;
            return;
        }

        if (!sensorDefinedForDevice(deviceKey, reference))
        {
            LOG(ERROR) << "Sensor does not exist for device: " << deviceKey << ", " << reference;
            return;
        }

        m_dataService->addSensorReading(deviceKey, reference, value, rtc != 0 ? rtc : Wolk::currentRtc());
    });
}

template <typename T>
void Wolk::addSensorReading(const std::string& deviceKey, const std::string& reference, T value, unsigned long long rtc)
{
    addSensorReading(deviceKey, reference, StringUtils::toString(value), rtc);
}

template <>
void Wolk::addSensorReading(const std::string& deviceKey, const std::string& reference,
                            const std::vector<std::string> values, unsigned long long int rtc)
{
    if (values.empty())
    {
        return;
    }

    addToCommandBuffer([=]() -> void {
        if (!deviceExists(deviceKey))
        {
            LOG(ERROR) << "Device does not exist: " << deviceKey;
            return;
        }

        if (!sensorDefinedForDevice(deviceKey, reference))
        {
            LOG(ERROR) << "Sensor does not exist for device: " << deviceKey << ", " << reference;
            return;
        }

        m_dataService->addSensorReading(deviceKey, reference, values, rtc != 0 ? rtc : Wolk::currentRtc());
    });
}

template <typename T>
void Wolk::addSensorReading(const std::string& deviceKey, const std::string& reference, std::initializer_list<T> values,
                            unsigned long long int rtc)
{
    addSensorReading(deviceKey, reference, std::vector<T>(values), rtc);
}

template <typename T>
void Wolk::addSensorReading(const std::string& deviceKey, const std::string& reference, const std::vector<T> values,
                            unsigned long long int rtc)
{
    std::vector<std::string> stringifiedValues(values.size());
    std::transform(values.cbegin(), values.cend(), stringifiedValues.begin(),
                   [&](const T& value) -> std::string { return StringUtils::toString(value); });

    addSensorReading(deviceKey, reference, stringifiedValues, rtc);
}

INSTANTIATE_ADD_SENSOR_READING_FOR(std::string);
INSTANTIATE_ADD_SENSOR_READING_FOR(const char*);
INSTANTIATE_ADD_SENSOR_READING_FOR(char*);
INSTANTIATE_ADD_SENSOR_READING_FOR(bool);
INSTANTIATE_ADD_SENSOR_READING_FOR(float);
INSTANTIATE_ADD_SENSOR_READING_FOR(double);
INSTANTIATE_ADD_SENSOR_READING_FOR(signed int);
INSTANTIATE_ADD_SENSOR_READING_FOR(signed long int);
INSTANTIATE_ADD_SENSOR_READING_FOR(signed long long int);
INSTANTIATE_ADD_SENSOR_READING_FOR(unsigned int);
INSTANTIATE_ADD_SENSOR_READING_FOR(unsigned long int);
INSTANTIATE_ADD_SENSOR_READING_FOR(unsigned long long int);

void Wolk::addAlarm(const std::string& deviceKey, const std::string& reference, bool active, unsigned long long rtc)
{
    if (rtc == 0)
    {
        rtc = Wolk::currentRtc();
    }

    addToCommandBuffer([=]() -> void {
        if (!deviceExists(deviceKey))
        {
            LOG(ERROR) << "Device does not exist: " << deviceKey;
            return;
        }

        if (!alarmDefinedForDevice(deviceKey, reference))
        {
            LOG(ERROR) << "Alarm does not exist for device: " << deviceKey << ", " << reference;
            return;
        }

        m_dataService->addAlarm(deviceKey, reference, active, rtc);
    });
}

void Wolk::publishActuatorStatus(const std::string& deviceKey, const std::string& reference)
{
    handleActuatorGetCommand(deviceKey, reference);
}

void Wolk::publishActuatorStatus(const std::string& deviceKey, const std::string& reference, const std::string& value)
{
    addToCommandBuffer([=] {
        m_dataService->addActuatorStatus(deviceKey, reference, value, ActuatorStatus::State::READY);
        m_dataService->publishActuatorStatuses(deviceKey);
    });
}

void Wolk::publishConfiguration(const std::string& deviceKey)
{
    handleConfigurationGetCommand(deviceKey);
}

void Wolk::publishConfiguration(const std::string& deviceKey, std::vector<ConfigurationItem> configurations)
{
    addToCommandBuffer([=] {
        m_dataService->addConfiguration(deviceKey, configurations);
        m_dataService->publishConfiguration();
    });
}

void Wolk::addDeviceStatus(const std::string& deviceKey, DeviceStatus::Status status)
{
    addToCommandBuffer([=] {
        if (!deviceExists(deviceKey))
        {
            LOG(ERROR) << "Device does not exist: " << deviceKey;
            return;
        }

        m_deviceStatusService->publishDeviceStatusUpdate(deviceKey, status);
    });
}

void Wolk::connect()
{
    addToCommandBuffer([=]() -> void {
        if (m_connectivityService->connect())
        {
            m_connected = true;
            registerDevices();
            publishFirmwareVersions();
            publishDeviceStatuses();

            for (const auto& kvp : m_devices)
            {
                for (const std::string& actuatorReference : kvp.second.getActuatorReferences())
                {
                    publishActuatorStatus(kvp.first, actuatorReference);
                }

                publishConfiguration(kvp.first);
            }

            publish();
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
            connect();
        }
    });
}

void Wolk::disconnect()
{
    addToCommandBuffer([=]() -> void {
        m_connected = false;
        m_connectivityService->disconnect();
    });
}

void Wolk::publish()
{
    addToCommandBuffer([=]() -> void {
        m_dataService->publishActuatorStatuses();
        m_dataService->publishConfiguration();
        m_dataService->publishAlarms();
        m_dataService->publishSensorReadings();
    });
}

void Wolk::publish(const std::string& deviceKey)
{
    addToCommandBuffer([=]() -> void {
        if (!deviceExists(deviceKey))
        {
            LOG(ERROR) << "Device does not exist: " << deviceKey;
            return;
        }

        m_dataService->publishActuatorStatuses(deviceKey);
        m_dataService->publishConfiguration(deviceKey);
        m_dataService->publishAlarms(deviceKey);
        m_dataService->publishSensorReadings(deviceKey);
    });
}

void Wolk::addDevice(const Device& device)
{
    addToCommandBuffer([=] {
        const std::string deviceKey = device.getKey();
        if (deviceExists(deviceKey))
        {
            LOG(ERROR) << "Device with key '" << deviceKey << "' was already added";
            return;
        }

        m_devices[deviceKey] = device;

        m_deviceStatusService->devicesUpdated(getDeviceKeys());

        if (m_connected)
        {
            registerDevice(device);
            m_connectivityService->reconnect();
        }
    });
}

void Wolk::addAssetsToDevice(std::string deviceKey, bool updateDefaultSemantics,
                             std::vector<ConfigurationTemplate> configurations, std::vector<SensorTemplate> sensors,
                             std::vector<AlarmTemplate> alarms, std::vector<ActuatorTemplate> actuators)
{
    addToCommandBuffer([=] {
        if (!deviceExists(deviceKey))
        {
            LOG(ERROR) << "Can't update device with key '" << deviceKey << "': device is not registered";
            return;
        }

        auto& device = m_devices[deviceKey];

        if (!validateAssetsToUpdate(device, configurations, sensors, alarms, actuators))
        {
            return;
        }

        if (m_connected)
        {
            updateDevice(deviceKey, updateDefaultSemantics, configurations, sensors, alarms, actuators);
            storeAssetsToDevice(device, configurations, sensors, alarms, actuators);
        }
    });
}

void Wolk::removeDevice(const std::string& deviceKey)
{
    addToCommandBuffer([=] {
        auto it = m_devices.find(deviceKey);
        if (it != m_devices.end())
        {
            m_devices.erase(it);
        }
    });
}

Wolk::Wolk() : m_connected{false}, m_commandBuffer{new CommandBuffer()} {}

Wolk::~Wolk()
{
    m_commandBuffer->stop();
}

void Wolk::addToCommandBuffer(std::function<void()> command)
{
    m_commandBuffer->pushCommand(std::make_shared<std::function<void()>>(command));
}

unsigned long long Wolk::currentRtc()
{
    auto duration = std::chrono::high_resolution_clock::now().time_since_epoch();
    return static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());
}

void Wolk::handleActuatorSetCommand(const std::string& key, const std::string& reference, const std::string& value)
{
    addToCommandBuffer([=] {
        if (!deviceExists(key))
        {
            LOG(ERROR) << "Device does not exist: " << key;
            return;
        }

        if (!actuatorDefinedForDevice(key, reference))
        {
            LOG(ERROR) << "Actuator does not exist for device: " << key << ", " << reference;
            return;
        }

        if (m_actuationHandler)
        {
            m_actuationHandler->handleActuation(key, reference, value);
        }
        else if (m_actuationHandlerLambda)
        {
            m_actuationHandlerLambda(key, reference, value);
        }

        const ActuatorStatus actuatorStatus = [&] {
            if (m_actuatorStatusProvider)
            {
                return m_actuatorStatusProvider->getActuatorStatus(key, reference);
            }
            else if (m_actuatorStatusProviderLambda)
            {
                return m_actuatorStatusProviderLambda(key, reference);
            }

            return ActuatorStatus("", ActuatorStatus::State::ERROR);
        }();

        m_dataService->addActuatorStatus(key, reference, actuatorStatus.getValue(), actuatorStatus.getState());
        m_dataService->publishActuatorStatuses();
    });
}

void Wolk::handleActuatorGetCommand(const std::string& key, const std::string& reference)
{
    addToCommandBuffer([=] {
        if (key.empty() && reference.empty())
        {
            for (const auto& kvp : m_devices)
            {
                for (const std::string& actuatorReference : kvp.second.getActuatorReferences())
                {
                    const ActuatorStatus actuatorStatus = [&] {
                        if (m_actuatorStatusProvider)
                        {
                            return m_actuatorStatusProvider->getActuatorStatus(kvp.second.getKey(), actuatorReference);
                        }
                        else if (m_actuatorStatusProviderLambda)
                        {
                            return m_actuatorStatusProviderLambda(kvp.second.getKey(), actuatorReference);
                        }

                        return ActuatorStatus("", ActuatorStatus::State::ERROR);
                    }();

                    m_dataService->addActuatorStatus(kvp.second.getKey(), actuatorReference, actuatorStatus.getValue(),
                                                     actuatorStatus.getState());
                }
                m_dataService->publishActuatorStatuses();
            }
        }
        else
        {
            if (!deviceExists(key))
            {
                return;
            }

            if (!actuatorDefinedForDevice(key, reference))
            {
                LOG(ERROR) << "Actuator does not exist for device: " << key << ", " << reference;
                return;
            }

            const ActuatorStatus actuatorStatus = [&] {
                if (m_actuatorStatusProvider)
                {
                    return m_actuatorStatusProvider->getActuatorStatus(key, reference);
                }
                else if (m_actuatorStatusProviderLambda)
                {
                    return m_actuatorStatusProviderLambda(key, reference);
                }

                return ActuatorStatus("", ActuatorStatus::State::ERROR);
            }();

            m_dataService->addActuatorStatus(key, reference, actuatorStatus.getValue(), actuatorStatus.getState());
            m_dataService->publishActuatorStatuses();
        }
    });
}

void Wolk::handleDeviceStatusRequest(const std::string& key)
{
    addToCommandBuffer([=] {
        if (key.empty())
        {
            publishDeviceStatuses();
        }
        else
        {
            if (!deviceExists(key))
            {
                return;
            }

            const DeviceStatus::Status status = [&] {
                if (m_deviceStatusProvider)
                {
                    return m_deviceStatusProvider->getDeviceStatus(key);
                }
                else if (m_deviceStatusProviderLambda)
                {
                    return m_deviceStatusProviderLambda(key);
                }

                return DeviceStatus::Status::OFFLINE;
            }();

            m_deviceStatusService->publishDeviceStatusResponse(key, status);
        }
    });
}

void Wolk::handleConfigurationSetCommand(const std::string& key, const std::vector<ConfigurationItem>& configuration)
{
    addToCommandBuffer([=] {
        if (!deviceExists(key))
        {
            LOG(ERROR) << "Device does not exist: " << key;
            return;
        }

        for (const auto& configurationItem : configuration)
        {
            if (!configurationItemDefinedForDevice(key, configurationItem.getReference()))
            {
                LOG(ERROR) << "Configuration item does not exist for device: " << key << ", "
                           << configurationItem.getReference();
                return;
            }
        }

        if (m_configurationHandler)
        {
            m_configurationHandler->handleConfiguration(key, configuration);
        }
        else if (m_configurationHandlerLambda)
        {
            m_configurationHandlerLambda(key, configuration);
        }

        const std::vector<ConfigurationItem> configFromDevice = [&] {
            if (m_configurationProvider)
            {
                return m_configurationProvider->getConfiguration(key);
            }
            else if (m_configurationProviderLambda)
            {
                return m_configurationProviderLambda(key);
            }

            return std::vector<ConfigurationItem>{};
        }();

        m_dataService->addConfiguration(key, configFromDevice);
        m_dataService->publishConfiguration();
    });
}

void Wolk::handleConfigurationGetCommand(const std::string& key)
{
    addToCommandBuffer([=] {
        if (!deviceExists(key))
        {
            LOG(ERROR) << "Device does not exist: " << key;
            return;
        }

        const std::vector<ConfigurationItem> configFromDevice = [&] {
            if (m_configurationProvider)
            {
                return m_configurationProvider->getConfiguration(key);
            }
            else if (m_configurationProviderLambda)
            {
                return m_configurationProviderLambda(key);
            }

            return std::vector<ConfigurationItem>{};
        }();

        m_dataService->addConfiguration(key, configFromDevice);
        m_dataService->publishConfiguration();
    });
}

void Wolk::registerDevice(const Device& device)
{
    addToCommandBuffer([=] { m_deviceRegistrationService->publishRegistrationRequest(device); });
}

void Wolk::updateDevice(std::string deviceKey, bool updateDefaultSemantics,
                        std::vector<ConfigurationTemplate> configurations, std::vector<SensorTemplate> sensors,
                        std::vector<AlarmTemplate> alarms, std::vector<ActuatorTemplate> actuators)
{
    addToCommandBuffer([=] {
        SubdeviceUpdateRequest request{deviceKey, updateDefaultSemantics, configurations, sensors, alarms, actuators};

        m_deviceRegistrationService->publishUpdateRequest(request);
    });
}

void Wolk::registerDevices()
{
    addToCommandBuffer([=] {
        for (const auto& kvp : m_devices)
        {
            m_deviceRegistrationService->publishRegistrationRequest(kvp.second);
        }
    });
}

void Wolk::publishFirmwareVersion(const std::string& deviceKey)
{
    addToCommandBuffer([=] {
        if (!m_firmwareUpdateService)
        {
            return;
        }

        m_firmwareUpdateService->publishFirmwareVersion(deviceKey);
    });
}

void Wolk::publishFirmwareVersions()
{
    addToCommandBuffer([=] {
        if (!m_firmwareUpdateService)
        {
            return;
        }

        for (const auto& kvp : m_devices)
        {
            m_firmwareUpdateService->publishFirmwareVersion(kvp.second.getKey());
        }
    });
}

void Wolk::publishDeviceStatuses()
{
    addToCommandBuffer([=] {
        for (const auto& kvp : m_devices)
        {
            addToCommandBuffer([=] {
                const DeviceStatus::Status status = [&] {
                    if (m_deviceStatusProvider)
                    {
                        return m_deviceStatusProvider->getDeviceStatus(kvp.second.getKey());
                    }
                    else if (m_deviceStatusProviderLambda)
                    {
                        return m_deviceStatusProviderLambda(kvp.second.getKey());
                    }

                    return DeviceStatus::Status::OFFLINE;
                }();

                m_deviceStatusService->publishDeviceStatusUpdate(kvp.second.getKey(), status);
            });
        }
    });
}

void Wolk::publishDeviceStatus(const std::string& deviceKey, DeviceStatus::Status status)
{
    addToCommandBuffer([=] { m_deviceStatusService->publishDeviceStatusUpdate(deviceKey, status); });
}

std::vector<std::string> Wolk::getDeviceKeys()
{
    std::vector<std::string> keys;
    for (const auto& kvp : m_devices)
    {
        keys.push_back(kvp.first);
    }

    return keys;
}

bool Wolk::deviceExists(const std::string& deviceKey)
{
    auto it = m_devices.find(deviceKey);
    return it != m_devices.end();
}

bool Wolk::sensorDefinedForDevice(const std::string& deviceKey, const std::string& reference)
{
    auto it = m_devices.find(deviceKey);
    if (it == m_devices.end())
    {
        return false;
    }

    const auto sensors = it->second.getTemplate().getSensors();
    auto sensorIt = std::find_if(sensors.cbegin(), sensors.cend(),
                                 [&](const SensorTemplate& Template) { return Template.getReference() == reference; });

    return sensorIt != sensors.end();
}

std::vector<std::string> Wolk::getActuatorReferences(const std::string& deviceKey)
{
    auto it = m_devices.find(deviceKey);
    if (it == m_devices.end())
    {
        return {};
    }

    return it->second.getActuatorReferences();
}

bool Wolk::alarmDefinedForDevice(const std::string& deviceKey, const std::string& reference)
{
    auto it = m_devices.find(deviceKey);
    if (it == m_devices.end())
    {
        return false;
    }

    const auto alarms = it->second.getTemplate().getAlarms();
    auto alarmIt = std::find_if(alarms.cbegin(), alarms.cend(),
                                [&](const AlarmTemplate& Template) { return Template.getReference() == reference; });

    return alarmIt != alarms.end();
}

bool Wolk::actuatorDefinedForDevice(const std::string& deviceKey, const std::string& reference)
{
    auto it = m_devices.find(deviceKey);
    if (it == m_devices.end())
    {
        return false;
    }

    const auto actuators = it->second.getActuatorReferences();
    auto actuatorIt = std::find(actuators.cbegin(), actuators.cend(), reference);

    return actuatorIt != actuators.end();
}

bool Wolk::configurationItemDefinedForDevice(const std::string& deviceKey, const std::string& reference)
{
    auto it = m_devices.find(deviceKey);
    if (it == m_devices.end())
    {
        return false;
    }

    const auto configurations = it->second.getTemplate().getConfigurations();
    auto configurationIt =
      std::find_if(configurations.cbegin(), configurations.cend(),
                   [&](const ConfigurationTemplate& Template) { return Template.getReference() == reference; });

    return configurationIt != configurations.end();
}

bool Wolk::validateAssetsToUpdate(const Device& device, const std::vector<ConfigurationTemplate>& configurations,
                                  const std::vector<SensorTemplate>& sensors, const std::vector<AlarmTemplate>& alarms,
                                  const std::vector<ActuatorTemplate>& actuators) const
{
    for (const auto& conf : configurations)
    {
        const auto existingConf = device.getTemplate().getConfigurationTemplate(conf.getReference());
        if (existingConf && *existingConf != conf)
        {
            LOG(ERROR) << "Can't update device with key '" << device.getKey()
                       << "': conflicting configuration template";
            return false;
        }
    }

    for (const auto& sensor : sensors)
    {
        const auto existingSensor = device.getTemplate().getSensorTemplate(sensor.getReference());
        if (existingSensor && *existingSensor != sensor)
        {
            LOG(ERROR) << "Can't update device with key '" << device.getKey() << "': conflicting sensor template";
            return false;
        }
    }

    for (const auto& alarm : alarms)
    {
        const auto existingAlarm = device.getTemplate().getAlarmTemplate(alarm.getReference());
        if (existingAlarm && *existingAlarm != alarm)
        {
            LOG(ERROR) << "Can't update device with key '" << device.getKey() << "': conflicting alarm template";
            return false;
        }
    }

    for (const auto& actuator : actuators)
    {
        const auto existingActuator = device.getTemplate().getActuatorTemplate(actuator.getReference());
        if (existingActuator && *existingActuator != actuator)
        {
            LOG(ERROR) << "Can't update device with key '" << device.getKey() << "': conflicting actuator template";
            return false;
        }
    }

    return true;
}

void Wolk::storeAssetsToDevice(Device& device, const std::vector<ConfigurationTemplate>& configurations,
                               const std::vector<SensorTemplate>& sensors, const std::vector<AlarmTemplate>& alarms,
                               const std::vector<ActuatorTemplate>& actuators)
{
    for (const auto& conf : configurations)
    {
        if (!device.getTemplate().hasConfigurationTemplateWithReference(conf.getReference()))
        {
            device.getTemplate().addConfiguration(conf);
        }
    }

    for (const auto& sensor : sensors)
    {
        if (!device.getTemplate().hasSensorTemplateWithReference(sensor.getReference()))
        {
            device.getTemplate().addSensor(sensor);
        }
    }

    for (const auto& alarm : alarms)
    {
        if (!device.getTemplate().hasAlarmTemplateWithReference(alarm.getReference()))
        {
            device.getTemplate().addAlarm(alarm);
        }
    }

    for (const auto& actuator : actuators)
    {
        if (!device.getTemplate().hasActuatorTemplateWithReference(actuator.getReference()))
        {
            device.getTemplate().addActuator(actuator);
        }
    }
}

void Wolk::handleRegistrationResponse(const std::string& deviceKey, PlatformResult::Code result)
{
    LOG(INFO) << "Registration response for device '" << deviceKey << "' received: " << static_cast<int>(result);

    addToCommandBuffer([=] {
        if (!deviceExists(deviceKey))
        {
            LOG(ERROR) << "Device does not exist: " << deviceKey;
            return;
        }

        if (result == PlatformResult::Code::OK)
        {
            for (const auto& ref : getActuatorReferences(deviceKey))
            {
                publishActuatorStatus(deviceKey, ref);
            }

            publishConfiguration(deviceKey);

            publishFirmwareVersion(deviceKey);
        }
    });
}

void Wolk::handleUpdateResponse(const std::string& deviceKey, PlatformResult::Code result)
{
    LOG(INFO) << "Update response for device '" << deviceKey << "' received: " << static_cast<int>(result);

    addToCommandBuffer([=] {
        if (!deviceExists(deviceKey))
        {
            LOG(ERROR) << "Device does not exist: " << deviceKey;
            return;
        }

        if (result == PlatformResult::Code::OK)
        {
            for (const auto& ref : getActuatorReferences(deviceKey))
            {
                publishActuatorStatus(deviceKey, ref);
            }

            publishConfiguration(deviceKey);

            publishFirmwareVersion(deviceKey);
        }
    });
}

Wolk::ConnectivityFacade::ConnectivityFacade(InboundMessageHandler& handler,
                                             std::function<void()> connectionLostHandler)
: m_messageHandler{handler}, m_connectionLostHandler{connectionLostHandler}
{
}

void Wolk::ConnectivityFacade::messageReceived(const std::string& channel, const std::string& message)
{
    m_messageHandler.messageReceived(channel, message);
}

void Wolk::ConnectivityFacade::connectionLost()
{
    m_connectionLostHandler();
}

std::vector<std::string> Wolk::ConnectivityFacade::getChannels() const
{
    return m_messageHandler.getChannels();
}
}    // namespace wolkabout

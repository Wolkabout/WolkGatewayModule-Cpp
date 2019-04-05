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

#include "Configuration.h"
#include "Wolk.h"
#include "model/DeviceTemplate.h"
#include "utilities/ConsoleLogger.h"
#include "utilities/Timer.h"

#include "model/SensorTemplate.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <thread>

class ActuatorHandler
{
public:
    virtual ~ActuatorHandler() = default;
    virtual std::string getValue() = 0;
    virtual void setValue(std::string value) = 0;
};

template <class T> class ActuatorTemplateHandler : public ActuatorHandler
{
public:
    void setValue(std::string value) override
    {
        try
        {
            m_value = std::stod(value);
        }
        catch (...)
        {
        }
    }

    std::string getValue() override { return std::to_string(m_value); }

private:
    T m_value;
};

template <> class ActuatorTemplateHandler<bool> : public ActuatorHandler
{
public:
    void setValue(std::string value) override { m_value = value == "true"; }

    std::string getValue() override { return m_value ? "true" : "false"; }

private:
    bool m_value;
};

template <> class ActuatorTemplateHandler<std::string> : public ActuatorHandler
{
public:
    void setValue(std::string value) override { m_value = value; }

    std::string getValue() override { return m_value; }

private:
    std::string m_value;
};

int main(int argc, char** argv)
{
    auto logger = std::unique_ptr<wolkabout::ConsoleLogger>(new wolkabout::ConsoleLogger());
    logger->setLogLevel(wolkabout::LogLevel::DEBUG);
    wolkabout::Logger::setInstance(std::move(logger));

    if (argc < 2)
    {
        LOG(ERROR) << "WolkGatewayModule Application: Usage -  " << argv[0] << " [configurationFilePath]";
        return -1;
    }

    wolkabout::DeviceConfiguration appConfiguration = [&] {
        try
        {
            return wolkabout::DeviceConfiguration::fromJson(argv[1]);
        }
        catch (std::logic_error& e)
        {
            LOG(ERROR) << "WolkGatewayModule Application: Unable to parse configuration file. Reason: " << e.what();
            std::exit(-1);
        }
    }();

    std::map<std::string, std::shared_ptr<ActuatorHandler>> handlers;

    for (const auto& device : appConfiguration.getDevices())
    {
        for (const auto& actuator : device.getTemplate().getActuators())
        {
            std::shared_ptr<ActuatorHandler> handler;
            if (actuator.getReadingTypeName() == "SWITCH(ACTUATOR)")
            {
                handler.reset(new ActuatorTemplateHandler<bool>());
            }
            else if (actuator.getReadingTypeName() == "COUNT(ACTUATOR)")
            {
                handler.reset(new ActuatorTemplateHandler<double>());
            }
            else
            {
                handler.reset(new ActuatorTemplateHandler<std::string>());
            }

            handlers[device.getKey() + "_" + actuator.getReference()] = handler;
        }
    }

    static std::map<std::string, std::vector<wolkabout::ConfigurationItem>> localConfiguration;

    static std::map<std::string, std::tuple<int, bool>> m_firmwareStatuses;

    for (const auto& device : appConfiguration.getDevices())
    {
        for (const auto& conf : device.getTemplate().getConfigurations())
        {
            localConfiguration[device.getKey()].push_back(wolkabout::ConfigurationItem{
              std::vector<std::string>(conf.getSize(), conf.getDefaultValue()), conf.getReference()});
        }

        m_firmwareStatuses[device.getKey()] = std::make_tuple(1, true);
    }

    class FirmwareInstallerImpl : public wolkabout::FirmwareInstaller
    {
    public:
        void install(const std::string& deviceKey, const std::string& firmwareFile,
                     std::function<void(const std::string& deviceKey)> onSuccess,
                     std::function<void(const std::string& deviceKey)> onFail) override
        {
            LOG(INFO) << "Installing firmware: " << firmwareFile << ", for device " << deviceKey;

            auto it = m_firmwareStatuses.find(deviceKey);
            if (it != m_firmwareStatuses.end() && std::get<1>(it->second))
            {
                auto statusIt = m_perDeviceInstallationState.find(deviceKey);
                if (statusIt == m_perDeviceInstallationState.end())
                {
                    m_perDeviceInstallationState[deviceKey] =
                      std::unique_ptr<FirmwareInstallionStruct>(new FirmwareInstallionStruct());
                }

                if (m_perDeviceInstallationState[deviceKey]->timer1.running() ||
                    m_perDeviceInstallationState[deviceKey]->timer2.running())
                {
                    return;
                }

                // abort is possible during first 5 seconds
                m_perDeviceInstallationState[deviceKey]->abortPossible = true;
                m_perDeviceInstallationState[deviceKey]->timer1.start(std::chrono::seconds(5), [=] {
                    m_perDeviceInstallationState[deviceKey]->abortPossible = false;
                    m_perDeviceInstallationState[deviceKey]->timer2.start(std::chrono::seconds(5), [=] {
                        auto firmwareVersionIt = m_firmwareStatuses.find(deviceKey);
                        ++(std::get<0>(firmwareVersionIt->second));
                        onSuccess(deviceKey);
                    });
                });
            }
            else
            {
                onFail(deviceKey);
            }
        }

        bool abort(const std::string& deviceKey) override
        {
            auto statusIt = m_perDeviceInstallationState.find(deviceKey);
            if (statusIt == m_perDeviceInstallationState.end())
            {
                return false;
            }

            if (!m_perDeviceInstallationState[deviceKey]->abortPossible)
            {
                return false;
            }

            m_perDeviceInstallationState[deviceKey]->timer1.stop();
            m_perDeviceInstallationState[deviceKey]->timer2.stop();
            return true;
        }

    private:
        struct FirmwareInstallionStruct
        {
            std::atomic_bool abortPossible;
            wolkabout::Timer timer1;
            wolkabout::Timer timer2;
        };

        std::map<std::string, std::unique_ptr<FirmwareInstallionStruct>> m_perDeviceInstallationState;
    };

    class FirmwareVersionProviderImpl : public wolkabout::FirmwareVersionProvider
    {
    public:
        std::string getFirmwareVersion(const std::string& deviceKey)
        {
            auto it = m_firmwareStatuses.find(deviceKey);
            if (it != m_firmwareStatuses.end())
            {
                return std::to_string(std::get<0>(it->second)) + ".0.0";
            }

            return "";
        }
    };

    auto installer = std::make_shared<FirmwareInstallerImpl>();
    auto provider = std::make_shared<FirmwareVersionProviderImpl>();

    std::unique_ptr<wolkabout::Wolk> wolk =
      wolkabout::Wolk::newBuilder()
        .actuationHandler([&](const std::string& key, const std::string& reference, const std::string& value) -> void {
            std::cout << "Actuation request received - Key: " << key << " Reference: " << reference
                      << " value: " << value << std::endl;

            std::string handlerId = key + "_" + reference;

            auto it = handlers.find(handlerId);
            if (it != handlers.end())
            {
                it->second->setValue(value);
            }
        })
        .actuatorStatusProvider([&](const std::string& key, const std::string& reference) -> wolkabout::ActuatorStatus {
            std::string handlerId = key + "_" + reference;

            auto it = handlers.find(handlerId);
            if (it != handlers.end())
            {
                return wolkabout::ActuatorStatus(it->second->getValue(), wolkabout::ActuatorStatus::State::READY);
            }

            return wolkabout::ActuatorStatus("", wolkabout::ActuatorStatus::State::READY);
        })
        .deviceStatusProvider([&](const std::string& deviceKey) -> wolkabout::DeviceStatus::Status {
            auto it =
              std::find_if(appConfiguration.getDevices().begin(), appConfiguration.getDevices().end(),
                           [&](const wolkabout::Device& device) -> bool { return (device.getKey() == deviceKey); });

            if (it != appConfiguration.getDevices().end())
            {
                return wolkabout::DeviceStatus::Status::CONNECTED;
            }

            return wolkabout::DeviceStatus::Status::OFFLINE;
        })
        .configurationHandler(
          [&](const std::string& deviceKey, const std::vector<wolkabout::ConfigurationItem>& configuration) {
              auto it = localConfiguration.find(deviceKey);
              if (it != localConfiguration.end())
              {
                  localConfiguration[deviceKey] = configuration;
              }
          })
        .configurationProvider([&](const std::string& deviceKey) -> std::vector<wolkabout::ConfigurationItem> {
            auto it = localConfiguration.find(deviceKey);
            if (it != localConfiguration.end())
            {
                return localConfiguration[deviceKey];
            }

            return {};
        })
        .withFirmwareUpdate(installer, provider)
        .host(appConfiguration.getLocalMqttUri())
        .build();

    for (const auto& device : appConfiguration.getDevices())
    {
        wolk->addDevice(device);
    }

    wolk->connect();

    std::random_device rd;
    std::mt19937 mt(rd());

    const unsigned interval = appConfiguration.getInterval();

    while (true)
    {
        for (const auto& device : appConfiguration.getDevices())
        {
            for (const auto& sensor : device.getTemplate().getSensors())
            {
                std::vector<int> values;

                if (appConfiguration.getValueGenerator() == wolkabout::ValueGenerator::INCEREMENTAL)
                {
                    static int value = 0;
                    int size = 1;
                    if (!sensor.getDescription().empty())
                    {
                        // get sensor size from description as the size param is removed
                        try
                        {
                            size = std::stoi(sensor.getDescription());
                        }
                        catch (...)
                        {
                        }
                    }

                    for (int i = 0; i < size; ++i)
                    {
                        values.push_back(++value);
                    }
                }
                else
                {
                    std::uniform_int_distribution<int> dist(sensor.getMinimum(), sensor.getMaximum());

                    int size = 1;
                    if (!sensor.getDescription().empty())
                    {
                        // get sensor size from description as the size param is removed
                        try
                        {
                            size = std::stoi(sensor.getDescription());
                        }
                        catch (...)
                        {
                        }
                    }

                    for (int i = 0; i < size; ++i)
                    {
                        int rand_num = dist(mt);
                        values.push_back(rand_num);
                    }
                }

                wolk->addSensorReading(device.getKey(), sensor.getReference(), values);
            }
        }

        wolk->publish();

        std::this_thread::sleep_for(std::chrono::milliseconds(interval));
    }
    return 0;
}

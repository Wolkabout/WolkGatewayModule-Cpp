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

#include "service/FirmwareUpdateService.h"
#include "FirmwareInstaller.h"
#include "FirmwareVersionProvider.h"
#include "connectivity/ConnectivityService.h"
#include "model/FirmwareUpdateAbort.h"
#include "model/FirmwareUpdateInstall.h"
#include "model/FirmwareUpdateStatus.h"
#include "model/FirmwareVersion.h"
#include "model/Message.h"
#include "protocol/json/JsonDFUProtocol.h"
#include "utilities/FileSystemUtils.h"
#include "utilities/Logger.h"
#include "utilities/StringUtils.h"

namespace wolkabout
{
FirmwareUpdateService::FirmwareUpdateService(JsonDFUProtocol& protocol,
                                             std::shared_ptr<FirmwareInstaller> firmwareInstaller,
                                             std::shared_ptr<FirmwareVersionProvider> firmwareVersionProvider,
                                             ConnectivityService& connectivityService)
: m_protocol{protocol}
, m_firmwareInstaller{firmwareInstaller}
, m_firmwareVersionProvider{firmwareVersionProvider}
, m_connectivityService{connectivityService}
{
}

void FirmwareUpdateService::messageReceived(std::shared_ptr<Message> message)
{
    auto installCommand = m_protocol.makeFirmwareUpdateInstall(*message);
    if (installCommand)
    {
        auto installDto = *installCommand;
        addToCommandBuffer([=] { handleFirmwareUpdateCommand(installDto); });

        return;
    }

    auto abortCommand = m_protocol.makeFirmwareUpdateAbort(*message);
    if (abortCommand)
    {
        auto abortDto = *abortCommand;
        addToCommandBuffer([=] { handleFirmwareUpdateCommand(abortDto); });

        return;
    }

    LOG(WARN) << "Unable to parse message; channel: " << message->getChannel()
              << ", content: " << message->getContent();
}

const Protocol& FirmwareUpdateService::getProtocol()
{
    return m_protocol;
}

void FirmwareUpdateService::publishFirmwareVersion(const std::string& deviceKey)
{
    addToCommandBuffer([=] {
        const std::string firmwareVersion = m_firmwareVersionProvider->getFirmwareVersion(deviceKey);

        if (firmwareVersion.empty())
        {
            LOG(WARN) << "Failed to get firmware version for device " << deviceKey;
            return;
        }

        const std::shared_ptr<Message> message =
          m_protocol.makeMessage(deviceKey, FirmwareVersion{deviceKey, firmwareVersion});

        if (!message)
        {
            LOG(WARN) << "Failed to create firmware version message";
            return;
        }

        if (!m_connectivityService.publish(message))
        {
            LOG(WARN) << "Failed to publish firmware version message";
            return;
        }
    });
}

void FirmwareUpdateService::handleFirmwareUpdateCommand(const FirmwareUpdateInstall& command)
{
    if (command.getDeviceKeys().size() != 1 || command.getDeviceKeys().at(0).empty())
    {
        LOG(WARN) << "Unable to extract device key from firmware install command";
        return;
    }

    auto deviceKey = command.getDeviceKeys().at(0);

    auto firmwareFile = command.getFileName();

    if (firmwareFile.empty())
    {
        LOG(WARN) << "Missing file path in firmware install command";

        sendStatus(FirmwareUpdateStatus{{deviceKey}, FirmwareUpdateStatus::Error::FILE_SYSTEM_ERROR});
        return;
    }

    if (!FileSystemUtils::isFilePresent(firmwareFile))
    {
        LOG(WARN) << "Missing firmware file: " << firmwareFile;

        sendStatus(FirmwareUpdateStatus{{deviceKey}, FirmwareUpdateStatus::Error::FILE_SYSTEM_ERROR});
        return;
    }

    install(deviceKey, firmwareFile);
}

void FirmwareUpdateService::handleFirmwareUpdateCommand(const FirmwareUpdateAbort& command)
{
    if (command.getDeviceKeys().size() != 1 || command.getDeviceKeys().at(0).empty())
    {
        LOG(WARN) << "Unable to extract device key from firmware abort command";
        return;
    }

    auto deviceKey = command.getDeviceKeys().at(0);

    abort(deviceKey);
}

void FirmwareUpdateService::install(const std::string& deviceKey, const std::string& firmwareFilePath)
{
    sendStatus(FirmwareUpdateStatus{{deviceKey}, FirmwareUpdateStatus::Status::INSTALLATION});

    m_firmwareInstaller->install(deviceKey, firmwareFilePath, [=](const std::string& key) { installSucceeded(key); },
                                 [=](const std::string& key) { installFailed(key); });
}

void FirmwareUpdateService::installSucceeded(const std::string& deviceKey)
{
    sendStatus(FirmwareUpdateStatus{{deviceKey}, FirmwareUpdateStatus::Status::COMPLETED});
    publishFirmwareVersion(deviceKey);
}

void FirmwareUpdateService::installFailed(const std::string& deviceKey)
{
    sendStatus(FirmwareUpdateStatus{{deviceKey}, FirmwareUpdateStatus::Error::INSTALLATION_FAILED});
}

void FirmwareUpdateService::abort(const std::string& deviceKey)
{
    LOG(INFO) << "Abort firmware installation for device: " << deviceKey;
    if (m_firmwareInstaller->abort(deviceKey))
    {
        LOG(INFO) << "Firmware installation aborted for device: " << deviceKey;
        sendStatus(FirmwareUpdateStatus{{deviceKey}, FirmwareUpdateStatus::Status::ABORTED});
    }
    else
    {
        LOG(INFO) << "Firmware installation cannot be aborted for device: " << deviceKey;
    }
}

void FirmwareUpdateService::sendStatus(const FirmwareUpdateStatus& response)
{
    auto& deviceKey = response.getDeviceKeys().at(0);
    std::shared_ptr<Message> message = m_protocol.makeMessage(deviceKey, response);

    if (!message)
    {
        LOG(WARN) << "Failed to create firmware update response";
        return;
    }

    if (!m_connectivityService.publish(message))
    {
        LOG(WARN) << "Firmware update response not published for device: " << deviceKey;
    }
}

void FirmwareUpdateService::addToCommandBuffer(std::function<void()> command)
{
    m_commandBuffer.pushCommand(std::make_shared<std::function<void()>>(command));
}
}    // namespace wolkabout

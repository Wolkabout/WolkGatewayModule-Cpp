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
#include "connectivity/ConnectivityService.h"
#include "model/FirmwareUpdateCommand.h"
#include "model/FirmwareUpdateResponse.h"
#include "model/Message.h"
#include "protocol/FirmwareUpdateProtocol.h"
#include "utilities/FileSystemUtils.h"
#include "utilities/Logger.h"
#include "utilities/StringUtils.h"

namespace wolkabout
{
FirmwareUpdateService::FirmwareUpdateService(FirmwareUpdateProtocol& protocol,
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
    if (!m_protocol.isFirmwareUpdateMessage(*message))
    {
        LOG(WARN) << "Unable to parse message channel: " << message->getChannel();
        return;
    }

    auto command = m_protocol.makeFirmwareUpdateCommand(*message);
    if (!command)
    {
        LOG(WARN) << "Unable to parse message contents: " << message->getContent();
        return;
    }

    const std::string deviceKey = m_protocol.extractDeviceKeyFromChannel(message->getChannel());
    if (deviceKey.empty())
    {
        LOG(WARN) << "Unable to extract device key from channel: " << message->getChannel();
    }

    auto dfuCommand = *command;
    addToCommandBuffer([=] { handleFirmwareUpdateCommand(dfuCommand, deviceKey); });
}

const Protocol& FirmwareUpdateService::getProtocol()
{
    return m_protocol;
}

void FirmwareUpdateService::handleFirmwareUpdateCommand(const FirmwareUpdateCommand& command,
                                                        const std::string deviceKey)
{
    switch (command.getType())
    {
    case FirmwareUpdateCommand::Type::FILE_UPLOAD:
    {
        LOG(WARN) << "Unsupported subdevice download protocol: FILE_UPLOAD";
        break;
    }
    case FirmwareUpdateCommand::Type::URL_DOWNLOAD:
    {
        if (!command.getUrl() || command.getUrl().value().empty())
        {
            LOG(WARN) << "Missing url from firmware update command";
            sendResponse(FirmwareUpdateResponse{FirmwareUpdateResponse::Status::ERROR,
                                                FirmwareUpdateResponse::ErrorCode::UNSPECIFIED_ERROR},
                         deviceKey);
            return;
        }

        bool autoInstall = command.getAutoInstall() ? command.getAutoInstall().value() : false;

        urlDownload(deviceKey, command.getUrl().value(), autoInstall);
        break;
    }
    case FirmwareUpdateCommand::Type::INSTALL:
    {
        // TODO
        break;
    }
    case FirmwareUpdateCommand::Type::ABORT:
    {
        // TODO
        break;
    }
    default:
    {
        sendResponse(FirmwareUpdateResponse{FirmwareUpdateResponse::Status::ERROR,
                                            FirmwareUpdateResponse::ErrorCode::UNSPECIFIED_ERROR},
                     deviceKey);
    }
    }
}

void FirmwareUpdateService::urlDownload(const std::string& deviceKey, const std::string& url, bool autoInstall)
{
    m_fileDownloader.download(url,
                              [=](const std::string& filePath) { downloadCompleted(filePath, deviceKey, autoInstall); },
                              [=](LocalFileDownloader::ErrorCode errorCode) { downloadFailed(errorCode, deviceKey); });
}

void FirmwareUpdateService::downloadCompleted(const std::string& filePath, const std::string& deviceKey,
                                              bool autoInstall)
{
    addToCommandBuffer([=] {
        sendResponse(FirmwareUpdateResponse{FirmwareUpdateResponse::Status::FILE_READY}, deviceKey);

        if (autoInstall)
        {
            install(deviceKey, filePath);
        }
    });
}

void FirmwareUpdateService::downloadFailed(LocalFileDownloader::ErrorCode errorCode, const std::string& deviceKey)
{
    switch (errorCode)
    {
    case LocalFileDownloader::ErrorCode::FILE_DOES_NOT_EXIST:
    {
        sendResponse(FirmwareUpdateResponse{FirmwareUpdateResponse::Status::ERROR,
                                            FirmwareUpdateResponse::ErrorCode::FILE_SYSTEM_ERROR},
                     deviceKey);
        break;
    }
    case LocalFileDownloader::ErrorCode::UNSPECIFIED_ERROR:
    default:
    {
        sendResponse(FirmwareUpdateResponse{FirmwareUpdateResponse::Status::ERROR,
                                            FirmwareUpdateResponse::ErrorCode::UNSPECIFIED_ERROR},
                     deviceKey);
        break;
    }
    }

    // TODO remove from map
}

void FirmwareUpdateService::install(const std::string& deviceKey, const std::string& firmwareFilePath) {}

void FirmwareUpdateService::sendResponse(const FirmwareUpdateResponse& response, const std::string& deviceKey)
{
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

void FirmwareUpdateService::LocalFileDownloader::download(const std::string& filePath,
                                                          std::function<void(const std::string& path)> onSuccess,
                                                          std::function<void(ErrorCode)> onFail)
{
    if (FileSystemUtils::isFilePresent(filePath))
    {
        onSuccess(filePath);
    }

    onFail(ErrorCode::FILE_DOES_NOT_EXIST);
}
}    // namespace wolkabout

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

#ifndef DEVICEREGISTRATIONSERVICE_H
#define DEVICEREGISTRATIONSERVICE_H

#include "core/InboundMessageHandler.h"
#include "core/model/SubdeviceRegistrationResponse.h"

#include <functional>
#include <vector>

namespace wolkabout
{
class ConnectivityService;
class DetailedDevice;
class RegistrationProtocol;
class SubdeviceUpdateRequest;

typedef std::function<void(const std::string&, PlatformResult::Code)> RegistrationResponseHandler;
typedef std::function<void(const std::string&, PlatformResult::Code)> UpdateResponseHandler;

class DeviceRegistrationService : public MessageListener
{
public:
    DeviceRegistrationService(RegistrationProtocol& protocol, ConnectivityService& connectivityService,
                              const RegistrationResponseHandler& registrationResponseHandler,
                              const UpdateResponseHandler& updateResponseHandler);

    void messageReceived(std::shared_ptr<Message> message) override;
    const Protocol& getProtocol() override;

    void publishRegistrationRequest(const DetailedDevice& device);
    void publishUpdateRequest(const SubdeviceUpdateRequest& request);

private:
    RegistrationProtocol& m_protocol;
    ConnectivityService& m_connectivityService;
    RegistrationResponseHandler m_registrationResponseHandler;
    UpdateResponseHandler m_updateResponseHandler;
};
}    // namespace wolkabout

#endif    // DEVICEREGISTRATIONSERVICE_H

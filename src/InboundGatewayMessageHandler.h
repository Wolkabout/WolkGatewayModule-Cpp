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

#ifndef INBOUNDGATEWAYMESSAGEHANDLER_H
#define INBOUNDGATEWAYMESSAGEHANDLER_H

#include "core/InboundMessageHandler.h"
#include "core/utilities/CommandBuffer.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace wolkabout
{
class InboundGatewayMessageHandler : public InboundMessageHandler
{
public:
    InboundGatewayMessageHandler();

    ~InboundGatewayMessageHandler();

    void messageReceived(const std::string& channel, const std::string& message) override;

    std::vector<std::string> getChannels() const override;

    void addListener(std::weak_ptr<MessageListener> listener) override;

private:
    void addToCommandBuffer(std::function<void()> command);

    std::unique_ptr<CommandBuffer> m_commandBuffer;

    std::vector<std::string> m_subscriptionList;

    std::map<std::string, std::weak_ptr<MessageListener>> m_channelHandlers;

    mutable std::mutex m_lock;
};
}    // namespace wolkabout

#endif    // INBOUNDGATEWAYMESSAGEHANDLER_H

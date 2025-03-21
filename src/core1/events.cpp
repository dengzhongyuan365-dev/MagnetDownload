#include "events.h"

namespace bt {

void EventSystem::registerHandler(EventType type, EventHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 生成新的处理函数ID
    size_t handlerId = nextHandlerId_++;
    
    // 将处理函数添加到映射表
    handlers_[type][handlerId] = std::move(handler);
}

void EventSystem::unregisterHandler(EventType type, size_t handlerId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 查找事件类型
    auto typeIt = handlers_.find(type);
    if (typeIt != handlers_.end()) {
        // 查找处理函数ID
        auto& handlers = typeIt->second;
        auto handlerIt = handlers.find(handlerId);
        if (handlerIt != handlers.end()) {
            // 从映射表中移除
            handlers.erase(handlerIt);
            
            // 如果该事件类型没有处理函数，移除整个事件类型
            if (handlers.empty()) {
                handlers_.erase(typeIt);
            }
        }
    }
}

void EventSystem::fireEvent(const std::shared_ptr<EventData>& eventData) {
    // 检查事件数据是否有效
    if (!eventData) {
        return;
    }
    
    // 获取事件类型
    EventType type = eventData->getType();
    
    // 创建处理函数的副本，避免在回调期间修改映射表
    std::vector<EventHandler> handlersToCall;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 查找事件类型
        auto typeIt = handlers_.find(type);
        if (typeIt != handlers_.end()) {
            // 为所有处理函数创建副本
            const auto& handlers = typeIt->second;
            handlersToCall.reserve(handlers.size());
            
            for (const auto& [id, handler] : handlers) {
                handlersToCall.push_back(handler);
            }
        }
    }
    
    // 调用所有处理函数
    for (const auto& handler : handlersToCall) {
        handler(eventData);
    }
}

} // namespace bt 
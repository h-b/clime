#ifndef CLIME_HPP
#define CLIME_HPP

#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <utility>

#if __cplusplus < 201402L
#error You need at least C++14 for clime.hpp. Please check example/CMakeLists.txt on how to set compiler options. Reason: This code uses std::get<T> to extract the elements of a std::tuple whose type is T.
#endif

namespace clime
{
	template<typename... MessageTypes>
	class message_manager
	{
	public:
		
		template<typename MessageType>
		void send_message(std::shared_ptr<MessageType> msg, unsigned int max_queued_messages=0)
		{
			using QueueType = std::queue<std::shared_ptr<MessageType>>;
			{
				std::unique_lock<std::mutex> lock(mutex_messages_);
				cv_.wait(lock, [&]
				{
					return max_queued_messages==0 || std::get<QueueType>(messages_).size() < max_queued_messages;
				});
				
				std::get<QueueType>(messages_).emplace(msg);
			}
			cv_.notify_one();
		}
		
		template<typename MessageType>
		std::shared_ptr<MessageType> receive_message(bool wait_for_message=false)
		{
			using QueueType = std::queue<std::shared_ptr<MessageType>>;
			
			std::shared_ptr<MessageType> result;
			{
				std::unique_lock<std::mutex> lock(mutex_messages_);
				cv_.wait(lock, [&]
				{
					return !wait_for_message || !std::get<QueueType>(messages_).empty();
				});
				
				auto& messages = std::get<QueueType>(messages_);
				
				if (!messages.empty())
				{
					result = messages.front();
					messages.pop();
				}
			}
			
			cv_.notify_one();
			return result;
		}
		
	private:
		std::tuple<std::queue<std::shared_ptr<MessageTypes>>...> messages_;
		std::mutex mutex_messages_;
		std::condition_variable cv_;
	};
}

#endif // CLIME_HPP

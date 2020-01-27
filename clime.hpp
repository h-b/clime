/*
clime C++ LIght MEssage passing library
Read the full documentation at README.md or at https://github.com/h-b/clime

MIT License

Copyright (c) 2020 Stefan Zipproth <info@zipproth.de>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#ifndef CLIME_HPP
#define CLIME_HPP

#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <utility>
#include <future>
#include <chrono>
#include <thread>
#include <atomic>

#if __cplusplus < 201402L
#error You need at least C++14 for clime.hpp. Please check example/CMakeLists.txt on how to set compiler options. Reason: This code uses std::get<T> to extract the elements of a std::tuple whose type is T.
#endif

namespace clime
{
	template<typename... MessageTypes>
	class message_manager
	{
	public:

		virtual ~message_manager()
		{
			dispose();
		}

		void dispose()
		{
			{
				std::lock_guard<std::mutex> lock(mutex_messages_);
				running_ = false;
			}
			cv_.notify_one();
		}
		
		template<typename MessageType>
		void send_message(std::shared_ptr<MessageType> msg, unsigned int max_queued_messages=0)
		{
			using QueueType = std::queue<std::shared_ptr<MessageType>>;
			{
				std::unique_lock<std::mutex> lock(mutex_messages_);
				cv_.wait(lock, [&]
				{
					return !running_ || max_queued_messages==0 || std::get<QueueType>(messages_).size() < max_queued_messages;
				});
				
				std::get<QueueType>(messages_).emplace(msg);
			}
			cv_.notify_one();

			using FunctionType = std::function<void(std::shared_ptr<MessageType>,bool)>;
			if (std::get<FunctionType>(logger_) != nullptr)
			{
				std::get<FunctionType>(logger_)(msg, true);
			}
		}
		
		template<typename MessageType, typename Rep, typename Period>
		void send_message(std::shared_ptr<MessageType> msg, const std::chrono::duration<Rep, Period>& delay_duration)
		{
			(void)std::async(std::launch::async, [msg,delay_duration,this]()
			{
				std::this_thread::sleep_for(delay_duration);
				send_message(msg);
			});
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
					return !running_ || !wait_for_message || !std::get<QueueType>(messages_).empty();
				});
				
				auto& messages = std::get<QueueType>(messages_);
				
				if (!messages.empty())
				{
					result = messages.front();
					messages.pop();
				}
			}
			
			cv_.notify_one();

			using FunctionType = std::function<void(std::shared_ptr<MessageType>,bool)>;
			if (std::get<FunctionType>(logger_) != nullptr)
			{
				std::get<FunctionType>(logger_)(result, false);
			}

			return result;
		}

		template<typename MessageType>
		void set_logger(std::function<void(std::shared_ptr<MessageType>,bool)> logger)
		{
			using FunctionType = std::function<void(std::shared_ptr<MessageType>,bool)>;
			std::get<FunctionType>(logger_) = logger;
		}
		
	private:
		std::tuple<std::queue<std::shared_ptr<MessageTypes>>...> messages_;
		std::tuple<std::function<void(std::shared_ptr<MessageTypes>,bool)>...> logger_;
		std::mutex mutex_messages_;
		std::condition_variable cv_;
		std::atomic<bool>running_{ true };
	};

	template<typename MessageType, typename MessageManagerType>
	class message_handler
	{
	public:
		message_handler(MessageManagerType& msg_manager,
		                std::function<void(const MessageType& message_type)> on_message,
		                std::function<void(const std::exception& exception)> on_exception=nullptr)
		    : _msg_manager(msg_manager)
			, on_exception_(on_exception)
			, thread_(std::thread([=]
			{
				run(on_message, nullptr);
			}))
		{
		}

		message_handler(MessageManagerType& msg_manager,
		                std::function<void(const MessageType& message_type)> on_message,
						std::function<void()> on_idle,
			            std::function<void(const std::exception& exception)> on_exception=nullptr)
		    : _msg_manager(msg_manager)
			, on_exception_(on_exception)
			, thread_(std::thread([=]
			{
				run(on_message, on_idle);
			}))
		{
		}

		~message_handler()
		{
			try
			{
				dispose();
				if (thread_.joinable())
				{
					thread_.join();
				}
			}
			catch (const std::exception & ex)
			{
				if (on_exception_)
				{
					on_exception_(ex);
				}
			}
		}

		void dispose()
		{
			try
			{
				_msg_manager.dispose();
				running_ = false;
			}
			catch (const std::exception& ex)
			{
				if (on_exception_)
				{
					on_exception_(ex);
				}
			}
		}

	private:
		MessageManagerType& _msg_manager;
		std::function<void(const std::exception& exception)> on_exception_;
		std::thread thread_;
		std::atomic<bool> running_{false};

		void run(std::function<void(const MessageType& message_type)> on_message,
				 std::function<void()> on_idle)
		{
			running_ = true;
			const std::runtime_error unknown_exception("unknown exception");

			while (running_)
			{
				try
				{
					auto incoming_message = _msg_manager.template receive_message<MessageType>(on_idle==nullptr);

					if (incoming_message)
					{
						on_message(*incoming_message);
					}
					else
					{
						on_idle();
					}
				}
				catch(const std::exception& ex)
				{
					if (on_exception_)
					{
						on_exception_(ex);
					}
				}
				catch(...)
				{
					if (on_exception_)
					{
						on_exception_(unknown_exception);
					}
				}
			}
		}
	};
}

#endif // CLIME_HPP

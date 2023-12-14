/*
clime C++ LIght MEssage passing library
Read the full documentation at README.md or at https://github.com/h-b/clime

MIT License

Copyright (c) 2020-2023 Stefan Zipproth <s.zipproth@acrion.ch>

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

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <list>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

#if __cplusplus < 201402L
    #error You need at least C++14 for clime.hpp. Please check example/CMakeLists.txt on how to set compiler options. Reason: This code uses std::get<T> to extract the elements of a std::tuple whose type is T.
#endif

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef NOMSG
        #define NOMSG
    #endif
    #include <windows.h>
#elif defined(__APPLE__)
    #include <pthread.h>
#else
    #include <sys/prctl.h>
#endif

#ifdef _MSC_VER
    #define CLIME_DEMANGLED_CLASS_NAME(demangling_status) typeid(*this).name()
#else
    #include <cxxabi.h>
    #define CLIME_DEMANGLED_CLASS_NAME(demangling_status) abi::__cxa_demangle(typeid(*this).name(), 0, 0, &(demangling_status))
#endif

namespace clime
{
#ifdef _WIN32
    #pragma pack(push, 8)
    typedef struct tagTHREADNAME_INFO
    {
        DWORD  dwType;     // Must be 0x1000.
        LPCSTR szName;     // Pointer to name (in user addr space).
        DWORD  dwThreadID; // Thread ID (-1=caller thread).
        DWORD  dwFlags;    // Reserved for future use, must be zero.
    } THREADNAME_INFO;
    #pragma pack(pop)

    inline void set_thread_name(uint32_t dwThreadID, const char* thread_name)
    {
        const DWORD     MS_VC_EXCEPTION = 0x406D1388;
        THREADNAME_INFO info;
        info.dwType     = 0x1000;
        info.szName     = thread_name;
        info.dwThreadID = dwThreadID;
        info.dwFlags    = 0;

        __try
        {
            RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    inline void set_thread_name(const char* thread_name)
    {
        set_thread_name(GetCurrentThreadId(), thread_name);
    }

    inline void set_thread_name(std::thread& thread, const char* thread_name)
    {
        DWORD thread_id = ::GetThreadId(static_cast<HANDLE>(thread.native_handle()));
        set_thread_name(thread_id, thread_name);
    }
#elif defined(__APPLE__)
    inline void set_thread_name(std::thread& thread, const char* thread_name)
    {
        // not possible under darwin
    }

    inline void set_thread_name(const char* thread_name)
    {
        pthread_setname_np(thread_name);
    }
#else
    inline void set_thread_name(std::thread& thread, const char* thread_name)
    {
        pthread_setname_np(thread.native_handle(), thread_name);
    }

    inline void set_thread_name(const char* thread_name)
    {
        prctl(PR_SET_NAME, thread_name, 0, 0, 0);
    }
#endif

    template <typename... MessageTypes>
    class message_manager
    {
    private:
        template <typename MessageType>
        class message_handler
        {
            using MessageTypeWithMetaData = std::pair<std::shared_ptr<MessageType>, int>;

        public:
            message_handler(message_manager&                                            msg_manager,
                            const std::function<void(const std::exception& exception)>& on_exception = nullptr,
                            const std::string&                                          thread_name  = {})
                : msg_manager_(msg_manager)
                , on_exception_(on_exception)
                , thread_name_(thread_name.empty() ? CLIME_DEMANGLED_CLASS_NAME(demangling_status_) : thread_name)
            {
            }

            virtual ~message_handler()
            {
                try
                {
                    {
                        std::lock_guard<std::mutex> lock(msg_manager_.mutex_messages_);
                        msg_manager_.running_ = false;
                    }
                    msg_manager_.cv_.notify_one();

                    if (thread_.joinable())
                    {
                        thread_.join();
                    }
                }
                catch (const std::exception& ex)
                {
                    if (on_exception_)
                    {
                        on_exception_(ex);
                    }
                }
            }

            void start_thread(std::function<void(std::shared_ptr<MessageType> message_type)> on_message,
                              const std::function<void()>&                                   on_idle,
                              const std::function<void()>&                                   on_exit)
            {
                thread_ = std::thread([this, on_message, on_idle, on_exit]
                                      {
                    auto pos = thread_name_.rfind("message_handler");
                    if (pos != std::string::npos)
                    {
                        thread_name_ = thread_name_.substr(pos);
                    }

                    set_thread_name(thread_name_.c_str());
                    run(on_message, on_idle);
                    if (on_exit)
                    {
                        on_exit();
                    } });
            }

        protected:
            message_manager&                                     msg_manager_;
            std::function<void(const std::exception& exception)> on_exception_;
            std::string                                          thread_name_;
            int                                                  demangling_status_{};
            std::thread                                          thread_;

            void run(std::function<void(std::shared_ptr<MessageType> message_type)> on_message,
                     const std::function<void()>&                                   on_idle)
            {
                const std::runtime_error unknown_exception("unknown exception");

                while (msg_manager_.running_)
                {
                    try
                    {
                        auto incoming_message = msg_manager_.template receive_message<MessageType>(on_idle == nullptr);

                        if (msg_manager_.running_)
                        {
                            if (incoming_message)
                            {
                                on_message(incoming_message);
                            }
                            else if (on_idle)
                            {
                                on_idle();
                            }
                        }
                    }
                    catch (const std::exception& ex)
                    {
                        if (on_exception_ && msg_manager_.running_)
                        {
                            on_exception_(ex);
                        }
                    }
                    catch (...)
                    {
                        if (on_exception_ && msg_manager_.running_)
                        {
                            on_exception_(unknown_exception);
                        }
                    }
                }
            }
        };

    public:
        message_manager()
            : message_handler_(std::make_shared<std::tuple<std::list<std::shared_ptr<message_handler<MessageTypes>>>...>>())
        {
        }

        virtual ~message_manager()
        {
            dispose();
        }

        void dispose()
        {
            clear_all_loggers();
            clear_all_messages();
            {
                std::lock_guard<std::mutex> lock(mutex_messages_);
                running_ = false;
            }
            cv_.notify_one();
            message_handler_.reset();
        }

        template <typename MessageType>
        void clear_messages()
        {
            using MessageTypeWithMetaData = std::pair<std::shared_ptr<MessageType>, int>;

            std::lock_guard<std::mutex>         lock(mutex_messages_);
            std::list<MessageTypeWithMetaData>& message_queue = std::get<std::list<MessageTypeWithMetaData>>(messages_);
            message_queue.clear();
        }

        void clear_all_messages()
        {
            clear_all_messages_helper(std::index_sequence_for<MessageTypes...>());
        }

        template <typename MessageType>
        std::size_t size() const
        {
            using MessageTypeWithMetaData = std::pair<std::shared_ptr<MessageType>, int>;

            using QueueType = std::list<MessageTypeWithMetaData>;
            std::lock_guard<std::mutex> lock(mutex_messages_);
            return std::get<QueueType>(messages_).size();
        }

        std::size_t total_size() const
        {
            return total_size_helper(std::index_sequence_for<MessageTypes...>());
        }

        template <typename MessageType>
        void send_message(std::shared_ptr<MessageType> msg, unsigned int max_queued_messages = 0, int target_id = -1)
        {
            using MessageTypeWithMetaData = std::pair<std::shared_ptr<MessageType>, int>;
            using QueueType               = std::list<MessageTypeWithMetaData>;
            {
                std::unique_lock<std::mutex> lock(mutex_messages_);
                cv_.wait(lock, [&]
                         { return !running_ || max_queued_messages == 0 || std::get<QueueType>(messages_).size() < max_queued_messages; });

                // std::get<QueueType>(messages_).emplace_back(std::make_pair<std::shared_ptr<MessageType>, int>(std::forward<std::shared_ptr<MessageType>>(msg), target_id));
                std::get<QueueType>(messages_).emplace_back(std::make_pair<std::shared_ptr<MessageType>>(std::forward<std::shared_ptr<MessageType>>(msg), target_id));
            }
            cv_.notify_one();

            using FunctionType = std::function<void(std::shared_ptr<MessageType>, bool)>;
            if (std::get<FunctionType>(logger_) != nullptr)
            {
                std::get<FunctionType>(logger_)(msg, true);
            }
        }

        template <typename MessageType, typename Rep, typename Period>
        void send_message(std::shared_ptr<MessageType> msg, const std::chrono::duration<Rep, Period>& delay_duration)
        {
            std::lock_guard<std::mutex> lock(mutex_future_pool_);
            std::future<void>*          future            = nullptr;
            std::size_t                 last_ready_future = future_pool_.size();

            for (std::size_t i = 0; i < future_pool_.size(); ++i)
            {
                if (future_pool_[i].wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                {
                    if (future == nullptr)
                    {
                        future = &future_pool_[i];
                    }
                    else
                    {
                        last_ready_future = i;
                    }
                }
            }

            future_pool_.resize(last_ready_future);

            if (future == nullptr)
            {
                future_pool_.emplace_back();
                future = &future_pool_.back();
            }

            *future = std::async(std::launch::async, [msg, delay_duration, this]()
                                 {
                                     std::this_thread::sleep_for(delay_duration);
                                     send_message(msg); });
        }

        template <typename MessageType>
        std::shared_ptr<MessageType> receive_message(bool wait_for_message = false, const int request_target_id = -1)
        {
            using MessageTypeWithMetaData = std::pair<std::shared_ptr<MessageType>, int>;
            using QueueType               = std::list<MessageTypeWithMetaData>;

            std::shared_ptr<MessageType> result;
            {
                std::unique_lock<std::mutex> lock(mutex_messages_);

                auto& messages = std::get<QueueType>(messages_);

                auto message_selector = [&request_target_id](const MessageTypeWithMetaData& message)
                {
                    return message.second == -1 || message.second == request_target_id;
                };

                while (true)
                {
                    auto it = std::find_if(messages.begin(), messages.end(), message_selector);

                    if (it != messages.end())
                    {
                        result = it->first;
                        messages.erase(it);
                        break;
                    }
                    else if (!wait_for_message || !running_)
                    {
                        break;
                    }
                    else
                    {
                        cv_.wait(lock);
                    }
                }
            }

            cv_.notify_one();

            using FunctionType = std::function<void(std::shared_ptr<MessageType>, bool)>;
            if (std::get<FunctionType>(logger_) != nullptr && result)
            {
                std::get<FunctionType>(logger_)(result, false);
            }

            return result;
        }

        template <typename MessageType>
        void set_logger(std::function<void(std::shared_ptr<MessageType>, bool)> logger)
        {
            using FunctionType              = std::function<void(std::shared_ptr<MessageType>, bool)>;
            std::get<FunctionType>(logger_) = logger;
        }

        template <typename MessageType>
        void add_handler(
            std::function<void(std::shared_ptr<MessageType> message_type)> on_message,
            std::function<void(const std::exception& exception)>           on_exception = nullptr,
            std::function<void()>                                          on_idle      = nullptr,
            std::function<void()>                                          on_exit      = nullptr,
            const std::string&                                             thread_name  = "")
        {
            using HandlerListType                 = std::list<std::shared_ptr<message_handler<MessageType>>>;
            HandlerListType& message_handler_list = std::get<HandlerListType>(*message_handler_);
            message_handler_list.emplace_back(std::make_shared<message_handler<MessageType>>(*this, on_exception, thread_name));
            message_handler_list.back()->start_thread(on_message, on_idle, on_exit);
        }

        template <typename MessageType>
        void clear_handlers()
        {
            using HandlerListType                 = std::list<std::shared_ptr<message_handler<MessageType>>>;
            HandlerListType& message_handler_list = std::get<HandlerListType>(*message_handler_);
            message_handler_list.clear(); // ends all message_handler threads that handled MessageType
        }

        void clear_all_loggers()
        {
            clear_logger_helper(std::index_sequence_for<MessageTypes...>());
        }

    protected:
        std::tuple<std::list<std::pair<std::shared_ptr<MessageTypes>, int>>...>                   messages_;
        std::tuple<std::function<void(std::shared_ptr<MessageTypes>, bool)>...>                   logger_;
        std::shared_ptr<std::tuple<std::list<std::shared_ptr<message_handler<MessageTypes>>>...>> message_handler_;
        mutable std::mutex                                                                        mutex_messages_;
        std::condition_variable                                                                   cv_;
        std::atomic<bool>                                                                         running_{true};
        std::mutex                                                                                mutex_future_pool_;
        std::vector<std::future<void>>                                                            future_pool_;

    private:
        template <std::size_t... Is>
        void clear_all_messages_helper(std::index_sequence<Is...>)
        {
            (clear_messages<MessageTypes>(), ...);
        }

        template <std::size_t... Is>
        void clear_logger_helper(std::index_sequence<Is...>)
        {
            (clear_logger<MessageTypes>(), ...);
        }

        template <typename MessageType>
        void clear_logger()
        {
            set_logger<MessageType>(nullptr);
        }

        template <std::size_t... Is>
        std::size_t total_size_helper(std::index_sequence<Is...>) const
        {
            return (size<MessageTypes>() + ...);
        }
    };

    template <typename Result>
    class future
    {
    public:
        future() = default;

        template <typename AsyncOp>
        future(const AsyncOp& async_op) // NOLINT(google-explicit-constructor)
        {
            operator=(async_op);
        }

        template <typename AsyncOp>
        future& operator=(const AsyncOp& async_op)
        {
            manager_future_.template clear_handlers<start_op>(); // remove any previous handlers - a future always has only 1 future function

            manager_future_.template add_handler<start_op>([&, async_op](std::shared_ptr<start_op>)
                                                           { manager_future_.send_message(std::make_shared<Result>(async_op())); });

            manager_future_.send_message(std::make_shared<start_op>());

            manager_future_.template add_handler<Result>([&](std::shared_ptr<Result> result)
                                                         {
                                                             {
                                                                 std::lock_guard<std::mutex> lock(mtx_);
                                                                 if (!result_)
                                                                 {
                                                                     // While the future function was running, result_ may have been set by operator =(const Result& result),
                                                                     // so we only set result_ if it is not yet initialized.
                                                                     result_ = result;
                                                                 }
                                                             }
                                                             cv_.notify_one(); });

            return *this;
        }

        future& operator=(const Result& result) // directly sets results without future function
        {
            {
                std::lock_guard<std::mutex> lock(mtx_);
                result_ = std::make_shared<Result>(result);
            }
            cv_.notify_one();
            return *this;
        }

        operator Result() // NOLINT(google-explicit-constructor)
        {
            std::unique_lock<std::mutex> lock(mtx_);
            cv_.wait(lock, [&]
                     { return manager_future_.template size<start_op>() == 0 || static_cast<bool>(result_); });
            return result_ ? *result_ : Result(); // if there is no future function, return default value of its return type
        }

        bool ready()
        {
            std::lock_guard<std::mutex> lock(mtx_);
            return result_;
        }

        std::shared_ptr<const Result> get()
        {
            {
                std::unique_lock<std::mutex> lock(mtx_);
                cv_.wait(lock, [&]
                         { return static_cast<bool>(result_); });
            }
            return result_;
        }

    private:
        struct start_op
        {
        };
        message_manager<start_op, Result> manager_future_;
        std::mutex                        mtx_;
        std::condition_variable           cv_;
        std::shared_ptr<const Result>     result_;
    };

    class thread_manager
    {
    public:
        explicit thread_manager(std::function<void()> on_idle, std::function<void(const std::exception& exception)> on_exception = nullptr)
        {
            message_manager_.add_handler<int>(nullptr, std::move(on_exception), std::move(on_idle));
        }

    private:
        message_manager<int> message_manager_;
    };
}

#endif // CLIME_HPP

<!-- START doctoc generated TOC please keep comment here to allow auto update -->
<!-- DON'T EDIT THIS SECTION, INSTEAD RE-RUN doctoc TO UPDATE -->


- [Introduction](#introduction)
- [Motivation](#motivation)
- [Using the library](#using-the-library)
  - [Basic usage](#basic-usage)
  - [How to send a delayed message](#how-to-send-a-delayed-message)
  - [How to avoid exploding message queues](#how-to-avoid-exploding-message-queues)
  - [How to wait for a certain message type](#how-to-wait-for-a-certain-message-type)
  - [How to log all messages](#how-to-log-all-messages)
  - [How to add an asynchronous message handler](#how-to-add-an-asynchronous-message-handler)
    - [Basics](#basics)
    - [Exception handling](#exception-handling)
    - [Handling idle times](#handling-idle-times)
    - [How to shutdown](#how-to-shutdown)

<!-- END doctoc generated TOC please keep comment here to allow auto update -->

**clime** - **C**++ **LI**ght **ME**ssage passing library
# Introduction

This platform independent library provides basic helper functions to send messages between std::threads in a C++ application
(no inter process communication). The basic idea is to provide a lightweight, header-only helper framework using pure C++
(no dependency to MPI or boost).

In order to use it, C++14 is required. You just need to include a single header:

```cpp
#include <clime.hpp>
```

# Motivation
The library wraps the C++11 thread support functions to clearly separate tasks in an application,
not only regarding the class design but the runtime behaviour. This way no needless blocking of class instances happens that
should work independent from each other (like one uses std::future for a temporary worker).
For example in UI libraries this is a common requirement to avoid blocking the UI.

Generally there is no reason
other than C++ language design why one class usually waits for another class to do its job
(e. g. when calling a function of a member class),
although it could happily take care of other things meanwhile.

In contrast to the robust and easy to use concurrency functions of the go language, which even has [its own statement](https://golang.org/ref/spec#Go_statements) for this scenario, I personally always need to re-think when using C++ concepts like [std::condition_variable](https://en.cppreference.com/w/cpp/thread/condition_variable) and find myself debugging deadlocks. This is why I implemented this small, general-purpose header.

# Using the library
## Basic usage

The library provides a class `clime::message_manager`. There is a method to send messages
```cpp
void send_message(std::shared_ptr<MessageType> msg, unsigned int max_queued_messages=0);
```
and one to receive messages
```cpp
std::shared_ptr<MessageType> receive_message(bool wait_for_message=false);
```

You can use these to exchange messages between several threads. First you need to declare your message classes. Let's declare a very basic message type:

```cpp
struct my_message
{
	int number;
};

```

This message just carries a number. Note that when sending and receiving an instance of a message, it will never be copied, because message instances are encapsulated in a `std::shared_ptr`. So a message class can also include larger things like images. Usually you will have an `enum` in your message class so you can distinguish several messages that are received by the same handler thread.

On the other hand, it is possible to use a basic type like `int` as a message type. But `message_manager` needs to make a difference between message types, so in case you plan to use several message types it makes sense to encapsulate them in a struct, even if they only carry basic types. For logging (see [below](#how-to-log-all-messages)), it makes sense to implement a method `to_string` in each of your message types.

First you need to tell the library about all message types that you will be sending and receiving. You do this by creating an instance of `clime::message_manager`. In this example, you would write:

```cpp
clime::message_manager<my_message> my_message_manager;
```

In case you have several message types, just put them as a comma-separated list as the template arguments of `clime::message_manager` (internally, it is using template [parameter pack expansion](https://en.cppreference.com/w/cpp/language/parameter_pack) to create as many message queues as there are message classes).

Message types are not bound to specific threads. You may send and receive messages in arbitrary threads (even the same one). To send a message in this example, you need to create a message instance and send it:

```cpp
auto msg = std::shared_ptr<my_message>(new my_message{42});
my_message_manager.send_message(msg)
```

To process such a message you can receive it with
```cpp
auto message_for_us = my_message_manager.receive_message<my_message>();
```

Several worker threads may receive the same message - whenever it is received, the message queue will automatically drop it. If the worker thread changes its mind after seeing the message, it may put the message back to the message queue simply by sending it again.

## How to send a delayed message

`message_manager::send_message` offers an overload where you can specify a duration after which the
message will be sent. Of course the delay is done asynchronously, so it will return instantly. For example you can write:

```cpp
auto msg = std::make_shared<my_message>(42);
my_message_manager.send_message(msg, std::chrono::milliseconds(500));
```

## How to avoid exploding message queues

When sending messages, there may be situations where you want to make sure your worker thread does keep up processing the messages you send to avoid that the message queue becomes longer and longer, eventually causing a memory problem. Then you can set the optional argument `unsigned int max_queued_messages` to a reasonable maximum number of messages. If the size of the message queue is `max_queued_messages`, then `message_manager::send_message` will wait (block your thread) until the message queue has become shorter. For example:

```cpp
auto msg = std::make_shared<my_message>(42);
my_message_manager.send_message(msg, 1000);
```

Of course this requires that there actually is a worker thread that consumes messages (particularly when the application shuts down), otherwise `send_message` will wait forever.

## How to wait for a certain message type

Per default, `message_manager::receive_message` will not wait until there is a suitable message (suitable meaning a message of the type that has been specified in the template argument). If there is none, it will return a nullptr, so the calling thread knows it can continue to take care of other things and re-check for messages later. If you want to wait for a message, just write

```cpp
auto message_for_us = my_message_manager.receive_message<my_message>(true);
```

Note that as soon as your instance of `clime::message_manager` is destroyed, `message_manager::receive_message` falls back to the default behaviour and returns a `nullptr` if there is no message in the queue. This way deadlocks on shutdown are prevented.

`my_message_manager::receive_message` has a default parameter `bool wait_for_message=false`.

## How to log all messages

Use method `set_logger` to specify a callback function that will automatically by called on each message that is sent or received, for example:

```cpp
my_message_manager.set_logger<my_message>([](std::shared_ptr<my_message> msg, bool sending)
{
	std::cout << "Message " << msg->number << " was " << (sending ? "sent":"received") << std::endl;
});
```

As you might have an `enum` in your message type, it makes sense to implement a `my_message::to_string` that you can easily call in the logger (to translate enums into strings you may use [magic_enum](https://github.com/Neargye/magic_enum)).

## How to add an asynchronous message handler

### Basics

If you use `clime::receive_message` directly, you have to care about a message handler thread yourself. Usually you will only make use of `clime::receive_message` in the main thread, for example the UI thread. Instead, you can use `message_manager::add_handler` to register your own callback function for a certain message type. For example

```cpp
my_message_manager.add_handler<my_message>([&](std::shared_ptr<my_message> msg)
{
	std::cout << msg->number << std::endl;
});
```

For each call of `add_handler` a corresponding message thread will be started. For load distribution, you can add several handlers for the same message type, which will setup several threads. All threads will be stopped on destruction of `clime::message_handler` (or if its method `dispose()` is called, see [below](#how-to-shutdown)).

### Exception handling

You do not need to add an exception handler to your callback function, because this is provided by the library. To implement special handling of exceptions, add a second argument to `add_handler` with a callback function that accepts `const std::exception&`, for example

```cpp
my_message_manager.add_handler<my_message>([&](std::shared_ptr<my_message> msg)
{
	// your message handler for msg
}), [&](const std::exception& ex)
{
	// your exception handler for ex
});
```

### Handling idle times

Default behaviour is that the thread will wait for messages of the given type. You may want to keep the thread busy with other things when there is no message, for example check for timeouts etc. To do this, just add a third argument with a callback function handling idle times, e. g.

```cpp
my_message_manager.add_handler<my_message>([&](std::shared_ptr<my_message> msg)
{
	// your message handler for msg
}), [&](const std::exception& ex)
{
	// your exception handler for ex
}, [&]()
{
	// your idle handler
});
```

### How to shutdown

Of course the object instances that contain your handlers must have at least the same lifetime as the instance of `clime::message_manager`, otherwise `clime::message_manager` will call methods of destroyed objects. If this is not possible, for example when these objects use the instance of `clime::message_manager` themselves to send messages, you can call `message_manager::dispose()` prior destruction of the instances that contain the handlers, so `message_manager` will stop calling the callback functions that you had registerd with `add_handler`. For example:

```cpp
#include <clime.hpp>
#include "my_worker.hpp"

struct my_message
{
    int number;
};

int main()
{
  clime::message_manager<my_message> my_message_manager;
  my_worker worker(my_message_manager);

  my_message_manager.add_handler<my_message>([&](std::shared_ptr<my_message> msg)
  {
      worker.handle_message(msg);
  });

  std::this_thread::sleep_for(std::chrono::seconds(5));
  my_message_manager.dispose(); // stop calling callbacks, so worker can be safely destroyed
}
```

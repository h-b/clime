# clime
**C**++ **LI**ght **ME**ssage passing library
## Introduction

This platform independent library provides basic helper functions to send messages between std::threads in a C++ application
(no inter process communication). The basic idea is to provide a lightweight, header-only helper framework using pure C++
(no dependency to MPI or boost).

In order to use it, C++14 is required. You just need to include a single header:

```cpp
#include <clime.hpp>
```

## Motivation
The library warps the C++11 thread support functions to clearly separate tasks in an application,
not only regarding the class design but the runtime behaviour. This way no needless blocking of class instances happens that
should work independent from each other (like one uses std::future for a temporary worker). 
For example in UI libraries this is a common requirement to avoid blocking the UI.

Generally there is no reason 
other than C++ language design why one class usually waits for another class to do its job
(e. g. when calling a function of a member class),
although it could happily take care of other things meanwhile. That's why the go programming language
has its [own statement](https://golang.org/ref/spec#Go_statements) for this scenario.

In contrast to the robust and easy to use concurrency functions of the go language, which even has [its own statement](https://golang.org/ref/spec#Go_statements) for this scenario, I personally always need to re-think when using C++ concepts like [std::condition_variable](https://en.cppreference.com/w/cpp/thread/condition_variable) and find myself debugging deadlocks. This is why I implemented this small, general-purpose header.

## Documentation

The library provides a class `message_manager` with two public methods. One to send messages
```cpp
void send_message(std::shared_ptr<MessageType> msg, unsigned int max_queued_messages=0);
```
and one to receive messages
```cpp
std::shared_ptr<MessageType> receive_message(bool wait_for_message=false);
```

You can use these to exchange messages between several threads. First you need to declare your message classes. Let's declare a very basic message class:

```cpp
class my_message
{
public:
	my_message(int number)
		: number_(number)
	{
	}
	
	const int number_;
};
```

This message just carries a number. Note that when sending and receiving an instance of a message, it will never be copied, as message instances are encapsulated in a `std::shared_ptr`. So a message class can also include larger things like images.

First you need to tell the library about all message classes that you will be sending and receiving. You do this by creating an instance of `clime::message_manager`. In this example, you would write:

```cpp
clime::message_manager<my_message> my_message_manager;
```

In case you have several message types, just put them as a comma-separated list as the template arguments of `clime::message_manager` (internally, it is using template [parameter packs](https://en.cppreference.com/w/cpp/language/parameter_pack) to create as many message queues as there are message classes).

Message classses are not bound to specific threads. You may send and receive messages in arbitrary threads (even the same one). To send a message in this example, you need to create a message instance and send it:

```cpp
auto msg = std::make_shared<my_message>(42);
message_manager.send_message(msg)
```

To process such a message you can receive it with
```cpp
auto message_for_us = message_manager.receive_message<my_message>();
```

Several worker threads may receive the same message - whenever it is received, the message queue will automatically drop it. If the worker thread changes its mind after seeing the message, it may put the message back to the message queue simply by sending it again.

Per default, `message_manager::receive_message` will not wait until there is a suitable message (suitable meaning a message of the type that was specific in the template argument). If there is none, it will return a nullptr, so the calling thread knows it can continue to take care of other things and re-check for this message later. If you want to wait for a message, just write

```cpp
auto message_for_us = message_manager.receive_message<my_message>(true);
```

`message_manager::receive_message` has a default parameter `bool wait_for_message=false`.

When sending messages, there may be the (rare) situation that you want to avoid sending an extreme amount of messages that you know that the worker thread will never be able to process. For this situation you can set the optional argument `unsigned int max_queued_messages` to a reasonable maximum number of messages. If the size of the message queue is `max_queued_messages`, then `message_manager::send_message` will wait until the message queue has become shorter.

That's all :-) To dig deeper, please run the example, which calculates prime numbers in an arbitrary number of worker threads.

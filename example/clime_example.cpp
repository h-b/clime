#include "clime.hpp"

#include <cmath>
#include <iostream>
#include <list>
#include <memory>

// message type to asks a prime_checker to calculate if a value is prime. A message type can be a complex class with lots of data, because messages will never be copied
struct message_for_prime_checker
{
    uint64_t number_to_check;
};

// message type to tell prime_printer that a value is prime
struct message_for_prime_printer
{
    uint64_t prime_number;
};

// type of message manager that needs to know all message types
using message_manager_type = clime::message_manager<message_for_prime_checker, message_for_prime_printer>;

// helper function that checks if a number is prime
bool is_prime(uint64_t p);

bool demoFutureTest()
{
    return is_prime(1000000000000873);
}

int main(int argc, char** argv)
{
    if (argc != 4)
    {
        std::cout << "Usage: clime_example <start number> <seconds to run> <number of worker threads>" << std::endl
                  << std::endl;
        std::cout << "This demo calculates prime numbers using worker threads." << std::endl;
        std::cout << "To calculate prime numbers starting from 2 for 2 seconds in 2 threads: clime_example 2 2 2" << std::endl;
        std::cout << "To calculate prime numbers starting from 1 trillion for 1 second in 2 threads: clime_example 1000000000000 1 2" << std::endl;
        return 1;
    }

    // run is_prime of large numbers in parallel to the main example below. We show 7 different uses.
    clime::future<bool> isPrime1 = demoFutureTest;
    clime::future<bool> isPrime2(demoFutureTest);
    clime::future<bool> isPrime3 = [=]()
    {
        return is_prime(1000000000000873);
    };
    clime::future<bool> isPrime4([=]()
                                 { return is_prime(1000000000000873); });
    clime::future<bool> isPrime5;
    isPrime5 = demoFutureTest;
    clime::future<bool> futureWithoutFunction;
    clime::future<bool> changeFutureResult;
    changeFutureResult = demoFutureTest;
    changeFutureResult = false;

    const uint64_t start_prime = std::atoll(argv[1]);
    const int      time_limit  = std::atoi(argv[2]);
    const int      n_threads   = std::atoi(argv[3]);

    {
        message_manager_type message_manager;

        // just a sample how logging is possible - We do not really log in this sample.
        message_manager.set_logger<message_for_prime_checker>([](const std::shared_ptr<message_for_prime_checker>& msg, bool sending)
                                                              {
                                                                  // Each and every message_for_prime_checker that is sent will arrive here, so this can be used for easy logging of one or all message types.
                                                                  // bool 'sending' will be true for message that are sent and false for messages that are received
                                                              });

        // start n_threads prime checker threads that handle message type message_for_prime_checker
        for (int i = 0; i < n_threads; i++)
        {
            message_manager.add_handler<message_for_prime_checker>([&](const std::shared_ptr<message_for_prime_checker>& msg) // clang-format off
            {
				if (is_prime(msg->number_to_check))
				{
					// We found that the number is prime, so send a message back to the printer.
					auto msg_to_printer = std::make_shared<message_for_prime_printer>(message_for_prime_printer{ msg->number_to_check });
					message_manager.send_message(msg_to_printer);
				}
            }); //clang-format on
        }

        uint64_t p = start_prime + (start_prime % 2 == 0); // make it odd, as we iterate with step size 2

        std::cout << "Calculating prime numbers in " << n_threads << " thread(s) for " << time_limit << " seconds, starting from " << p << "..." << std::endl;

        // start prime printer thread that handles message type message_for_prime_printer and sends requests (i.e. message_for_prime_checker) to prime checker threads
        message_manager.add_handler<message_for_prime_printer>([](const std::shared_ptr<message_for_prime_printer>& msg)
                                                               { std::cout << msg->prime_number << " "; },
                                                               nullptr, // we do not provide an exception handler in this sample
                                                               [&]()    // handler for no message from prime checker: we send requests to the prime checker
                                                               {
                                                                   auto msg_to_checker = std::make_shared<message_for_prime_checker>(message_for_prime_checker{p});
                                                                   message_manager.send_message(msg_to_checker, 100); // in case prime checker's message queue has reached 100 messages, wait until it processed one
                                                                   p += 2;                                            // proceed to the next odd number to check if it is prime
                                                               });

        std::this_thread::sleep_for(std::chrono::seconds(time_limit));
    }

    std::cout << std::endl
              << "In addition, we calculated other prime numbers in parallel. Results were: "
              << (isPrime1 ? "yes" : "no") << ", "
              << (isPrime2 ? "yes" : "no") << ", "
              << (isPrime3 ? "yes" : "no") << ", "
              << (isPrime4 ? "yes" : "no") << ", "
              << (isPrime5 ? "yes" : "no") << ", "
              << (futureWithoutFunction ? "yes" : "no") << ", "
              << (changeFutureResult ? "yes" : "no") << std::endl;

    return 0;
}

bool is_prime(uint64_t p)
{
    if (p % 2 == 0) return false; // all prime numbers are odd

    const uint64_t stop = static_cast<uint64_t>(std::sqrt(p));

    for (uint64_t i = 3; i <= stop; i += 2)
    {
        if (p % i == 0) return false;
    }

    return true;
}

#include "clime.hpp"
#include <iostream>
#include <list>
#include <functional>
#include <cmath>

// message type to asks a prime_checker to calculate if a value is prime
class message_for_prime_checker
{
public:
	explicit message_for_prime_checker(uint64_t number)
		: number_(number)
	{
	}

	uint64_t get_number() const
	{
		return number_;
	}

private:
	const uint64_t number_;
};

// message type to tell prime_printer that a value is prime
class message_for_prime_printer
{
public:
	explicit message_for_prime_printer(uint64_t prime_number)
		: prime_number_(prime_number)
	{
	}
	
	uint64_t get_prime() const
	{
		return prime_number_;
	}
	
private:
	const uint64_t prime_number_;
};

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

using message_manager_type = clime::message_manager<message_for_prime_checker, message_for_prime_printer>;

int main(int argc, char**argv)
{
	if (argc != 4)
	{
		std::cout << "Usage: clime_example <start number> <seconds to run> <number of worker threads>" << std::endl << std::endl;
		std::cout << "This demo calculates prime numbers using worker threads." << std::endl;
		std::cout << "To calculate prime numbers starting from 2 for 2 seconds in 2 threads: clime_example 2 2 2" << std::endl;
		std::cout << "To calculate prime numbers starting from 1 trillion for 1 second in 2 threads: clime_example 1000000000000 1 2" << std::endl;
		return 1;
	}
	
	const uint64_t start_prime = std::atoll(argv[1]);
	const int       time_limit = std::atoi(argv[2]);
	const int        n_threads = std::atoi(argv[3]);
	
	message_manager_type message_manager;

	// just a sample how logging is possible - We do not really log in this sample.
	message_manager.set_logger<message_for_prime_checker>([](std::shared_ptr<message_for_prime_checker> msg, bool sending)
	{
		// Each and every message_for_prime_checker that is sent will arrive here, so this can be used for easy logging of one or all message types.
	});

	uint64_t p = start_prime + (start_prime%2==0); // make it odd, as we iterate with step size 2

	std::list<std::shared_ptr<clime::message_handler<message_for_prime_checker, message_manager_type>>> prime_checker_handlers;
	
	clime::message_handler<message_for_prime_printer, message_manager_type> handle_msg_for_prime_printer(message_manager, [](const message_for_prime_printer& msg)
	{
		std::cout << msg.get_prime() << " ";
	}, [&]() // handler for no message from prime checker: we send requests to the prime checker
	{
		auto msg = std::make_shared<message_for_prime_checker>(p);
		message_manager.send_message(msg, 100); // in case prime checker's message queue has reached 100 messages, wait until it processed one
		p += 2; // proceed to the next odd number to check if it is prime
	});

	for (int i = 0; i < n_threads; i++)
	{
		prime_checker_handlers.emplace_back(std::make_shared<clime::message_handler<message_for_prime_checker, message_manager_type>>(message_manager, [&](const message_for_prime_checker& msg)
		{
			const uint64_t number_to_check = msg.get_number();

			if (is_prime(number_to_check))
			{
				// We found that the number is prime, so send a message back to the printer.
				auto msg = std::make_shared<message_for_prime_printer>(number_to_check);
				message_manager.send_message(msg);
			}
		}));
	}

	std::this_thread::sleep_for(std::chrono::seconds(time_limit));

	return 0;
}

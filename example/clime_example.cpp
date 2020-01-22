#include "clime.hpp"
#include <thread>
#include <atomic>
#include <memory>
#include <iostream>
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

	uint64_t get_number()
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
	
	uint64_t get_prime()
	{
		return prime_number_;
	}
	
private:
	const uint64_t prime_number_;
};

using message_manager_type = clime::message_manager<message_for_prime_checker, message_for_prime_printer>;

// This class has two tasks:
// (1) Sending potential prime numbers to one of the checker threads.
// (2) Receiving messages from checker threads carrying confirmed primes, and printing them.
class prime_printer
{
public:
	explicit prime_printer(message_manager_type& message_manager)
		: message_manager_(message_manager)
	{
	}
	
	void run(uint64_t start_prime)
	{
		// max_queued_messages avoids that this thread needlessly steals CPU time by sending messages that will never be received.
		// This increases performance a bit, but requires correct order of shutdown of the threads - first this thread, then the checkers (workers).
		// See note at the end of this file.
		const unsigned int max_queued_messages = 100;
		uint64_t p=start_prime;
		
		if (p%2==0) ++p; // make it odd, as we do not check even numbers if they are prime
		
		while (running)
		{
			// Create message to checker thread carrying the current potential prime numbe p. Then send it.
			auto msg = std::make_shared<message_for_prime_checker>(p);
			message_manager_.send_message(msg, max_queued_messages); // in case the message queue has grown to max_queued_messages, send_message will block until queue is shorter
			
			// Try to get a message from a checker thread that would confirm a number to be prime that we sent in the past.
			// If there is no such message, it might be that no checker thread has finished yet, or we already processed all messaegs from checker threads.
			auto message_for_us = message_manager_.receive_message<message_for_prime_printer>(false);
			if (message_for_us)
			{
				std::cout << message_for_us->get_prime() << " ";
			}

			p += 2;
		}
	}

	std::atomic<bool> running{true};
	
private:
	message_manager_type& message_manager_;
};

// This class checks if numbers are prime and, if so, sends a message to the printer thread so it will printed.
// The number we need to check arrives here as a message from another thread (the printer, which has two tasks: asking for checks and printing).
class prime_checker
{
public:
	explicit prime_checker(message_manager_type& message_manager)
		: message_manager_(message_manager)
	{
	}
	
	void run()
	{
		while (running)
		{
			auto message_for_us = message_manager_.receive_message<message_for_prime_checker>(true);
			if (message_for_us)
			{
				const uint64_t number_to_check = message_for_us->get_number();
				
				if (is_prime(number_to_check))
				{
					// We found that the number is prime, so send a message back to the printer.
					auto msg = std::make_shared<message_for_prime_printer>(number_to_check);
					message_manager_.send_message(msg);
				}
			}
		}
	}

	std::atomic<bool> running{true};
	
private:
	message_manager_type& message_manager_;
	
	static bool is_prime(uint64_t p)
	{
		if (p % 2 == 0) return false; // all prime numbers are odd

		const uint64_t stop = static_cast<uint64_t>(std::sqrt(p));

		for (uint64_t i = 3; i <= stop; i += 2)
		{
			if (p % i == 0) return false;
		}

		return true;
	}
};


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
	
	std::vector<std::shared_ptr<prime_checker>> prime_checkers;
	std::vector<std::shared_ptr<std::thread>> prime_checker_threads;
	
	for (int i = 0; i < n_threads; ++i)
	{
		prime_checkers.emplace_back(std::make_shared<prime_checker>(message_manager));
		prime_checker_threads.emplace_back(std::make_shared<std::thread>([&]{prime_checkers.back()->run();}));
	}
	
	prime_printer printer(message_manager);
	std::thread prime_printer_thread = std::thread([&]{printer.run(start_prime);});

	std::this_thread::sleep_for(std::chrono::seconds(time_limit));

	// Stop threads - first the printer, because it may be waiting for checkers to consume messages.
	// If we would stop the worker threads first, the printer might be waiting indefinitely that the message queue becomes shorter.
	printer.running = false;
	prime_printer_thread.join();

	for (int i = 0; i < n_threads; ++i) prime_checkers[i]->running = false;
	for (int i = 0; i < n_threads; ++i) prime_checker_threads[i]->join();

	return 0;
}

#include "clime.hpp"
#include <thread>
#include <atomic>
#include <memory>
#include <iostream>
#include <functional>
#include <cmath>

class message_for_prime_printer // tells prime_printer that value is prime
{
public:
	message_for_prime_printer(uint64_t prime_number)
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

class message_for_prime_checker // asks a prime_checker to calculate if value is prime
{
public:
	message_for_prime_checker(uint64_t number)
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

using message_manager_type = clime::message_manager<message_for_prime_printer, message_for_prime_checker>;

class prime_printer
{
public:
	prime_printer(std::shared_ptr<message_manager_type> message_manager)
		: message_manager_(message_manager)
	{
	}
	
	void run(uint64_t start_prime)
	{
		const unsigned int max_queued_messages = 100; // avoid that this thread needlessly steals CPU time by sending messages that will never be received
		uint64_t p=start_prime;
		
		if (p%2==0) ++p; // make it odd, as we do not check even numbers if they are prime
		
		while (running)
		{
			auto msg = std::make_shared<message_for_prime_checker>(p);
			message_manager_->send_message(msg, max_queued_messages); // in case the message queue has grown to max_queued_messages, send_message will block until queue is shorter
			
			auto message_for_us = message_manager_->receive_message<message_for_prime_printer>(false); // we do not wait for a message from prime_checker, because it may have processed all our messages, so we need to continue sending messages to prime_checker
			if (message_for_us)
			{
				std::cout << message_for_us->get_prime() << " ";
			}

			p += 2;
		}
	}

	std::atomic<bool> running{true};
	
private:
	std::shared_ptr<message_manager_type> message_manager_;
};

class prime_checker
{
public:
	prime_checker(std::shared_ptr<message_manager_type> message_manager)
		: message_manager_(message_manager)
	{
	}
	
	void run()
	{
		while (running)
		{
			auto message_for_us = message_manager_->receive_message<message_for_prime_checker>(true);
			if (message_for_us)
			{
				const uint64_t number_to_check = message_for_us->get_number();
				
				if (is_prime(number_to_check))
				{
					auto msg = std::make_shared<message_for_prime_printer>(number_to_check);
					message_manager_->send_message(msg);
				}
			}
		}
	}

	std::atomic<bool> running{true};
	
private:
	std::shared_ptr<message_manager_type> message_manager_;
	
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
	
	auto message_manager = std::make_shared<message_manager_type>();
	
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

	// stop threads
	for (int i = 0; i < n_threads; ++i) prime_checkers[i]->running = false;
	for (int i = 0; i < n_threads; ++i) prime_checker_threads[i]->join();
	printer.running = false;
	prime_printer_thread.join();

	return 0;
}

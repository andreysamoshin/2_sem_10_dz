#define BOOST_DATE_TIME_NO_LIB

#include <iostream>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>

#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/containers/map.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/interprocess_condition.hpp>

using namespace boost::interprocess;

struct shared_memory_guard
{
	explicit shared_memory_guard(std::string name, int * ref_count_ptr)
		:	m_name(name), m_ref_count_ptr(ref_count_ptr)
	{
        ++(*m_ref_count_ptr);
	}

	~shared_memory_guard()
	{
        --(*m_ref_count_ptr);
        if (*m_ref_count_ptr <= 0)
        {
            shared_memory_object::remove(m_name.c_str());
        }
	}

private:
    int * m_ref_count_ptr;
	std::string m_name;
};

template <typename ID, typename String, typename Container, typename Mutex, typename Cond_var>
void send(bool & should_exit, ID my_id, ID * message_id, ID * last_speak, String * message, Container * container, Mutex * mutex, Cond_var * condition)
{
	std::string raw_message;

	{
		std::unique_lock < Mutex > lock (*mutex);
		* last_speak = my_id;
		* message = "User " + std::to_string (my_id) + " joined the chat";
		container->insert(std::pair(++(*message_id), *message));
		condition->notify_all();
	}

    while (true)
	{
		std::getline(std::cin, raw_message, '\n');

		std::unique_lock < Mutex > lock (*mutex);

		*last_speak = my_id;

		if (raw_message == "exit")
		{
			should_exit = true;
			*message = "User " + std::to_string (my_id) + ": left the chat";
			container->insert(std::pair (++(*message_id), *message));
			condition->notify_all();
			return;
		}

		*message = "User " + std::to_string (my_id) + ": " + raw_message;
		container->insert(std::pair (++(*message_id), *message));
		condition->notify_all();
	}
}

template <typename ID, typename Container, typename Mutex, typename Cond_var>
void recieve(bool & should_exit, ID my_id, ID * message_id, ID * last_speak, Container * container, Mutex * mutex, Cond_var * condition)
{
    while (!should_exit)
	{
		std::unique_lock < Mutex > lock (*mutex);

		condition->wait(lock);

		if (* last_speak != my_id)
		{
			std::cout << container->find(*message_id)->second << std::endl;
		}
	}
}

int main(int argc, char** argv)
{
	using id_t = int;

    const std::string shared_memory_name = "managed_shared_memory";
    managed_shared_memory sm(open_or_create, shared_memory_name.c_str(), 65000);
    
    auto ref_count_ptr      = sm.find_or_construct < id_t > ("ref_count")(0);
	auto current_process_id = sm.find_or_construct < id_t > ("current_process_id")(0);
	auto last_speak        = sm.find_or_construct < id_t > ("last_speak")(0);
	auto message_id        = sm.find_or_construct < id_t > ("message_id")(0);
	bool should_exit       = false;

	int my_id = (*current_process_id)++;

	shared_memory_guard shm_guard (shared_memory_name, ref_count_ptr);

	using segment_manager_t = managed_shared_memory::segment_manager;
	using char_allocator_t  = allocator <char, segment_manager_t>;
	using string_t          = basic_string <char, std::char_traits <char>, char_allocator_t>;
	using key_t             = id_t;
	using value_t           = std::pair < const key_t, string_t>;
	using value_allocator_t = allocator <value_t, segment_manager_t>;
	using map_t             = boost::interprocess::map <key_t, string_t, std::less <key_t>, value_allocator_t>;

	char_allocator_t char_allocator (sm.get_segment_manager());
	value_allocator_t value_allocator (sm.get_segment_manager());

	auto map = sm.find_or_construct < map_t >("map")(std::less <key_t> (), value_allocator);
	auto mutex = sm.find_or_construct < interprocess_mutex >("mutex")();
	auto message = sm.find_or_construct < string_t >("message")(char_allocator);
	auto condition  = sm.find_or_construct < interprocess_condition >("condition")();

	for (auto & value : *map)
	{
		std::cout << value.second << std::endl;
	}

    std::thread send_thread (send <id_t, string_t, map_t, interprocess_mutex, interprocess_condition>, 
		std::ref(should_exit), my_id, std::ref(message_id), std::ref(last_speak), std::ref(message),
		std::ref(map), std::ref(mutex), std::ref(condition));

    std::thread recieve_thread (recieve <id_t, map_t, interprocess_mutex, interprocess_condition>, 
		std::ref(should_exit), my_id, std::ref(message_id), std::ref(last_speak),
		std::ref(map), std::ref(mutex), std::ref(condition));

	send_thread.join();
	recieve_thread.join();

	return EXIT_SUCCESS;
}
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/interprocess_condition.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>

class Chat 
{
private:

    using shared_memory_t = boost::interprocess::managed_shared_memory;
    using manager_t = shared_memory_t::segment_manager;
    using string_allocator_t = boost::interprocess::allocator < char, manager_t >;
    using string_t = boost::interprocess::basic_string < char, std::char_traits < char >, string_allocator_t >;
    using vector_allocator_t = boost::interprocess::allocator < string_t, manager_t >;
    using vector_t = boost::interprocess::vector < string_t, vector_allocator_t >;
    using mutex_t = boost::interprocess::interprocess_mutex;
    using condition_t = boost::interprocess::interprocess_condition;
    using counter_t = std::atomic < std::size_t >;

public:

    explicit Chat(const std::string & user_name) : m_user_name(user_name), m_exit_flag(false),
        m_shared_memory(boost::interprocess::open_or_create, shared_memory_name.c_str(), 1024)
    {
        m_vector    = m_shared_memory.find_or_construct < vector_t > ("Vector")(m_shared_memory.get_segment_manager());
        m_mutex     = m_shared_memory.find_or_construct < mutex_t > ("Mutex")();
        m_condition = m_shared_memory.find_or_construct < condition_t > ("Cond_var")();
        m_users     = m_shared_memory.find_or_construct < counter_t > ("User_counter")(0);
        m_messages  = m_shared_memory.find_or_construct < counter_t > ("Messages_counter")(0);

        (*m_users)++;

        m_local_messages = 0;
    }

    ~Chat() noexcept
    {
        if (!(--(*m_users)))
        {
            boost::interprocess::shared_memory_object::remove(shared_memory_name.c_str());
        }
    }

public:

    void run() 
    {
        auto reader = std::thread(&Chat::read, this); // поток для чтения на условной переменной

        write();

        m_exit_flag = true;

        m_condition->notify_all();

        reader.join();

        send_message(m_user_name + " left the chat");
    }

private:

    void read()
    {
        show_history();

        send_message(m_user_name + " joined the chat");

        while (true)
        {
            std::unique_lock lock(*m_mutex);

            m_condition->wait(lock, [this]() { return m_local_messages < *m_messages || m_exit_flag; });

            if (m_exit_flag)
            {
                break;
            }

            std::cout << m_vector->back() << std::endl;

            m_local_messages ++;
        }
    }

    void show_history()
    {
        std::scoped_lock lock(*m_mutex);

        for (const auto & message : *m_vector)
        {
            std::cout << message << std::endl;

            m_local_messages++;
        }
    }

    void send_message(const std::string & message) 
    {
        std::scoped_lock lock(*m_mutex);

        m_vector->push_back(string_t(message.c_str(), m_shared_memory.get_segment_manager()));

        (*m_messages)++;

        m_local_messages++;

        m_condition->notify_all();
    }

    void write() 
    {

        for(std::string s; std::getline(std::cin, s); send_message(m_user_name + " : " + s))
        {
            if(s == "exit")
            {
                break;
            }

        }
    }

private:

    static inline const std::string shared_memory_name = "shared_memory";

private:

    std::string m_user_name;

    std::atomic < bool > m_exit_flag;

    shared_memory_t m_shared_memory;

    vector_t    * m_vector;
    mutex_t     * m_mutex;
    condition_t * m_condition;
    counter_t   * m_users;
    counter_t   * m_messages;

    std::size_t m_local_messages;
};

int main(int argc, char ** argv) 
{
    std::string user_name;

    std::cout << "Enter your name: ";

    std::getline(std::cin, user_name);

    Chat(user_name).run();

    system("pause");

    return EXIT_SUCCESS;
}
#ifndef KVIMAGEFETCH_H
#define KVIMAGEFETCH_H

/**
 * this code is responsible for fetching and deserializing
 * image data quickly and at high volume. it's split between
 * the fetcher (this file) and a task (KVImageTask.hpp). this
 * code is both thread and memory safe, as anything it allocates
 * is wrapped in a smart pointer.
 *
 * this code replaces NSCache as well as the built in networking of both objective-C and
 * Swift. it also uses its own thread, so no need to use the async/concurrency features of
 * either of those langauges.
 */

#include "KVImageTask.hpp"
#include <shared_mutex>
#include <thread>

namespace kv {

/**
 * data_ptr is expecting a pointer type, like `kv::TwitchCategory *`
 */
template <typename data_ptr>
class KVImageFetch {
public:
    using task_t        = KVTask<data_ptr>;
    using container_t   = typename task_t::container_t;
    using task_vector_t = std::vector<typename KVTask<data_ptr>::ptr>;

    inline KVImageFetch() {}

    /**
     * start the thread. this isn't necessary if the use case is just one off, individual
     * requests. if you start the thread, however, then it continuously polls for new
     * work. this can be used if it's being shared between modules.
     */
    inline void start() {
        std::unique_lock lock(this->_mutex);
        if (this->_ioc.stopped()) {
            this->_ioc.restart();
        }
        if (!this->_thread.joinable()) {
            this->_start_thread();
        }
    }

    /**
     * all work is automatically stoped and deallocated when
     * this object goes out of scope.
     */
    inline ~KVImageFetch() {
        this->stop();
    }

    inline void clear() const {
        this->_map.clear();
    }

    /**
     * it's recommended to call this function in the completion
     * handler that you pass to `fetch_image()`.
     *
     * since the image data is meant to be owned by the
     * caller, there's no reason for the map to hold onto
     * the entry.
     *
     * items are only removed if each task associated to it is
     * complete, so syncronization isn't the responsibility of
     * the caller.
     *
     * the tasks will update their status when they are
     * complete.
     */
    inline bool remove_item(data_ptr key) {
        std::unique_lock lock(this->_mutex);
        bool ret = false;
        auto got = this->_map.find(key);
        if (got != this->_map.end()) {
            auto &vec            = got->second;
            int completion_count = 0;
            for (auto &task : vec) {
                if (task->finished()) {
                    completion_count++;
                }
            }
            if (completion_count == vec.size()) {
                this->_map.erase(key);
                ret = true;
            }
        }
        return ret;
    }

    /**
     * asyncronously fetch and deserialize image data.
     * the data_ptr passed is used directly as the key to
     * the map entry.
     *
     * the url is expected to exist inside the `key`. this is
     * important because it's used to check if the task already
     * exists. one key can have multiple tasks, and rather than
     * a string comparison, the raw memory of the string is used
     * to check if a task already exists for the URL.
     */
    inline void fetch_image(
        data_ptr key,
        std::string_view url,
        callback_t<data_ptr> &&callback
    ) {
        std::unique_lock lock(this->_mutex);
        auto got = this->_map.find(key);
        if (got == this->_map.end()) {
            auto task = this->_create_task(key, url, std::move(callback));
            task->start_work();
            this->_map.insert({key, task_vector_t{std::move(task)}});
        } else {
            int found     = false;
            int found_ind = 0;
            auto &vec     = got->second;
            for (auto &task : vec) {
                auto &container = task->container();
                if (url.data() == container.url.data()) {
                    found = true;
                    break;
                }
                found_ind++;
            }
            if (!found) {
                auto task = this->_create_task(key, url, std::move(callback));
                task->start_work();
                got->second.emplace_back(std::move(task));
            } else {
                auto task = got->second[found_ind];
                task->invoke();
            }
        }
    }

    inline void stop() {
        std::unique_lock lock(this->_mutex);
        this->_ioc.stop();
        if (this->_thread.joinable()) {
            this->_thread.join();
        }
    }

    inline bool stopped() const {
        return this->_ioc.stopped();
    }

private:
    /**
     * the map stores the key as `void *` with the value being
     * a vector of `KVTask`. the task is what is responsible for
     * fetching and deserializing.
     */
    using map_type_t = std::unordered_map<void *, task_vector_t>;

    inline typename task_t::ptr _create_task(
        data_ptr key,
        std::string_view url,
        callback_t<data_ptr> &&callback
    ) {
        container_t container{key, url, std::move(callback)};
        return task_t::get_shared(this->_ioc, this->_thread, std::move(container));
    }

    inline void _start_thread() {
        this->_thread = std::thread([this] {
            asio::executor_work_guard<asio::io_context::executor_type> guard(
                this->_ioc.get_executor()
            );
            this->_ioc.run();
        });
    }

    asio::io_context _ioc;
    map_type_t _map;
    std::thread _thread;
    std::shared_mutex _mutex;
};

} // namespace kv

#endif /* KVIMAGEFETCH_H */

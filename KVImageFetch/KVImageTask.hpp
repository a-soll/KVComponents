#ifndef KVIMAGETASK_H
#define KVIMAGETASK_H

#include <CoreImage/CoreImage.h>
#include <ImageIO/ImageIO.h>
/**
 * this module uses Kulve's in-house async client.
 */
#include <kulve/http_client.h>
#include <thread>

namespace kv {

template <typename data_ptr>
using callback_t = std::function<void(data_ptr, CGImageRef)>;

/**
 * simple struct to package up all the data related to the
 * task.
 */
template <typename data_ptr>
struct data_container {
    data_ptr key;
    std::string_view url;
    callback_t<data_ptr> callback;

    inline data_container(
        data_ptr key,
        std::string_view url,
        callback_t<data_ptr> &&callback
    )
        : key(key), url(url), callback(std::move(callback)) {}

    data_container(const data_container<data_ptr> &) = delete;

    inline data_container(data_container &&rhs) noexcept
        : key(std::move(rhs.key)), url(rhs.url), callback(std::move(rhs.callback)) {}
};

template <typename data_ptr>
class KVTask : public std::enable_shared_from_this<KVTask<data_ptr>> {
public:
    using ptr         = std::shared_ptr<KVTask<data_ptr>>;
    using container_t = data_container<data_ptr>;

    /**
     * constructor is private, so this is the only way to
     * create one. this object is only to be used as a
     * `shared_ptr`.
     */
    inline static ptr get_shared(
        asio::io_context &ioc,
        std::thread &thread,
        container_t &&container
    ) {
        return std::shared_ptr<KVTask>(
            new KVTask<data_ptr>(ioc, thread, std::move(container))
        );
    }

    /**
     * asyncronously fetch the raw image data which gets passed
     * to `_deserialize_image()`.
     */
    inline void start_work() {
        std::weak_ptr<KVTask> weak_self = this->shared_from_this();
        this->_client                   = http::make_client(this->_ioc);
        this->_client->prepare_get(
            this->_container.url,
            [weak_self](std::string_view response, int status_code) {
                if (auto self = weak_self.lock()) {
                    auto image      = self->_deserialize_image(response);
                    self->_finished = true;
                    self->_container.callback(self->_container.key, image);
                }
            }
        );
        this->_client->run();
    }

    /**
     * used to manually trigger the callback if needed.
     */
    inline const void invoke() const {
        this->_container.callback(this->_container.key, nullptr);
    }

    inline bool finished() const {
        return this->_finished;
    }

    inline const container_t &container() const {
        return this->_container;
    }

private:
    inline KVTask<data_ptr>(
        asio::io_context &ioc,
        std::thread &thread,
        container_t &&container
    )
        : _ioc(ioc), _thread(thread), _container(std::move(container)) {}

    /**
     * creates a CGImage that the UI can use to create a
     * UIImage via `[UIImage imageWithCGImage:CGImage]`
     */
    inline CGImageRef _deserialize_image(std::string_view data) {
        CGImageRef image = nullptr;
        CFDataRef d      = CFDataCreate(
            kCFAllocatorDefault,
            reinterpret_cast<const uint8_t *>(data.data()),
            static_cast<CFIndex>(data.size())
        );

        CGImageSourceRef source = CGImageSourceCreateWithData(d, nullptr);
        if (source) {
            image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
            CFRelease(source);
        }
        CFRelease(d);
        return image;
    }

    asio::io_context &_ioc;
    std::thread &_thread;
    http::ptr _client;
    container_t _container;
    bool _finished = false;
};

} // namespace kv

#endif /* KVIMAGETASK_H */

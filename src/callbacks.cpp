#include <iostream>
#include <opentok.h>

/*
This file contains basic, boring, lifecycle reporting callbacks
*/

void default_otc_logger_callback(const char *message) {
    std::cout << message << std::endl;
}

struct otc_audio_device_callbacks default_device_callbacks = {
    // clang-format off
    .init_renderer = [](const otc_audio_device *audio_device, void *user_data) -> otc_bool {
        std::cout << "[audio_device.init_renderer]" << std::endl;
        return OTC_TRUE;
    },
    .destroy_renderer = [](const otc_audio_device *audio_device, void *user_data) -> otc_bool {
        std::cout << "[audio_device.destroy_renderer]" << std::endl;
        return OTC_TRUE;
    },
    .start_renderer = [](const otc_audio_device *audio_device, void *user_data) -> otc_bool {
        std::cout << "[audio_device.start_renderer]" << std::endl;
        return OTC_TRUE;
    },
    .stop_renderer = [](const otc_audio_device *audio_device, void *user_data) -> otc_bool {
        std::cout << "[audio_device.stop_renderer]" << std::endl;
        return OTC_TRUE;
    },
    /*
    .is_renderer_initialized = [](const otc_audio_device *audio_device, void *user_data) -> otc_bool {
        std::cout << "[audio_device.is_renderer_initialized]" << std::endl;
        return OTC_TRUE;
    },
    .is_renderer_started = [](const otc_audio_device *audio_device, void *user_data) -> otc_bool {
        std::cout << "[audio_device.is_renderer_started]" << std::endl;
        return OTC_TRUE;
    },
    */
    .get_render_settings = [](const otc_audio_device *audio_device,
                              void *user_data,
                              struct otc_audio_device_settings *settings) -> otc_bool {
        std::cout << "[audio_device.get_render_settings]" << std::endl;
        return OTC_TRUE;
    },
    // clang-format on
};

struct otc_session_callbacks default_session_callbacks = {
    // clang-format off
    .on_connected = [](otc_session *session, void *user_data) {
        std::cout << "[session.on_connected]" << std::endl;
    },
    .on_disconnected = [](otc_session *session, void *user_data) {
        std::cout << "[session.on_disconnected]" << std::endl;
    },
    .on_connection_created = [](otc_session *session, void *user_data, const otc_connection *connection) {
        std::cout << "[session.on_connection_created]" << std::endl;
    },
    .on_connection_dropped = [](otc_session *session, void *user_data, const otc_connection *connection) {
        std::cout << "[session.on_connection_dropped]" << std::endl;
    },
    .on_stream_received = [](otc_session *session, void *user_data, const otc_stream *stream) {
        const char *stream_id = otc_stream_get_id(stream);
        std::cout << "[session.on_stream_received]" << stream_id << std::endl;
    },
    .on_stream_dropped = [](otc_session *session, void *user_data, const otc_stream *stream) {
        const char *stream_id = otc_stream_get_id(stream);
        std::cout << "[session.on_stream_dropped] " << stream_id << std::endl;
    },
    .on_error = [](otc_session *session, void *user_data, const char *error_string, enum otc_session_error_code error) {
        std::cerr << "[session.on_error] " << error_string << std::endl;
    },
    // clang-format on
};

struct otc_subscriber_callbacks default_subscriber_callbacks = {
    // clang-format off
    .on_connected = [](otc_subscriber *subscriber, void *user_data, const otc_stream *stream) {
        const char *stream_id = otc_stream_get_id(stream);
        std::cout << "[subscriber.on_connected] " << stream_id << std::endl;
    },
    .on_disconnected = [](otc_subscriber *subscriber, void *user_data) {
        otc_stream *stream = otc_subscriber_get_stream(subscriber);
        const char *stream_id = otc_stream_get_id(stream);
        std::cout << "[subscriber.on_disconnected] " << stream_id << std::endl;
    },
    .on_reconnected = [](otc_subscriber* subscriber, void* user_data) {
        otc_stream *stream = otc_subscriber_get_stream(subscriber);
        const char *stream_id = otc_stream_get_id(stream);
        std::cout << "[subscriber.on_reconnected] " << stream_id << std::endl;
    },
    .on_error = [](otc_subscriber *subscriber, void *user_data, const char *error_string, enum otc_subscriber_error_code error) {
        std::cerr << "[subscriber.on_error] " << error_string << std::endl;
    },
    // clang-format on
};

struct otc_publisher_callbacks default_publisher_callbacks = {
    // clang-format off
    .on_stream_created = [](otc_publisher* publisher, void* user_data,const otc_stream* stream) {
        const char *stream_id = otc_stream_get_id(stream);
        std::cout << "[publisher.on_stream_connected] " << stream_id << std::endl;
    },
    .on_stream_destroyed = [](otc_publisher* publisher, void* user_data,const otc_stream* stream) {
        const char *stream_id = otc_stream_get_id(stream);
        std::cout << "[publisher.on_stream_destroyed] " << stream_id << std::endl;
    },
    .on_error = [](otc_publisher *publisher, void *user_data, const char *error_string, enum otc_publisher_error_code error_code) {
        std::cout << __FUNCTION__ << " callback function" << std::endl;
        std::cout << "Publisher error. Error code: " << error_string << std::endl;
    },
    // clang-format on
};

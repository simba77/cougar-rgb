#include "sink.hpp"

#include <chrono>
#include <pulse/pulseaudio.h>

namespace cougar {

namespace {

struct Query {
    std::optional<SinkInfo> result;
    bool done = false;
};

void on_sink_info(pa_context *, const pa_sink_info *info, int eol, void *userdata)
{
    auto *q = static_cast<Query *>(userdata);
    if (eol) {
        q->done = true;
        return;
    }
    if (info && info->description && q->result)
        q->result->description = info->description;
}

void on_server_info(pa_context *c, const pa_server_info *info, void *userdata)
{
    auto *q = static_cast<Query *>(userdata);
    if (!info || !info->default_sink_name) {
        q->done = true;
        return;
    }
    q->result = SinkInfo{info->default_sink_name, info->default_sink_name};
    pa_operation_unref(pa_context_get_sink_info_by_name(c, info->default_sink_name, on_sink_info, q));
}

void on_state(pa_context *c, void *userdata)
{
    auto *q = static_cast<Query *>(userdata);
    switch (pa_context_get_state(c)) {
    case PA_CONTEXT_READY:
        pa_operation_unref(pa_context_get_server_info(c, on_server_info, q));
        break;
    case PA_CONTEXT_FAILED:
    case PA_CONTEXT_TERMINATED:
        q->done = true;
        break;
    default:
        break;
    }
}

}  // namespace

std::optional<SinkInfo> default_sink()
{
    Query q;
    pa_mainloop *loop = pa_mainloop_new();
    pa_context *context = pa_context_new(pa_mainloop_get_api(loop), "cougar-rgb");
    pa_context_set_state_callback(context, on_state, &q);
    if (pa_context_connect(context, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) >= 0) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!q.done && std::chrono::steady_clock::now() < deadline) {
            if (pa_mainloop_prepare(loop, 50'000) < 0 || pa_mainloop_poll(loop) < 0 ||
                pa_mainloop_dispatch(loop) < 0)
                break;
        }
    }
    pa_context_disconnect(context);
    pa_context_unref(context);
    pa_mainloop_free(loop);
    return q.done ? q.result : std::nullopt;
}

}  // namespace cougar

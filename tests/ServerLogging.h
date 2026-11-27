#pragma once
#include "Infrastructure/Logging/Logging.h"
#include <QHttpServerRequest>
#include <QHttpServerResponse>
namespace Infrastructure::Logging {
// Wrap handlers so every early return (including authentication/validation errors)
// records the actual response status, without recording headers or bodies.
template<class Handler>
auto tracedRoute(const char* route, Handler handler) {
    return [route, handler = std::move(handler)](const QHttpServerRequest& request) -> QHttpServerResponse {
        const auto id = requestId(request.value("X-Request-ID"));
        RequestContext context(id);
        QElapsedTimer timer;
        timer.start();
        qCDebug(http) << "event=server.request.started route=" << route << "method=" << static_cast<int>(request.method());
        try {
            auto response = handler(request);
            auto headers = response.headers();
            headers.replaceOrAppend("X-Request-ID", id);
            response.setHeaders(std::move(headers));
            const auto status = static_cast<int>(response.statusCode());
            if (status >= 400)
                qCWarning(http) << "event=server.request.failed route=" << route << "status=" << status << "elapsed_ms=" << timer.elapsed();
            else
                qCDebug(http) << "event=server.request.completed route=" << route << "status=" << status << "elapsed_ms=" << timer.elapsed();
            return response;
        } catch (...) {
            qCCritical(http) << "event=server.request.exception route=" << route << "elapsed_ms=" << timer.elapsed();
            throw;
        }
    };
}

}

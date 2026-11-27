#pragma once

#include "Infrastructure/Logging/Logging.h"
#include <QNetworkReply>
#include <utility>

namespace Infrastructure::Logging {
inline void traceReply(QNetworkReply* reply) {
    const auto id = reply->request().rawHeader("X-Request-ID");
    // The request ID below is explicit. Do not inherit the previous request's
    // context when a response callback launches another request.
    RequestContext context({});
    // Application routes are fixed strings; omit host, query, credentials and body.
    const auto route = reply->request().url().path();
    QElapsedTimer timer;
    timer.start();
    qCDebug(http) << "event=client.request.started request_id=" << id << "route=" << route
                 << "method=" << static_cast<int>(reply->operation());
    // Bind to the reply, not ApiClient, so session cancellation still gets logged.
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, id, route, timer]() {
        RequestContext context({});
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto error = static_cast<int>(reply->error());
        if (error || status >= 400)
            qCWarning(http) << "event=client.request.failed request_id=" << id << "route=" << route
                           << "status=" << status << "network_error=" << error << "elapsed_ms=" << timer.elapsed();
        else
            qCDebug(http) << "event=client.request.completed request_id=" << id << "route=" << route
                         << "status=" << status << "elapsed_ms=" << timer.elapsed();
    });
}
} // namespace Infrastructure::Logging

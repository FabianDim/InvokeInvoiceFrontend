#include "Application/Accounts/FrontendAccountSession.h"
#include "Domain/Accounts/User.h"
#include "Infrastructure/Http/ApiClient.h"
#include <QHttpServer>
#include <QHttpServerResponder>
#include <QJsonArray>
#include <QFile>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTimer>
#include <QtTest>

using Application::Accounts::FrontendAccountSession;
using Infrastructure::Http::ApiClient;
using StatusCode = QHttpServerResponder::StatusCode;

namespace {
struct SessionServer {
    QHttpServer http;
    QTcpServer* tcp = new QTcpServer(&http);
    QByteArray login_body;
    QByteArray pending_login_body;
    int login_count = 0;
    QList<QByteArray> logout_authorizations;
    StatusCode login_status = StatusCode::Ok;
    QByteArray pdf_body = "%PDF-1.4\ntransport fixture\n%%EOF\n";
    QByteArray pdf_type = "application/pdf";
    StatusCode pdf_status = StatusCode::Ok;
    QList<QByteArray> demo_authorizations;
    QList<QJsonObject> demo_payloads;
    bool hold_demo = false;
    std::unique_ptr<QHttpServerResponder> pending_demo;
    bool hold_login = false;
    bool hold_logout = false;
    std::unique_ptr<QHttpServerResponder> pending_login;
    std::unique_ptr<QHttpServerResponder> pending_logout;
    QStringList requests;
    QByteArray businesses_body = "{}";
    QList<QByteArray> business_authorizations;
    bool hold_businesses = false;
    std::unique_ptr<QHttpServerResponder> pending_businesses;

    bool start() {
        http.route("/business/list", QHttpServerRequest::Method::Get,
            [this](const QHttpServerRequest& request, QHttpServerResponder& responder) {
                requests.append("businesses");
                business_authorizations.append(request.value("Authorization"));
                if (hold_businesses)
                    pending_businesses = std::make_unique<QHttpServerResponder>(std::move(responder));
                else
                    responder.write(businesses_body, "application/json");
            });
        http.route("/demo/invoices/pdf", QHttpServerRequest::Method::Post,
            [this](const QHttpServerRequest& request, QHttpServerResponder& responder) {
                requests.append("demo-pdf");
                demo_authorizations.append(request.value("Authorization"));
                demo_payloads.append(QJsonDocument::fromJson(request.body()).object());
                if (hold_demo) {
                    pending_demo = std::make_unique<QHttpServerResponder>(std::move(responder));
                } else {
                    QHttpHeaders headers;
                    headers.append("Content-Type", pdf_type);
                    if (pdf_status == StatusCode::TooManyRequests) headers.append("Retry-After", "20");
                    if (pdf_status == StatusCode::TemporaryRedirect) headers.append("Location", "/unexpected");
                    responder.write(pdf_body, headers, pdf_status);
                }
            });
        http.route("/auth/login", QHttpServerRequest::Method::Post,
                   [this](const QHttpServerRequest& request, QHttpServerResponder& responder) {
                       requests.append("login");
                       ++login_count;
                       const auto identity = QJsonObject{
                           {"_id", "65d000000000000000000003"},
                           {"firstName", "Alex"}, {"lastName", "Taylor"},
                           {"email", QJsonDocument::fromJson(request.body()).object().value("email")}};
                       const auto body = login_body.isNull()
                           ? QJsonDocument(QJsonObject{{"ok", true}, {"token", QString("session-%1").arg(login_count)},
                                                       {"user", identity}}).toJson(QJsonDocument::Compact)
                           : login_body;
                       if (hold_login) {
                           pending_login_body = body;
                           pending_login = std::make_unique<QHttpServerResponder>(std::move(responder));
                       } else
                           responder.write(body, "application/json", login_status);
                   });
        http.route("/auth/logout", QHttpServerRequest::Method::Post,
                   [this](const QHttpServerRequest& request, QHttpServerResponder& responder) {
                       requests.append("logout");
                       logout_authorizations.append(request.value("Authorization"));
                       if (hold_logout)
                           pending_logout = std::make_unique<QHttpServerResponder>(std::move(responder));
                       else
                           responder.write(QJsonDocument(QJsonObject{{"ok", true}}));
                   });
        http.route("/invoices/stock-list", QHttpServerRequest::Method::Post, [this]() {
            requests.append("pdf");
            return QHttpServerResponse(pdf_type, pdf_body, pdf_status);
        });
        http.route("/invoices/invoice_start", QHttpServerRequest::Method::Post, []() {
            return QHttpServerResponse(QJsonObject{{"ok", true}});
        });
        http.route("/invoices/pdf", QHttpServerRequest::Method::Get, [this]() {
            requests.append("archived-pdf");
            QHttpServerResponse response(pdf_type, pdf_body, pdf_status);
            if (pdf_status == StatusCode::TooManyRequests) {
                auto headers = response.headers(); headers.append("Retry-After", "20");
                response.setHeaders(std::move(headers));
            }
            return response;
        });
        return tcp->listen(QHostAddress::LocalHost, 0) && http.bind(tcp);
    }

    void finish_login() {
        pending_login->write(pending_login_body, "application/json", login_status);
        pending_login.reset();
    }

    void finish_logout() {
        pending_logout->write(QJsonDocument(QJsonObject{{"ok", true}}));
        pending_logout.reset();
    }

    QUrl url() const { return QUrl(QString("http://127.0.0.1:%1").arg(tcp->serverPort())); }
};
void begin_demo(ApiClient& api, const QString& directory) {
    api.set_offline(true);
    api.save_resource("business", QJsonDocument(QJsonObject{{"Business name", "Demo Business"}}));
    api.business_selected(QJsonObject{{"_id", "DEMO-1"}, {"BusinessName", "Demo Business"}});
    api.save_resource("client", QJsonDocument(QJsonObject{{"Name", "Demo Customer"}}));
    api.save_resource("stock", QJsonDocument(QJsonObject{{"Name", "Widget"}, {"Quantity", 5}, {"Price", 10}}));
    api.invoice_details(QJsonDocument(QJsonObject{{"ClientID", "DEMO-2"}, {"file_dir", directory}, {"file_name", "demo"}}));
}
QJsonDocument demo_lines() {
    return QJsonDocument(QJsonArray{QJsonObject{{"_id", "DEMO-3"}, {"Name", "Widget"}, {"Quantity", 2}, {"Price", 10}}});
}
int demo_quantity(ApiClient& api) {
    QSignalSpy records(&api, &ApiClient::business_items_received);
    api.get_business_items();
    return qvariant_cast<QJsonDocument>(records.last().first()).object().value("stocks").toArray().first().toObject().value("Quantity").toInt();
}
} // namespace

class FrontendSessionTests : public QObject {
    Q_OBJECT

  private slots:
    void large_business_list_loads_after_rejected_login() {
        SessionServer server;
        QVERIFY(server.start());
        server.login_status = StatusCode::Unauthorized;
        server.login_body = "{\"error\":\"Invalid email or password\"}";
        FrontendAccountSession account;
        ApiClient api(server.url(), &account);
        QSignalSpy login_failed(&api, &ApiClient::login_failed);
        QSignalSpy logged_in(&api, &ApiClient::login_succeeded);
        QSignalSpy loaded(&api, &ApiClient::business_list_received);
        QSignalSpy failed(&api, &ApiClient::business_list_failed);
        api.do_login("test@example.com", "wrong", false);
        QTRY_COMPARE(login_failed.count(), 1);
        server.login_status = StatusCode::Ok;
        server.login_body = QByteArray{};
        const QJsonObject business{{"_id", "65d000000000000000000004"},
            {"BusinessName", "Large logo business"}, {"LogoData", QString(1200000, QLatin1Char('A'))}};
        const QJsonDocument expected(QJsonObject{{"65d000000000000000004", business}});
        server.businesses_body = expected.toJson(QJsonDocument::Compact);
        connect(&api, &ApiClient::login_succeeded, &api, &ApiClient::get_business_list);
        api.do_login("test@example.com", "Secret1!", false);
        QTRY_COMPARE(logged_in.count(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 10000);
        QCOMPARE(qvariant_cast<QJsonDocument>(loaded.first().first()), expected);
        QCOMPARE(server.business_authorizations, QList<QByteArray>{"Bearer session-2"});
        QVERIFY(failed.isEmpty());
    }

    void malformed_business_list_reports_failure_and_can_retry() {
        SessionServer server;
        QVERIFY(server.start());
        FrontendAccountSession account;
        ApiClient api(server.url(), &account);
        QSignalSpy loaded(&api, &ApiClient::business_list_received);
        QSignalSpy failed(&api, &ApiClient::business_list_failed);
        for (const QByteArray body : {QByteArray("{broken"), QByteArray("[]")}) {
            server.businesses_body = body;
            api.get_business_list();
            QTRY_VERIFY(!failed.isEmpty());
            QVERIFY(failed.takeFirst().first().toString().contains("invalid business list"));
            QVERIFY(loaded.isEmpty());
        }
        server.businesses_body = "{}";
        api.get_business_list();
        QTRY_COMPARE(loaded.count(), 1);
        QVERIFY(failed.isEmpty());
    }

    void stalled_business_list_times_out_and_cancelled_session_stays_clear() {
        SessionServer server;
        server.hold_businesses = true;
        QVERIFY(server.start());
        FrontendAccountSession account;
        ApiClient api(server.url(), &account);
        QSignalSpy loaded(&api, &ApiClient::business_list_received);
        QSignalSpy failed(&api, &ApiClient::business_list_failed);
        api.get_business_list();
        auto* deadline = api.findChild<QTimer*>("business_list_deadline");
        QVERIFY(deadline);
        auto* reply = qobject_cast<QNetworkReply*>(deadline->parent());
        QVERIFY(reply);
        QVERIFY(!reply->request().attribute(QNetworkRequest::Http2AllowedAttribute).toBool());
        QCOMPARE(deadline->interval(), 45000);
        QTRY_VERIFY(server.pending_businesses != nullptr);
        deadline->start(1);
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(failed.first().first().toString().contains("timed out"));
        QVERIFY(loaded.isEmpty());
        server.pending_businesses.reset();
        server.hold_businesses = false;
        api.get_business_list();
        QTRY_COMPARE(loaded.count(), 1);
        loaded.clear();
        failed.clear();
        server.hold_businesses = true;
        api.get_business_list();
        QTRY_VERIFY(server.pending_businesses != nullptr);
        api.clear_session();
        server.pending_businesses->write(QJsonDocument(QJsonObject{}));
        server.pending_businesses.reset();
        QTest::qWait(30);
        QVERIFY(loaded.isEmpty());
        QVERIFY(failed.isEmpty());
    }

    void hosted_demo_export_and_saved_export_use_only_anonymous_endpoint() {
        SessionServer server;
        QVERIFY(server.start());
        QTemporaryDir output;
        ApiClient api(server.url(), nullptr);
        begin_demo(api, output.path());
        QCOMPARE(server.requests.size(), 0);
        QSignalSpy pdf(&api, &ApiClient::pdf_generated);
        api.stock_list(demo_lines());
        QTRY_COMPARE(pdf.count(), 1);
        QCOMPARE(demo_quantity(api), 3);
        QCOMPARE(server.demo_payloads.first().value("Total").toDouble(), 20.0);
        QVERIFY(!QJsonDocument(server.demo_payloads.first()).toJson().contains(output.path().toUtf8()));
        api.stock_list(demo_lines());
        QTRY_COMPARE(pdf.count(), 2);
        QCOMPARE(demo_quantity(api), 3);
        QSignalSpy history(&api, &ApiClient::invoices_received);
        api.get_invoices();
        const auto invoices = qvariant_cast<QJsonDocument>(history.last().first()).object().value("invoices").toArray();
        QCOMPARE(invoices.size(), 1);
        const auto id = invoices.first().toObject().value("_id").toString();
        api.set_invoice_paid(id, true);
        QSignalSpy exported(&api, &ApiClient::invoice_exported);
        api.export_saved_invoice(id, output.filePath("saved.pdf"));
        QTRY_COMPARE(exported.count(), 1);
        QCOMPARE(server.demo_payloads.last().value("Paid").toBool(), true);
        QCOMPARE(server.requests, (QStringList{"demo-pdf", "demo-pdf", "demo-pdf"}));
        for (const auto& authorization : server.demo_authorizations) QVERIFY(authorization.isEmpty());
    }

    void hosted_demo_failure_preserves_file_stock_and_history_data() {
        QTest::addColumn<int>("status");
        QTest::addColumn<QByteArray>("type");
        QTest::addColumn<QByteArray>("body");
        QTest::newRow("server failure") << 503 << QByteArray("application/json") << QByteArray("{}");
        QTest::newRow("wrong content type") << 200 << QByteArray("text/html") << QByteArray("%PDF-fake");
        QTest::newRow("invalid PDF") << 200 << QByteArray("application/pdf") << QByteArray("not a PDF");
        QTest::newRow("redirect") << 307 << QByteArray("application/pdf") << QByteArray("%PDF-redirect");
        QTest::newRow("oversized") << 200 << QByteArray("application/pdf") << (QByteArray("%PDF-") + QByteArray(16 * 1024 * 1024, 'x'));
    }

    void hosted_demo_failure_preserves_file_stock_and_history() {
        QFETCH(int, status); QFETCH(QByteArray, type); QFETCH(QByteArray, body);
        SessionServer server;
        server.pdf_status = static_cast<StatusCode>(status); server.pdf_type = type; server.pdf_body = body;
        QVERIFY(server.start());
        QTemporaryDir output;
        QFile original(output.filePath("demo.pdf"));
        QVERIFY(original.open(QIODevice::WriteOnly)); original.write("previous invoice"); original.close();
        ApiClient api(server.url(), nullptr);
        begin_demo(api, output.path());
        QSignalSpy failed(&api, &ApiClient::pdf_failed);
        QSignalSpy pdf(&api, &ApiClient::pdf_generated);
        api.stock_list(demo_lines());
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(pdf.isEmpty());
        QCOMPARE(demo_quantity(api), 5);
        QSignalSpy history(&api, &ApiClient::invoices_received);
        api.get_invoices();
        QVERIFY(qvariant_cast<QJsonDocument>(history.last().first()).object().value("invoices").toArray().isEmpty());
        QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(), QByteArray("previous invoice")); original.close();
        server.pdf_status = StatusCode::Ok; server.pdf_type = "application/pdf"; server.pdf_body = "%PDF-1.4\nretry\n%%EOF";
        api.stock_list(demo_lines());
        QTRY_COMPARE(pdf.count(), 1);
        QCOMPARE(demo_quantity(api), 3);
        QCOMPARE(server.requests.size(), 2);
    }

    void hosted_demo_rate_limit_is_honoured() {
        SessionServer server; server.pdf_status = StatusCode::TooManyRequests;
        QVERIFY(server.start());
        QTemporaryDir output; ApiClient api(server.url(), nullptr); begin_demo(api, output.path());
        QSignalSpy failed(&api, &ApiClient::pdf_failed);
        api.stock_list(demo_lines()); QTRY_COMPARE(failed.count(), 1);
        QVERIFY(failed.first().first().toString().contains("20"));
        api.stock_list(demo_lines()); QCOMPARE(failed.count(), 2);
        QCOMPARE(server.requests.size(), 1); QCOMPARE(demo_quantity(api), 5);
    }

    void hosted_demo_late_reply_cannot_cross_session_or_overwrite_changes_data() {
        QTest::addColumn<int>("change");
        QTest::newRow("exit") << 0;
        QTest::newRow("new invoice") << 1;
        QTest::newRow("edited records") << 2;
        QTest::newRow("different business") << 3;
        QTest::newRow("changed settings") << 4;
    }
    void hosted_demo_late_reply_cannot_cross_session_or_overwrite_changes() {
        QFETCH(int, change);
        SessionServer server; server.hold_demo = true; QVERIFY(server.start());
        QTemporaryDir output; ApiClient api(server.url(), nullptr); begin_demo(api, output.path());
        QSignalSpy pdf(&api, &ApiClient::pdf_generated);
        api.stock_list(demo_lines()); QTRY_VERIFY(server.pending_demo != nullptr);
        if (change == 0) api.set_offline(false);
        if (change == 1) api.reset_invoice();
        if (change == 2) api.save_resource("client", QJsonDocument(QJsonObject{{"Name", "Another client"}}));
        if (change == 3) api.business_selected(QJsonObject{});
        if (change == 4) api.save_invoice_settings(QJsonObject{{"BusinessID", "DEMO-1"}, {"InvoicePrefix", "CHANGED-"}});
        server.pending_demo->write(server.pdf_body, "application/pdf"); server.pending_demo.reset();
        QTest::qWait(100);
        QVERIFY(pdf.isEmpty()); QVERIFY(!QFile::exists(output.filePath("demo.pdf")));
        if (change == 2) QCOMPARE(demo_quantity(api), 5);
    }

    void hosted_backend_roundtrip() {
        const auto url = qEnvironmentVariable("INVOICE_TEST_DEMO_URL");
        if (url.isEmpty()) QSKIP("Set INVOICE_TEST_DEMO_URL to run against the separately built backend.");
        QVERIFY(QUrl(url).host() == "127.0.0.1");
        QTemporaryDir output; ApiClient api(QUrl(url), nullptr); begin_demo(api, output.path());
        QSignalSpy pdf(&api, &ApiClient::pdf_generated); QSignalSpy failed(&api, &ApiClient::pdf_failed);
        api.stock_list(demo_lines());
        QTRY_COMPARE(pdf.count() + failed.count(), 1);
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().first().toString()));
        QFile file(output.filePath("demo.pdf")); QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.size() > 500); QCOMPARE(file.read(5), QByteArray("%PDF-"));
        QCOMPARE(demo_quantity(api), 3);
    }

    void successful_http_login_updates_frontend_without_storing_password() {
        SessionServer server;
        QVERIFY(server.start());
        FrontendAccountSession account;
        ApiClient api(server.url(), &account);
        QVERIFY(!account.is_logged_in());
        QSignalSpy logged_in(&api, &ApiClient::login_succeeded);
        QSignalSpy failed(&api, &ApiClient::login_failed);
        api.do_login("test@example.com", "Secret1!", false);
        QTRY_COMPARE(logged_in.count(), 1);
        QVERIFY(account.is_logged_in());
        QCOMPARE(account.getAccount()->getEmail(), std::string("test@example.com"));
        QCOMPARE(account.getAccount()->getFirstName(), std::string("Alex"));
        QCOMPARE(account.getAccount()->getLastName(), std::string("Taylor"));
        QVERIFY(account.getAccount()->getPassword().empty());
        QVERIFY(failed.isEmpty());
        QCOMPARE(server.requests, QStringList{"login"});
    }

    void failed_login_does_not_authenticate_data() {
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<int>("status");
        QTest::addColumn<QString>("message");
        QTest::newRow("rejected") << QByteArray("{\"error\":\"Invalid email or password\"}") << 401
                                  << QString("Invalid email or password");
        QTest::newRow("malformed") << QByteArray("not json") << 200
                                   << QString("The server returned an invalid sign-in response. Please update the backend.");
        QTest::newRow("missing-success") << QByteArray("{}") << 200
                                         << QString("The server returned an invalid sign-in response. Please update the backend.");
        QTest::newRow("missing-token") << QByteArray("{\"ok\":true,\"user\":{\"_id\":\"65d000000000000000000003\"}}") << 200
                                       << QString("The server returned an invalid sign-in response. Please update the backend.");
    }

    void failed_login_does_not_authenticate() {
        QFETCH(QByteArray, body);
        QFETCH(int, status);
        QFETCH(QString, message);
        SessionServer server;
        server.login_body = body;
        server.login_status = static_cast<StatusCode>(status);
        QVERIFY(server.start());
        FrontendAccountSession account;
        account.login("previous@example.com", "");
        ApiClient api(server.url(), &account);
        QSignalSpy logged_in(&api, &ApiClient::login_succeeded);
        QSignalSpy failed(&api, &ApiClient::login_failed);
        api.do_login("test@example.com", "wrong", false);
        QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(failed.first().first().toString(), message);
        QVERIFY(!account.is_logged_in());
        QVERIFY(logged_in.isEmpty());
        QCOMPARE(server.requests, QStringList{"login"});
        QVERIFY(server.logout_authorizations.isEmpty()); // No token was issued to revoke.
    }

    void sign_out_clears_local_and_backend_sessions() {
        SessionServer server;
        QVERIFY(server.start());
        FrontendAccountSession account;
        ApiClient api(server.url(), &account);
        QSignalSpy logged_in(&api, &ApiClient::login_succeeded);
        api.do_login("test@example.com", "Secret1!", false);
        QTRY_COMPARE(logged_in.count(), 1);
        api.clear_session();
        QVERIFY(!account.is_logged_in());
        QVERIFY(!account.getAccount());
        QTRY_COMPARE(server.requests, (QStringList{"login", "logout"}));
        QCOMPARE(server.logout_authorizations, (QList<QByteArray>{"Bearer session-1"}));
    }

    void old_token_logout_cannot_clear_new_login() {
        SessionServer server;
        QVERIFY(server.start());
        FrontendAccountSession account;
        ApiClient api(server.url(), &account);
        QSignalSpy logged_in(&api, &ApiClient::login_succeeded);
        api.do_login("first@example.com", "Secret1!", false);
        QTRY_COMPARE(logged_in.count(), 1);
        server.hold_logout = true;
        api.clear_session();
        QTRY_VERIFY(server.pending_logout != nullptr);
        QCOMPARE(server.requests, (QStringList{"login", "logout"}));
        QVERIFY(!account.is_logged_in());
        QCOMPARE(server.logout_authorizations, (QList<QByteArray>{"Bearer session-1"}));
        api.do_login("second@example.com", "Secret2!", false);
        QTRY_COMPARE(logged_in.count(), 2);
        QCOMPARE(server.requests, (QStringList{"login", "logout", "login"}));
        QCOMPARE(account.getAccount()->getEmail(), std::string("second@example.com"));
        server.finish_logout();
        QTest::qWait(50);
        QVERIFY(account.is_logged_in());
        QCOMPARE(account.getAccount()->getEmail(), std::string("second@example.com"));
        server.hold_logout = false;
        api.clear_session();
        QTRY_COMPARE(server.logout_authorizations.size(), 2);
        QCOMPARE(server.logout_authorizations.last(), QByteArray("Bearer session-2"));
    }

    void entering_demo_ends_online_session() {
        SessionServer server;
        QVERIFY(server.start());
        FrontendAccountSession account;
        ApiClient api(server.url(), &account);
        QSignalSpy logged_in(&api, &ApiClient::login_succeeded);
        api.do_login("test@example.com", "Secret1!", false);
        QTRY_COMPARE(logged_in.count(), 1);
        api.set_offline(true);
        QVERIFY(api.is_offline());
        QVERIFY(!account.is_logged_in());
        QTRY_COMPARE(server.requests, (QStringList{"login", "logout"}));
    }

    void pending_login_cannot_authenticate_after_entering_demo() {
        SessionServer server;
        server.hold_login = true;
        QVERIFY(server.start());
        FrontendAccountSession account;
        ApiClient api(server.url(), &account);
        QSignalSpy logged_in(&api, &ApiClient::login_succeeded);
        api.do_login("test@example.com", "Secret1!", false);
        QTRY_VERIFY(server.pending_login != nullptr);
        api.set_offline(true);
        server.finish_login();
        QTest::qWait(50); // The aborted request's response must not authenticate a demo.
        QCOMPARE(server.requests, QStringList{"login"});
        QVERIFY(api.is_offline());
        QVERIFY(!account.is_logged_in());
        QVERIFY(logged_in.isEmpty());
    }

    void fresh_offline_session_needs_no_backend() {
        SessionServer server;
        QVERIFY(server.start());
        QSignalSpy connections(server.tcp, &QTcpServer::newConnection);
        FrontendAccountSession account;
        ApiClient api(server.url(), &account);
        QSignalSpy businesses(&api, &ApiClient::business_list_received);
        api.set_offline(true);
        api.get_business_list();
        QCOMPARE(businesses.count(), 1);
        api.set_offline(false);
        QTest::qWait(50);
        QVERIFY(connections.isEmpty());
        QVERIFY(server.requests.isEmpty());
    }

    void archived_pdf_export_enforces_wait_and_preserves_files_data() {
        QTest::addColumn<int>("status");
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<bool>("success");
        QTest::newRow("pdf") << 200 << QByteArray("%PDF-1.4\narchived invoice\n%%EOF\n") << true;
        QTest::newRow("invalid-pdf") << 200 << QByteArray("invalid") << false;
        QTest::newRow("server-wait") << 429 << QByteArray("{\"error\":\"Please wait\"}") << false;
    }

    void archived_pdf_export_enforces_wait_and_preserves_files() {
        QFETCH(int, status); QFETCH(QByteArray, body); QFETCH(bool, success);
        SessionServer server;
        server.pdf_status = static_cast<StatusCode>(status); server.pdf_body = body;
        QVERIFY(server.start());
        FrontendAccountSession account;
        ApiClient api(server.url(), &account);
        api.business_selected({{"_id", "65a000000000000000000001"}});
        QTemporaryDir output; QVERIFY(output.isValid());
        const auto path = output.filePath("saved.pdf");
        { QFile previous(path); QVERIFY(previous.open(QIODevice::WriteOnly)); previous.write("original file"); }
        QSignalSpy exported(&api, &ApiClient::invoice_exported);
        QSignalSpy failed(&api, &ApiClient::invoice_export_failed);
        QSignalSpy waits(&api, &ApiClient::invoice_export_wait);
        api.export_saved_invoice("65e000000000000000000001", path);
        QTRY_COMPARE(exported.count() + failed.count(), 1);
        QCOMPARE(exported.count(), success ? 1 : 0);
        if (status == 429) QCOMPARE(waits.last().first().toInt(), 20);
        QFile result(path); QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll(), success ? body : QByteArray("original file"));
        api.export_saved_invoice("65e000000000000000000002", output.filePath("another.pdf"));
        QCOMPARE(failed.count(), success ? 1 : 2);
        QCOMPARE(server.requests, QStringList{"archived-pdf"});
        QVERIFY(!QFile::exists(output.filePath("another.pdf")));
    }

    void pdf_results_cross_the_http_boundary_data() {
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<QByteArray>("content_type");
        QTest::addColumn<int>("status");
        QTest::addColumn<bool>("succeeds");
        QTest::addColumn<QString>("result");
        QTest::newRow("created") << QByteArray("%PDF-1.4\ntransport fixture\n%%EOF\n") << QByteArray("application/pdf") << 200
                                 << true << QString{};
        QTest::newRow("generation-failed") << QByteArray("{\"error\":\"Output folder is unavailable\"}") << QByteArray("application/json") << 500
                                           << false << QString("Output folder is unavailable");
        QTest::newRow("legacy-path-response") << QByteArray("{\"ok\":true,\"path\":\"C:/invoices/example.pdf\"}") << QByteArray("application/json") << 200
                                          << false << QString("The backend did not return a PDF. Please update the backend and try again.");
        QTest::newRow("invalid-pdf-body") << QByteArray("not a PDF") << QByteArray("application/pdf") << 200
                                          << false << QString("The backend did not return a PDF. Please update the backend and try again.");
    }

    void pdf_results_cross_the_http_boundary() {
        QFETCH(QByteArray, body);
        QFETCH(QByteArray, content_type);
        QFETCH(int, status);
        QFETCH(bool, succeeds);
        QFETCH(QString, result);
        SessionServer server;
        server.pdf_body = body;
        server.pdf_type = content_type;
        server.pdf_status = static_cast<StatusCode>(status);
        QVERIFY(server.start());
        FrontendAccountSession account;
        ApiClient api(server.url(), &account);
        QTemporaryDir output;
        QVERIFY(output.isValid());
        api.business_selected(QJsonObject{{"_id", "65a000000000000000000001"}});
        QSignalSpy started(&api, &ApiClient::invoice_started);
        api.invoice_details(QJsonDocument(QJsonObject{{"ClientID", "65b000000000000000000001"},
                                                     {"file_dir", output.path()}, {"file_name", "example"}}));
        QTRY_COMPARE(started.count(), 1);
        QSignalSpy generated(&api, &ApiClient::pdf_generated);
        QSignalSpy failed(&api, &ApiClient::pdf_failed);
        api.stock_list(QJsonDocument(QJsonArray{}));
        QTRY_COMPARE(generated.count() + failed.count(), 1);
        QCOMPARE(generated.count(), succeeds ? 1 : 0);
        QCOMPARE((succeeds ? generated : failed).first().first().toString(),
                 succeeds ? output.filePath("example.pdf") : result);
        QCOMPARE(server.requests, QStringList{"pdf"});
        QFile pdf(output.filePath("example.pdf"));
        if (succeeds) {
            QVERIFY(pdf.open(QIODevice::ReadOnly));
            QCOMPARE(pdf.readAll(), body);
        } else {
            QVERIFY(!pdf.exists());
        }
    }
};

QTEST_GUILESS_MAIN(FrontendSessionTests)
#include "FrontendSessionTests.moc"

#include "Infrastructure/Updates/UpdateController.h"
#include "Infrastructure/Updates/UpdateStore.h"
#include "Infrastructure/Logging/Logging.h"
#include <QApplication>
#include <QCommandLineParser>
#include <qfile.h>
#include <QIcon>
#include <QStatusBar>
#include <QLabel>
#include "Application/Accounts/FrontendAccountSession.h"
#include "Application/Controllers/AppController.h"
#include "Infrastructure/Http/ApiClient.h"
#include "View/MainWindow.h"
#include <cstdio>

int main(int argc, char* argv[]) {
    for (int index = 1; index + 1 < argc; ++index) {
        if (QString::fromLocal8Bit(argv[index]) == "--update-wait-pid") {
            bool valid = false;
            const auto pid = QString::fromLocal8Bit(argv[index + 1]).toLongLong(&valid);
            if (!valid || !Infrastructure::Updates::wait_for_launcher(pid)) return EXIT_FAILURE;
        }
    }
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Invoke");
    QCoreApplication::setApplicationName("InvokeInvoiceSystem");
    QCoreApplication::setApplicationVersion(QStringLiteral(INVOKE_APP_VERSION));
    QCommandLineParser commandLine;
    commandLine.addHelpOption();
    commandLine.addVersionOption();
    QCommandLineOption waitOption("update-wait-pid", "Wait for the previous application instance to exit.", "pid");
    waitOption.setFlags(QCommandLineOption::HiddenFromHelp);
    commandLine.addOption(waitOption);
    commandLine.addOption({"no-update-handoff", "Run this executable directly, bypassing the cached update (recovery)."});
    commandLine.addOption({"development", "Run the local build without cached updates or release update checks."});
    QCommandLineOption selfCheckOption("update-self-check",
        "Report this executable's version and exit; used by the updater's startup check.");
    selfCheckOption.setFlags(QCommandLineOption::HiddenFromHelp);
    commandLine.addOption(selfCheckOption);
    commandLine.process(app);
    if (commandLine.isSet("update-self-check")) {
        // Written explicitly to stdout because Qt sends --version output to stderr.
        const auto report = QByteArray("InvokeInvoiceSystem ") + QCoreApplication::applicationVersion().toUtf8() + '\n';
        std::fwrite(report.constData(), 1, size_t(report.size()), stdout);
        std::fflush(stdout);
        return EXIT_SUCCESS;
    }
    const bool development = INVOICE_DEVELOPMENT_BUILD || commandLine.isSet("development");
    if (!development && !commandLine.isSet("no-update-handoff") && Infrastructure::Updates::launch_cached_update()) return EXIT_SUCCESS;
    Infrastructure::Logging::initialize(true);
    qCInfo(Infrastructure::Logging::app) << "event=application.starting qt_version=" << qVersion()
        << "development=" << development << "executable=" << QCoreApplication::applicationFilePath();
    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
        qCInfo(Infrastructure::Logging::app) << "event=application.stopping";
    });

    app.setWindowIcon(QIcon(":/icons/invoice_icon.png"));
    QFile f(":/styles/UI/Global.qss");
    if (f.open(QIODevice::ReadOnly)) {
        QString StyleSheet = QString::fromUtf8(f.readAll());
        app.setStyleSheet(StyleSheet);
    } else {
        qWarning() << "Failed to load stylesheet" << f.fileName() << f.errorString();
    }
    Application::Accounts::FrontendAccountSession accountManager;
    const QUrl backendUrl(qEnvironmentVariable("INVOICE_API_URL",
        "https://invoice-backend-app-69d26786220b.herokuapp.com"));
    const bool localBackend = backendUrl.scheme() == "http" && backendUrl.host() == "127.0.0.1";
    if (!backendUrl.isValid() || backendUrl.host().isEmpty() ||
        (backendUrl.scheme() != "https" && !localBackend) ||
        !backendUrl.userInfo().isEmpty() || backendUrl.hasQuery() || backendUrl.hasFragment() ||
        (!backendUrl.path().isEmpty() && backendUrl.path() != "/")) {
        qCritical() << "INVOICE_API_URL must be an HTTPS server URL (HTTP is allowed only for 127.0.0.1).";
        return EXIT_FAILURE;
    }
    qInfo() << "Invoice backend:" << backendUrl.toString();
    auto api = new Infrastructure::Http::ApiClient(backendUrl, &accountManager, &app);
    App::Views::MainWindow window(accountManager);
    Application::Controllers::AppController controller(&window, accountManager, api);
    window.resize(1080, 860);
    if (development) {
        window.setWindowTitle("Invoke Invoice - Development");
        window.statusBar()->addPermanentWidget(new QLabel("Development build - release updates disabled", &window));
    }
    window.show();
    if (!development) new Infrastructure::Updates::UpdateController(&window);
    qCInfo(Infrastructure::Logging::app) << "event=application.ready mode=" << (localBackend ? "local_backend" : "remote_backend");
    return app.exec();
}

cmake_minimum_required(VERSION 3.24)
project(InvokeInvoiceFrontend LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
file(READ "${CMAKE_SOURCE_DIR}/vcpkg.json" INVOICE_MANIFEST)
string(JSON INVOICE_BASE_VERSION GET "${INVOICE_MANIFEST}" version-string)
set(INVOICE_APP_VERSION "${INVOICE_BASE_VERSION}" CACHE STRING "Application version")
if(NOT INVOICE_APP_VERSION MATCHES "^[0-9]+[.][0-9]+[.][0-9]+$")
    message(FATAL_ERROR "INVOICE_APP_VERSION must be major.minor.patch")
endif()
add_compile_definitions(INVOKE_APP_VERSION="${INVOICE_APP_VERSION}")
include(CTest)
find_package(fmt CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Qt6Keychain CONFIG REQUIRED)
find_package(Qt6 COMPONENTS Widgets Network REQUIRED)
file(READ "${CMAKE_SOURCE_DIR}/config/update-public-key.pem" INVOICE_UPDATE_PUBLIC_KEY)
configure_file(config/UpdateTrust.h.in generated/UpdateTrust.h @ONLY)
qt_standard_project_setup()
set(CMAKE_AUTORCC ON)
add_library(frontend_core OBJECT
    src/View/MainWindow.cpp
    src/View/StyleSheet.cpp
    src/View/UICode/Views/BusinessInvoiceChoice.cpp
    src/View/UICode/Views/Dashboard.cpp
    src/View/UICode/Views/DefaultInvoiceSettings.cpp
    src/View/UICode/Views/InvoicesPage.cpp
    src/View/UICode/Views/ItemsPage.cpp
    src/View/UICode/Views/LandingPage.cpp
    src/View/UICode/Views/LoginPage.cpp
    src/View/UICode/Views/ManagementForm.cpp
    src/View/UICode/Views/NewInvoiceCreation.cpp
    src/View/UICode/Views/NewInvoiceStock.cpp
    src/View/UICode/Views/SignupPage.cpp
    src/View/UICode/Widgets/AnimatedButton.cpp
    src/Application/Controllers/AppController.cpp
    src/Application/Accounts/FrontendAccountSession.cpp
    src/Application/Auth/QSettingsSessionManager.cpp
    src/Application/Invoices/OfflineInvoiceSession.cpp
    src/Infrastructure/Http/ApiClient.cpp
    src/Infrastructure/Security/LocalAuth/LocalPasswordSaving.cpp
    src/Infrastructure/Security/LocalAuth/KeyChainClass.cpp
    src/Domain/Accounts/User.cpp
    src/Infrastructure/Updates/UpdateController.cpp
    src/Infrastructure/Updates/UpdateDownload.cpp
    src/Infrastructure/Updates/UpdateSecurity.cpp
    src/Infrastructure/Updates/UpdateStore.cpp
    include/Application/Accounts/FrontendAccountSession.h
    include/Application/Auth/ISessionManager.h
    include/Application/Auth/QSettingsSessionManager.h
    include/Application/Business/BusinessRepository.h
    include/Application/Controllers/AppController.h
    include/Application/Invoices/OfflineInvoiceSession.h
    include/Domain/Accounts/Interfaces/IAccountManager.h
    include/Domain/Accounts/User.h
    include/Domain/Business/Clients/Client.h
    include/Domain/Business/Clients/ClientPreferences.h
    include/Domain/Business/InvoicePreferences.h
    include/Domain/Invoices/Invoice.h
    include/Domain/Invoices/InvoiceAmounts.h
    include/Domain/Invoices/InvoiceFilename.h
    include/Domain/Invoices/InvoiceRecord.h
    include/Domain/Invoices/InvoiceTemplateEnum.h
    include/Domain/Records/RecordDefinition.h
    include/Domain/Stock/StockEdit.h
    include/Domain/Stock/StockItem.h
    include/Domain/Stock/StockShortage.h
    include/Infrastructure/Enums/RouteEnums.h
    include/Infrastructure/Http/ApiClient.h
    include/Infrastructure/Logging/HttpLogging.h
    include/Infrastructure/Logging/Logging.h
    include/Infrastructure/Security/LocalAuth/KeyChainClass.h
    include/Infrastructure/Security/LocalAuth/LocalPasswordSaving.h
    include/Infrastructure/Updates/UpdateController.h
    include/Infrastructure/Updates/UpdateDownload.h
    include/Infrastructure/Updates/UpdateSecurity.h
    include/Infrastructure/Updates/UpdateStore.h
    include/pch.h
    include/Utils/BusinessLogo.h
    include/Utils/DateUtil.h
    include/Utils/FormUtils.h
    include/Utils/StringUtils.h
    include/Utils/TemplateUtil.h
    include/View/MainWindow.h
    include/View/StyleSheet.h
    include/View/UICode/Views/BusinessInvoiceChoice.h
    include/View/UICode/Views/ClientPreferencesForm.h
    include/View/UICode/Views/CustomerForm.h
    include/Domain/Business/Clients/CustomerDetails.h
    include/View/UICode/Views/Dashboard.h
    include/View/UICode/Views/DefaultInvoiceSettings.h
    include/View/UICode/Views/FilenameTemplateEdit.h
    include/View/UICode/Views/InvoicePreferencesForm.h
    include/View/UICode/Views/InvoicesPage.h
    include/View/UICode/Views/ItemsPage.h
    include/View/UICode/Views/LandingPage.h
    include/View/UICode/Views/LoginPage.h
    include/View/UICode/Views/ManagementForm.h
    include/View/UICode/Views/NewInvoiceCreation.h
    include/View/UICode/Views/NewInvoiceStock.h
    include/View/UICode/Views/SignupPage.h
    include/View/UICode/Widgets/AnimatedButton.h
    include/View/UiStyle.h
    forms/Invoice_Resources.qrc
)
target_include_directories(frontend_core PUBLIC include api "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(frontend_core PUBLIC Qt6::Widgets Qt6::Network Qt6Keychain::Qt6Keychain OpenSSL::Crypto fmt::fmt)
qt_add_executable(InvokeInvoiceSystem src/View/InvokeInvoiceSystem.cpp)
target_compile_definitions(InvokeInvoiceSystem PRIVATE
    INVOICE_DEVELOPMENT_BUILD=$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>)
target_link_libraries(InvokeInvoiceSystem PRIVATE frontend_core)
set_target_properties(InvokeInvoiceSystem PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE ON)
qt_import_plugins(InvokeInvoiceSystem INCLUDE Qt6::QJpegPlugin)
if(WIN32)
    string(REPLACE "." "," INVOICE_FILE_VERSION_COMMAS "${INVOICE_APP_VERSION},0")
    configure_file(InvokeInvoiceSystem.rc.in InvokeInvoiceSystem.rc @ONLY)
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/InvokeInvoiceSystem.rc" PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/forms/resources/icons/invoice_icon.ico")
    target_sources(InvokeInvoiceSystem PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/InvokeInvoiceSystem.rc")
    qt_import_plugins(InvokeInvoiceSystem INCLUDE Qt6::QWindowsIntegrationPlugin)
endif()

function(register_invoice_test name target label)
    add_test(NAME ${name} COMMAND ${target})
    add_test(NAME ${name}Discovery COMMAND ${target} -functions)
    set_tests_properties(${name} ${name}Discovery PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_ASSUME_STDERR_HAS_CONSOLE=1")
    set_tests_properties(${name} PROPERTIES TIMEOUT 180 LABELS "${label}")
    set_tests_properties(${name}Discovery PROPERTIES TIMEOUT 30 LABELS smoke)
    qt_import_plugins(${target} INCLUDE Qt6::QOffscreenIntegrationPlugin Qt6::QJpegPlugin)
    if(WIN32 AND QT6_IS_SHARED_LIBS_BUILD)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/platforms"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>" "$<TARGET_FILE_DIR:${target}>/platforms/"
            VERBATIM)
    endif()
endfunction()
if(BUILD_TESTING)
    find_package(Qt6 COMPONENTS Test HttpServer REQUIRED)
    foreach(suite BusinessSession FrontendSession Logging Update)
        qt_add_executable(${suite}Tests tests/${suite}Tests.cpp)
        target_link_libraries(${suite}Tests PRIVATE frontend_core Qt6::Test Qt6::HttpServer)
        register_invoice_test(${suite} ${suite}Tests local)
    endforeach()
    add_custom_target(all-tests DEPENDS BusinessSessionTests FrontendSessionTests LoggingTests UpdateTests InvokeInvoiceSystem)
    if(WIN32)
        add_test(NAME WindowsDesktopStartup COMMAND InvokeInvoiceSystem --version)
        add_test(NAME WindowsUpdateSelfCheck COMMAND InvokeInvoiceSystem --update-self-check)
        set_tests_properties(WindowsDesktopStartup WindowsUpdateSelfCheck PROPERTIES LABELS local TIMEOUT 30
            ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_ASSUME_STDERR_HAS_CONSOLE=1")
        set_tests_properties(WindowsUpdateSelfCheck PROPERTIES PASS_REGULAR_EXPRESSION "InvokeInvoiceSystem [0-9]+[.][0-9]+[.][0-9]+")
    endif()
endif()
if(WIN32 AND QT6_IS_SHARED_LIBS_BUILD)
    add_custom_command(TARGET InvokeInvoiceSystem POST_BUILD
        COMMAND Qt6::windeployqt --release --no-libraries --no-translations --no-compiler-runtime --no-patchqt "$<TARGET_FILE:InvokeInvoiceSystem>"
        VERBATIM)
endif()
install(TARGETS InvokeInvoiceSystem BUNDLE DESTINATION . RUNTIME DESTINATION bin)
qt_generate_deploy_app_script(TARGET InvokeInvoiceSystem OUTPUT_SCRIPT deploy_script NO_UNSUPPORTED_PLATFORM_ERROR)
install(SCRIPT ${deploy_script})

if(WIN32 AND QT6_IS_SHARED_LIBS_BUILD)
    find_package(libjpeg-turbo CONFIG REQUIRED)
    foreach(target InvokeInvoiceSystem BusinessSessionTests FrontendSessionTests UpdateTests LoggingTests)
        if(TARGET ${target})
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/imageformats"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::QJpegPlugin>" "$<TARGET_FILE_DIR:${target}>/imageformats/"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:libjpeg-turbo::jpeg>" "$<TARGET_FILE_DIR:${target}>/"
                VERBATIM)
        endif()
    endforeach()
endif()

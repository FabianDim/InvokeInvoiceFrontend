#pragma once
#include "View/UICode/Views/ClientPreferencesForm.h"
#include <QButtonGroup>
#include <QGridLayout>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QTabWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QCompleter>
#include <QTimer>
#include <QMap>

namespace App::Views {
// Reflow field groups instead of squeezing labels and inputs on smaller windows.
class CustomerColumns : public QWidget {
  public:
    explicit CustomerColumns(QWidget* parent = nullptr, int breakpoint = 620, int wide_columns = 2)
        : QWidget(parent), grid_(new QGridLayout(this)), breakpoint_(breakpoint), wide_columns_(wide_columns) {
        grid_->setContentsMargins(0, 0, 0, 0);
        grid_->setSizeConstraint(QLayout::SetNoConstraint);
        grid_->setHorizontalSpacing(24); grid_->setVerticalSpacing(16);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }
    void add(QWidget* widget) { fields_.append(widget); reflow(); }
    QSize minimumSizeHint() const override { return {0, grid_->minimumSize().height()}; }
  protected:
    void resizeEvent(QResizeEvent* event) override { QWidget::resizeEvent(event); reflow(); }
  private:
    void reflow() {
        const int columns = width() >= breakpoint_ ? wide_columns_ : 1;
        if (columns == columns_ && count_ == fields_.size()) return;
        columns_ = columns; count_ = fields_.size();
        for (auto* field : fields_) grid_->removeWidget(field);
        for (int i = 0; i < fields_.size(); ++i) grid_->addWidget(fields_[i], i / columns, i % columns);
        for (int column = 0; column < wide_columns_; ++column) grid_->setColumnStretch(column, column < columns ? 1 : 0);
        updateGeometry();
    }
    QGridLayout* grid_;
    QVector<QWidget*> fields_;
    int breakpoint_, wide_columns_, columns_ = 0, count_ = -1;
};

class CustomerForm : public QWidget {
  public:
    explicit CustomerForm(QWidget* parent, bool editing = false) : QWidget(parent), editing_(editing) {
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0); root->setSpacing(16);
        tabs_ = new QTabWidget(this); tabs_->setObjectName("customer_tabs");
        tabs_->setDocumentMode(true); tabs_->setUsesScrollButtons(true);
        root->addWidget(tabs_);

        auto* overview = page("Overview", "Customer details", "Only the display name is required. Everything else can be added later.");
        auto* type_row = new QHBoxLayout;
        business_ = new QRadioButton("Business", this); individual_ = new QRadioButton("Individual", this);
        business_->setObjectName("customer_type_business"); individual_->setObjectName("customer_type_individual");
        auto* types = new QButtonGroup(this); types->addButton(business_); types->addButton(individual_);
        business_->setChecked(true);
        type_row->addWidget(business_); type_row->addWidget(individual_); type_row->addStretch();
        overview->addWidget(UiStyle::label("Customer type", this)); overview->addLayout(type_row);
        auto* core = new CustomerColumns(this);
        name_ = field(core, "Display name *", "Name", "name", 200, true);
        name_->setToolTip("The customer name shown on invoices and in customer lists.");
        company_ = field(core, "Company name", "Company name", "company_name");
        email_ = field(core, "Email", "Email", "email", 200, true);
        phone_ = field(core, "Work phone", "Phone", "phone", 200, true);
        mobile_ = field(core, "Mobile", "Mobile", "mobile");
        website_ = field(core, "Website", "https://example.com", "website", 2000);
        overview->addWidget(core);
        overview->addSpacing(4);
        overview->addWidget(UiStyle::label("Primary contact", this, "sectionTitle"));
        auto* contact = new CustomerColumns(this, 620, 3);
        salutation_ = field(contact, "Salutation", "e.g. Ms, Mr, Dr", "salutation");
        first_ = field(contact, "First name", "First name", "first_name");
        last_ = field(contact, "Last name", "Last name", "last_name");
        overview->addWidget(contact); overview->addStretch();
        connect(individual_, &QRadioButton::toggled, this, [this](bool individual) {
            company_->parentWidget()->setVisible(!individual);
            resize_tabs();
        });

        auto* addresses = page("Addresses", "Billing & shipping", "The billing address appears on invoices. Shipping details are saved for your reference.");
        auto* address_columns = new CustomerColumns(this, 760);
        address_columns->add(address_card("Billing address", "billing", billing_));
        address_columns->add(address_card("Shipping address", "shipping", shipping_));
        auto* copy = new QPushButton("Copy billing to shipping", this); copy->setObjectName("customer_copy_billing");
        UiStyle::button(copy);
        connect(copy, &QPushButton::clicked, this, [this]() {
            for (auto it = billing_.cbegin(); it != billing_.cend(); ++it) shipping_[it.key()]->setText(it.value()->text());
            UiStyle::message(address_status_, "Billing address copied. You can edit the shipping address separately.");
        });
        addresses->addWidget(copy, 0, Qt::AlignLeft); addresses->addWidget(address_columns);
        address_status_ = UiStyle::label("", this, "body"); address_status_->setWordWrap(true); address_status_->hide();
        addresses->addWidget(address_status_); addresses->addStretch();

        auto* contacts = page("Contacts", "Additional contacts", "Add the people you work with at this customer, such as accounts or purchasing.");
        contacts_layout_ = new QVBoxLayout; contacts_layout_->setSpacing(12); contacts->addLayout(contacts_layout_);
        contacts_empty_ = UiStyle::label("No additional contacts yet. Your primary contact is saved in Overview.", this, "body");
        contacts_empty_->setWordWrap(true); contacts->addWidget(contacts_empty_);
        add_contact_ = new QPushButton("+ Add contact person", this); add_contact_->setObjectName("customer_add_contact");
        UiStyle::button(add_contact_); contacts->addWidget(add_contact_, 0, Qt::AlignLeft);
        connect(add_contact_, &QPushButton::clicked, this, [this]() { add_contact({}); }); contacts->addStretch();

        auto* pricing = page("Pricing", "Payment & pricing", "Set payment terms and optional prices just for this customer.");
        preferences_ = new ClientPreferencesForm(this); pricing->addWidget(preferences_); pricing->addStretch();

        auto* notes = page("Notes", "Customer notes", "Keep useful background and reminders here. These notes do not appear on invoices.");
        notes_ = new QPlainTextEdit(this); notes_->setObjectName("customer_notes");
        notes_->setAccessibleName("Customer notes"); notes_->setPlaceholderText("Delivery preferences, account reminders, or other information...");
        notes_->setMinimumHeight(180); notes->addWidget(notes_);
        auto* count = UiStyle::label("0 / 10,000 characters", this, "body"); notes->addWidget(count); notes->addStretch();
        connect(notes_, &QPlainTextEdit::textChanged, this, [this, count]() {
            count->setText(QString("%1 / 10,000 characters").arg(notes_->toPlainText().size()));
        });
        connect(tabs_, &QTabWidget::currentChanged, this, [this]() {
            resize_tabs();
            QTimer::singleShot(0, this, [this]() {
                for (auto* parent = parentWidget(); parent; parent = parent->parentWidget())
                    if (auto* scroll = qobject_cast<QScrollArea*>(parent)) { scroll->verticalScrollBar()->setValue(0); break; }
            });
        });
        resize_tabs();
    }

    ClientPreferencesForm* preferences() const { return preferences_; }
    void set_values(const QJsonObject& record) {
        auto details = record.value("CustomerDetails").toObject();
        name_->setText(record.value("Name").toString()); email_->setText(record.value("Email").toString());
        phone_->setText(record.value("Phone").toString());
        (details.value("Type").toString() == "Individual" ? individual_ : business_)->setChecked(true);
        company_->setText(details.value("CompanyName").toString()); salutation_->setText(details.value("Salutation").toString());
        // Preserve an older full contact name as entered; do not guess name boundaries.
        first_->setText(details.isEmpty() ? record.value("ContactName").toString() : details.value("FirstName").toString());
        last_->setText(details.value("LastName").toString()); mobile_->setText(details.value("Mobile").toString());
        QStringList primary;
        for (const auto* part : {"Salutation", "FirstName", "LastName"})
            if (!details.value(part).toString().isEmpty()) primary.append(details.value(part).toString());
        if (!details.isEmpty() && record.contains("ContactName") && primary.join(' ') != record.value("ContactName").toString()) {
            // An older desktop may have changed the invoice-facing contact.
            salutation_->clear(); last_->clear(); first_->setText(record.value("ContactName").toString());
        }
        website_->setText(details.value("Website").toString()); notes_->setPlainText(details.value("Remarks").toString());
        auto billing = details.value("BillingAddress").toObject();
        if (details.isEmpty() || (record.contains("Address") && Domain::Clients::customer_billing_address(details) != record.value("Address").toString()))
            billing = {{"Street1", record.value("Address").toString()}};
        const auto shipping = details.value("ShippingAddress").toObject();
        for (auto it = billing_.begin(); it != billing_.end(); ++it) it.value()->setText(billing.value(it.key()).toString());
        for (auto it = shipping_.begin(); it != shipping_.end(); ++it) it.value()->setText(shipping.value(it.key()).toString());
        for (const auto& contact : contacts_) delete contact.card;
        contacts_.clear();
        for (const auto& contact : details.value("Contacts").toArray()) add_contact(contact.toObject());
        update_contacts(); preferences_->set_values(record);
        address_status_->hide(); tabs_->setCurrentIndex(0); resize_tabs();
    }

    QJsonObject values() {
        if (name_->text().trimmed().isEmpty()) {
            tabs_->setCurrentIndex(0); name_->setFocus();
            throw std::invalid_argument("Enter a customer display name.");
        }
        static const QRegularExpression email_pattern(R"(^[^\s@]+@[^\s@]+\.[^\s@]+$)");
        if (!email_->text().trimmed().isEmpty() && !email_pattern.match(email_->text().trimmed()).hasMatch()) {
            tabs_->setCurrentIndex(0); email_->setFocus(); throw std::invalid_argument("Enter a valid customer email address, or leave it blank.");
        }
        QJsonObject details{{"Type", individual_->isChecked() ? "Individual" : "Business"},
            {"CompanyName", company_->text()}, {"Salutation", salutation_->text()}, {"FirstName", first_->text()},
            {"LastName", last_->text()}, {"Mobile", mobile_->text()}, {"Website", website_->text()}, {"Remarks", notes_->toPlainText()}};
        for (const auto& key : {QString("BillingAddress"), QString("ShippingAddress")}) {
            QJsonObject address;
            const auto& fields = key == "BillingAddress" ? billing_ : shipping_;
            for (auto it = fields.cbegin(); it != fields.cend(); ++it) address[it.key()] = it.value()->text();
            details[key] = address;
        }
        QJsonArray contacts;
        for (const auto& contact : contacts_) {
            QJsonObject entry;
            for (auto it = contact.fields.cbegin(); it != contact.fields.cend(); ++it) entry[it.key()] = it.value()->text();
            contacts.append(entry);
        }
        details["Contacts"] = contacts;
        try { details = Domain::Clients::customer_details(details); }
        catch (const std::exception& error) {
            const auto message = QString::fromUtf8(error.what());
            tabs_->setCurrentIndex(message.contains("contact", Qt::CaseInsensitive) ? 2 : message.contains("Remarks") ? 4 : 0);
            throw;
        }
        QStringList name;
        for (const auto* key : {"Salutation", "FirstName", "LastName"})
            if (!details.value(key).toString().isEmpty()) name.append(details.value(key).toString());
        const auto contact_name = name.join(' ');
        const auto address = Domain::Clients::customer_billing_address(details);
        if (contact_name.size() > 200) { tabs_->setCurrentIndex(0); throw std::invalid_argument("Keep the primary contact's full name within 200 characters."); }
        if (address.size() > 2000) { tabs_->setCurrentIndex(1); throw std::invalid_argument("Keep the combined billing address within 2,000 characters."); }
        QJsonObject result{{"Name", name_->text().trimmed()}, {"Email", email_->text().trimmed()}, {"Phone", phone_->text().trimmed()},
            {"Contact name", contact_name}, {"ContactName", contact_name}, {"Address", address}, {"CustomerDetails", details}};
        try {
            const auto prefs = preferences_->values();
            for (auto it = prefs.begin(); it != prefs.end(); ++it) result[it.key()] = it.value();
        } catch (...) { tabs_->setCurrentIndex(3); throw; }
        return result;
    }
  protected:
    void resizeEvent(QResizeEvent* event) override { QWidget::resizeEvent(event); resize_tabs(); }
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::LayoutRequest)
            QTimer::singleShot(0, this, [this]() { resize_tabs(); });
        return QWidget::eventFilter(watched, event);
    }
  private:
    struct Contact { QWidget* card; QMap<QString, QLineEdit*> fields; };
    QVBoxLayout* page(const QString& title, const QString& heading, const QString& help) {
        auto* page = new QWidget(tabs_);
        auto* layout = new QVBoxLayout(page); layout->setContentsMargins(20, 22, 20, 20); layout->setSpacing(16);
        layout->addWidget(UiStyle::label(heading, page, "sectionTitle"));
        auto* description = UiStyle::label(help, page, "body"); description->setWordWrap(true); layout->addWidget(description);
        page->installEventFilter(this);
        tabs_->addTab(page, title); return layout;
    }
    QLineEdit* field(CustomerColumns* columns, const QString& label, const QString& placeholder, const QString& key,
                     int maximum = 200, bool legacy = false) {
        auto* group = new QWidget(columns); auto* layout = new QVBoxLayout(group);
        layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(6);
        QLineEdit* edit;
        QWidget* control;
        if (key.endsWith("_country")) {
            auto* country = new QComboBox(group); country->setEditable(true);
            country->setObjectName("customer_" + key + "_combo");
            country->setInsertPolicy(QComboBox::NoInsert);
            country->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); country->setMinimumContentsLength(8);
            QStringList countries;
            for (const auto& locale : QLocale::matchingLocales(QLocale::AnyLanguage, QLocale::AnyScript, QLocale::AnyTerritory))
                if (locale.territory() != QLocale::AnyTerritory) countries.append(QLocale::territoryToString(locale.territory()));
            countries.removeDuplicates(); countries.sort(Qt::CaseInsensitive); country->addItems(countries);
            country->setCurrentIndex(-1); country->completer()->setCaseSensitivity(Qt::CaseInsensitive);
            edit = country->lineEdit(); control = country;
        } else { edit = new QLineEdit(group); control = edit; }
        edit->setMaxLength(maximum); edit->setMinimumWidth(0);
        edit->setPlaceholderText(placeholder); edit->setAccessibleName(label);
        edit->setObjectName(legacy ? (editing_ ? "client_edit_" + key : "form_input") : "customer_" + key);
        auto* caption = UiStyle::label(label, group); caption->setBuddy(control);
        layout->addWidget(caption); layout->addWidget(control); columns->add(group); return edit;
    }
    QWidget* address_card(const QString& title, const QString& prefix, QMap<QString, QLineEdit*>& fields) {
        auto* card = new QFrame(this); card->setObjectName("customer_address_card");
        auto* layout = new QVBoxLayout(card); layout->setContentsMargins(16, 16, 16, 16); layout->setSpacing(16);
        layout->addWidget(UiStyle::label(title, card, "sectionTitle"));
        auto* columns = new CustomerColumns(card, 350);
        const QList<QPair<QString, QString>> names{{"Attention", "Attention"}, {"Country", "Country / region"},
            {"Street1", "Street address"}, {"Street2", "Address line 2"}, {"City", "City / suburb"}, {"State", "State or province"},
            {"Postcode", "Postcode"}, {"Phone", "Phone"}, {"Fax", "Fax"}};
        for (const auto& [key, label] : names)
            fields[key] = field(columns, label, label, prefix + "_" + key.toLower(), key.startsWith("Street") ? 2000 : 200);
        layout->addWidget(columns); layout->addStretch(); return card;
    }
    void add_contact(const QJsonObject& record) {
        if (contacts_.size() >= 20) return;
        auto* card = new QFrame(this); card->setObjectName("customer_contact_card");
        auto* layout = new QVBoxLayout(card); layout->setContentsMargins(16, 16, 16, 16); layout->setSpacing(12);
        auto* heading = new QHBoxLayout; heading->addWidget(UiStyle::label("Contact person", card, "sectionTitle")); heading->addStretch();
        auto* remove = new QPushButton("Remove", card); UiStyle::button(remove); remove->setObjectName("customer_remove_contact");
        remove->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        remove->setAccessibleName("Remove this contact person"); heading->addWidget(remove); layout->addLayout(heading);
        Contact contact{card, {}};
        auto* columns = new CustomerColumns(card);
        const QList<QPair<QString, QString>> names{{"Salutation", "Salutation"}, {"FirstName", "First name"},
            {"LastName", "Last name"}, {"Email", "Email"}, {"Phone", "Work phone"}, {"Mobile", "Mobile"}};
        for (const auto& [key, label] : names) {
            auto* edit = field(columns, label, label, "contact_" + key.toLower());
            edit->setText(record.value(key).toString()); contact.fields[key] = edit;
        }
        layout->addWidget(columns); contacts_.append(contact); contacts_layout_->addWidget(card);
        connect(remove, &QPushButton::clicked, this, [this, card]() {
            for (int i = 0; i < contacts_.size(); ++i) if (contacts_[i].card == card) { contacts_.removeAt(i); break; }
            contacts_layout_->removeWidget(card); card->hide(); card->deleteLater(); update_contacts();
        });
        update_contacts();
    }
    void update_contacts() {
        contacts_empty_->setVisible(contacts_.isEmpty()); add_contact_->setEnabled(contacts_.size() < 20);
        resize_tabs();
    }
    void resize_tabs() {
        for (int i = 0; i < tabs_->count(); ++i) tabs_->widget(i)->setSizePolicy(QSizePolicy::Ignored,
            i == tabs_->currentIndex() ? QSizePolicy::Preferred : QSizePolicy::Ignored);
        if (auto* current = tabs_->currentWidget()) {
            const int height = current->layout()->sizeHint().height() + tabs_->tabBar()->sizeHint().height() + 4;
            if (tabs_->minimumHeight() != height) tabs_->setFixedHeight(height);
        }
        tabs_->updateGeometry(); updateGeometry();
    }
    bool editing_;
    QTabWidget* tabs_;
    QRadioButton *business_, *individual_;
    QLineEdit *name_, *company_, *email_, *phone_, *mobile_, *website_, *salutation_, *first_, *last_;
    QMap<QString, QLineEdit*> billing_, shipping_;
    QPlainTextEdit* notes_;
    ClientPreferencesForm* preferences_;
    QVBoxLayout* contacts_layout_;
    QVector<Contact> contacts_;
    QLabel *contacts_empty_, *address_status_;
    QPushButton* add_contact_;
};
}

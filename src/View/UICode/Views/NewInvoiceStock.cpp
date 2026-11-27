#include "Domain/Invoices/InvoiceRecord.h"
#include "Domain/Business/Clients/ClientPreferences.h"
#include "Infrastructure/Logging/Logging.h"
#include "View/UICode/Views/NewInvoiceStock.h"
#include "View/UiStyle.h"
#include <QGroupBox>
#include <QJsonDocument>
#include <QScrollArea>
#include <QMessageBox>
#include <QTimer>

using namespace App::Views;

NewInvoiceStock::NewInvoiceStock(QWidget* parent) : QWidget(parent) {
    create_page_layout();
}

void NewInvoiceStock::reset_invoice() {
    Infrastructure::Logging::Operation operation(Infrastructure::Logging::ui(), "NewInvoiceStock.reset_invoice");
    ++stock_request_;
    shortage_policy_ = "reject";
    if (shortage_dialog_) shortage_dialog_->reject();
    saving_pdf_ = false;
    invoice_finalized_ = false;
    set_saving_stock(false);
    stock_items = QJsonArray{};
    for (auto* row : item_rows_)
        delete row;
    item_rows_.clear();
    resize_item_list();
    for (auto* field : invoice_body_form_fields) {
        if (auto* selector = qobject_cast<QComboBox*>(field))
            selector->clear();
        else if (auto* input = qobject_cast<QLineEdit*>(field))
            input->clear();
    }
    create_invoice_pdf->setEnabled(true);
    create_invoice_pdf->setText("Finish invoice");
    new_name_->clear();
    unit_->clear();
    keywords_->clear();
    margin_->setValue(0);
    stock_on_hand_->setValue(0);
    save_stock_->setChecked(false);
    item_source_->setCurrentIndex(0);
    UiStyle::message(status_label_, {});
    update_totals();
    update_item_source();
}

void NewInvoiceStock::business_selected(const QJsonObject& business) {
    Infrastructure::Logging::Operation operation(Infrastructure::Logging::ui(), "NewInvoiceStock.business_selected");
    const auto registered = Domain::Business::gst_registered(business);
    const bool changed = registered != gst_registered_;
    gst_registered_ = registered;
    const auto id = business.value("_id").toString();
    if (id != business_id_ || id.isEmpty() || changed) {
        business_id_ = id;
        reset_invoice();
    }
    update_totals();
}

void NewInvoiceStock::populate_stock_list(const QJsonDocument& list) {
    Infrastructure::Logging::Operation operation(Infrastructure::Logging::ui(), "NewInvoiceStock.populate_stock_list");
    auto* selector = qobject_cast<QComboBox*>(invoice_body_form_fields.value("stock_selector"));
    if (!selector || !list.isArray()) return;
    selector->clear();
    for (const auto& value : list.array()) {
        const auto item = value.toObject();
        selector->addItem(item.value("Name").toString(), item);
    }
    update_stock_price();
}

void NewInvoiceStock::client_selected(const QJsonObject& client) {
    client_ = client;
    update_stock_price();
}

void NewInvoiceStock::update_stock_price() {
    if (item_source_->currentIndex() != 1) return;
    auto* selector = qobject_cast<QComboBox*>(invoice_body_form_fields.value("stock_selector"));
    const auto stock = selector->currentData().toJsonObject();
    auto* price = qobject_cast<QLineEdit*>(invoice_body_form_fields.value("price"));
    price->setText(stock.isEmpty() ? QString{} : QLocale().toString(Domain::Clients::stock_price(client_, stock), 'f', 2));
    price->setToolTip(client_.value("StockPrices").toObject().contains(stock.value("_id").toString())
        ? "This client's saved price. You can change it for this invoice."
        : "The stock item's default price. You can change it for this invoice.");
}

void NewInvoiceStock::create_page_layout() {
    auto* content = UiStyle::page_content(this, 960);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);
    auto* title = UiStyle::label("Invoice items", content, "title");
    title->setAlignment(Qt::AlignLeft);
    layout->addWidget(title);

    auto* group = new QGroupBox("Items added", content);
    auto* group_layout = new QVBoxLayout(group);
    group_layout->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(group);
    item_scroll_ = scroll;
    scroll->setObjectName("invoice_item_list");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* item_list = new QWidget(scroll);
    auto* rows = new QVBoxLayout(item_list);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setAlignment(Qt::AlignTop);
    rows->setSpacing(10);
    item_header_ = new QWidget(item_list);
    auto* header = new QHBoxLayout(item_header_);
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(10);
    header->addWidget(UiStyle::label("Item name", item_list), 1);
    for (const auto& column : QList<QPair<QString, int>>{{"Quantity", 58}, {"Price ex GST", 82}, {"GST / total", 102}}) {
        auto* label = UiStyle::label(column.first, item_list);
        label->setFixedWidth(column.second);
        label->setWordWrap(true);
        header->addWidget(label);
    }
    auto* remove_heading = new QWidget(item_header_);
    remove_heading->setFixedWidth(88);
    header->addWidget(remove_heading);
    rows->addWidget(item_header_);
    empty_items_ = UiStyle::label("No items yet. Add your first item below.", item_list, "body");
    empty_items_->setWordWrap(true);
    rows->addWidget(empty_items_);
    scroll->setWidget(item_list);
    item_list->installEventFilter(this);
    scroll->viewport()->installEventFilter(this);
    resize_item_list();
    group_layout->addWidget(scroll);
    layout->addWidget(group);
    totals_label_ = UiStyle::label("", content);
    totals_label_->setObjectName("invoice_totals");
    totals_label_->setWordWrap(true);
    layout->addWidget(totals_label_);
    connect(this, &NewInvoiceStock::add_item_to_invoice, this,
            [this, rows, item_list](const QJsonObject& item) {
                auto* row = new QWidget(item_list);
                auto* cells = new QHBoxLayout(row);
                cells->setContentsMargins(0, 0, 0, 0);
                cells->setSpacing(10);
                auto* name = UiStyle::label(item.value("Name").toString(), row, "body");
                name->setObjectName("invoice_line_name");
                name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
                name->setTextFormat(Qt::PlainText);
                name->setWordWrap(true);
                auto* quantity = UiStyle::label(QString::number(item.value("Quantity").toInt()), row, "body");
                auto* price = UiStyle::label(QString::number(item.value("Price").toDouble(), 'f', 2), row, "body");
                quantity->setFixedWidth(58);
                price->setFixedWidth(82);
                auto* remove = new QPushButton("Remove", row);
                remove->setObjectName("invoice_remove_item");
                remove->setAccessibleName("Remove " + item.value("Name").toString() + " from invoice");
                remove->setToolTip("Remove this line from the invoice. Saved business stock is kept.");
                UiStyle::button(remove);
                remove->setFixedWidth(88);
                cells->addWidget(name, 1);
                cells->addWidget(quantity);
                cells->addWidget(price);
                Domain::Invoices::Amounts line;
                line.add(item.value("Price").toDouble(), item.value("Quantity").toInt(), gst_registered_);
                auto* total = UiStyle::label(QString("$%1 / $%2").arg(line.gst, 0, 'f', 2).arg(line.total(), 0, 'f', 2), row, "body");
                total->setFixedWidth(102);
                total->setWordWrap(true);
                cells->addWidget(total);
                cells->addWidget(remove);
                rows->addWidget(row);
                item_rows_.append(row);
                row->show();
                resize_item_list();
                row->setEnabled(!saving_stock_ && !saving_pdf_ && !invoice_finalized_);
                connect(remove, &QPushButton::clicked, this, [this, rows, row]() {
                    if (saving_stock_ || saving_pdf_ || invoice_finalized_) return;
                    const auto index = item_rows_.indexOf(row);
                    if (index < 0 || index >= stock_items.size()) return;
                    stock_items.removeAt(index);
                    update_totals();
                    item_rows_.removeAt(index);
                    rows->removeWidget(row);
                    row->hide();
                    row->deleteLater();
                    resize_item_list();
                    shortage_policy_ = "reject";
                    UiStyle::message(status_label_, "Item removed from this invoice. Saved business stock is unchanged.");
                });
            });

    create_invoice_pdf = new QPushButton("Finish invoice", content);
    create_invoice_pdf->setObjectName("invoice_finish");
    back_button_ = new QPushButton("Back to dashboard", content);
    UiStyle::button(create_invoice_pdf, "primary");
    UiStyle::button(back_button_);
    layout->addLayout(create_item_entry_form());
    status_label_ = UiStyle::label("", content, "status");
    status_label_->setObjectName("invoice_stock_status");
    status_label_->setTextFormat(Qt::PlainText);
    status_label_->setWordWrap(true);
    layout->addWidget(status_label_);
    auto* buttons = new UiStyle::ActionRow(content);
    buttons->addWidget(back_button_, 1);
    buttons->addWidget(create_invoice_pdf, 1);
    layout->addWidget(buttons);
    connect(back_button_, &QPushButton::clicked, this, [this]() { emit invoice_navigation(Page::Dashboard); });
}

QLayout* NewInvoiceStock::create_item_entry_form() {
    auto* container = new QVBoxLayout;
    entry_widget_ = new QWidget(this);
    container->addWidget(entry_widget_);
    auto* form = new QGridLayout(entry_widget_);
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);
    form->setColumnStretch(0, 1);
    form->setColumnStretch(1, 1);
    auto* name = new QComboBox(this);
    name->setObjectName("invoice_saved_stock");
    name->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    item_source_ = new QComboBox(this);
    item_source_->setObjectName("invoice_item_source");
    item_source_->addItems({"New item", "Saved stock"});
    new_name_ = new QLineEdit(this);
    new_name_->setObjectName("invoice_new_stock_name");
    new_name_->setPlaceholderText("Item name or description");
    auto* quantity = new QLineEdit(this);
    auto* price = new QLineEdit(this);
    auto* notes = new QLineEdit(this);
    auto* add = new QPushButton("Add item", this);
    add->setObjectName("invoice_add_item");
    UiStyle::button(add);
    quantity->setValidator(new QIntValidator(1, 1'000'000, quantity));
    price->setValidator(new QDoubleValidator(0, 1e9, 2, price));
    quantity->setPlaceholderText("Quantity");
    price->setPlaceholderText("Price each");
    notes->setPlaceholderText("Optional notes");
    const auto add_field = [this, form](const QString& text, QWidget* edit, int row, int column, int span = 1) {
        auto* label = UiStyle::label(text, this);
        label->setBuddy(edit);
        edit->setProperty("role", "input");
        edit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        form->addWidget(label, row, column, 1, span);
        form->addWidget(edit, row + 1, column, 1, span);
    };
    add_field("Item source", item_source_, 0, 0);
    add_field("Item", new_name_, 0, 1);
    name->setProperty("role", "input");
    form->addWidget(name, 1, 1);
    add_field("Invoice quantity", quantity, 2, 0);
    add_field("Price each (excluding GST)", price, 2, 1);
    add_field("Notes (optional)", notes, 4, 0, 2);
    save_stock_ = new UiStyle::FormToggle("Save this item to business stock", this);
    save_stock_->setObjectName("save_invoice_stock");
    save_stock_->setProperty("role", "formToggle");
    save_stock_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    save_stock_->setToolTip("Keep this item in business stock for use on future invoices.");
    form->addWidget(save_stock_, 6, 0, 1, 2);
    saved_fields_ = new QWidget(this);
    auto* saved_layout = new QGridLayout(saved_fields_);
    saved_layout->setContentsMargins(0, 0, 0, 0);
    saved_layout->setHorizontalSpacing(12);
    saved_layout->setVerticalSpacing(10);
    saved_layout->setColumnStretch(0, 1);
    saved_layout->setColumnStretch(1, 1);
    stock_on_hand_ = new QSpinBox(saved_fields_);
    stock_on_hand_->setRange(0, 1'000'000);
    stock_on_hand_->setObjectName("new_stock_on_hand");
    margin_ = new QDoubleSpinBox(saved_fields_);
    margin_->setRange(0, 1'000'000);
    margin_->setSuffix("%");
    margin_->setObjectName("new_stock_margin");
    unit_ = new QLineEdit(saved_fields_);
    unit_->setObjectName("new_stock_unit");
    unit_->setMaxLength(80);
    unit_->setPlaceholderText("e.g. each, hour, metre or box");
    unit_->setToolTip("How this item is sold, e.g. each, hour, metre or box.");
    margin_->setToolTip("Profit as a percentage of the selling price, for your records only. It does not change the invoice price.");
    keywords_ = new QLineEdit(saved_fields_);
    keywords_->setObjectName("new_stock_keywords");
    keywords_->setPlaceholderText("Optional, comma separated");
    const QList<QPair<QString, QWidget*>> saved_inputs{
        {"Stock on hand", stock_on_hand_}, {"Margin", margin_}, {"Unit", unit_}, {"Keywords", keywords_}};
    for (int index = 0; index < saved_inputs.size(); ++index) {
        const bool required = saved_inputs[index].first == "Unit";
        auto* label = UiStyle::label(required ? saved_inputs[index].first : saved_inputs[index].first + " (optional)",
                                      saved_fields_);
        label->setBuddy(saved_inputs[index].second);
        saved_inputs[index].second->setProperty("role", "input");
        saved_layout->addWidget(label, (index / 2) * 2, index % 2);
        saved_layout->addWidget(saved_inputs[index].second, (index / 2) * 2 + 1, index % 2);
    }
    form->addWidget(saved_fields_, 7, 0, 1, 2);
    form->addWidget(add, 8, 1);
    invoice_body_form_fields["stock_selector"] = name;
    invoice_body_form_fields["quantity"] = quantity;
    invoice_body_form_fields["price"] = price;
    invoice_body_form_fields["notes"] = notes;
    connect(item_source_, &QComboBox::currentIndexChanged, this, &NewInvoiceStock::update_item_source);
    connect(item_source_, &QComboBox::currentIndexChanged, this, [this]() {
        if (item_source_->currentIndex() == 0)
            qobject_cast<QLineEdit*>(invoice_body_form_fields.value("price"))->clear();
        else update_stock_price();
    });
    connect(name, &QComboBox::currentIndexChanged, this, &NewInvoiceStock::update_stock_price);
    connect(save_stock_, &QCheckBox::toggled, this, &NewInvoiceStock::update_item_source);
    update_item_source();

    connect(add, &QPushButton::clicked, this, [=, this]() {
        qCDebug(Infrastructure::Logging::ui) << "event=invoice.item.add_requested saving_pending=" << saving_stock_;
        if (saving_stock_) return;
        const bool is_new = item_source_->currentIndex() == 0;
        const auto selected = name->currentData().toJsonObject();
        if (!quantity->hasAcceptableInput() || (is_new && new_name_->text().trimmed().isEmpty()) ||
            (!is_new && selected.isEmpty()) ||
            ((is_new || !price->text().isEmpty()) && !price->hasAcceptableInput())) {
            qCWarning(Infrastructure::Logging::ui) << "event=invoice.item.rejected reason=invalid_fields";
            UiStyle::message(status_label_, "Enter an item name, a positive invoice quantity, and a valid price.");
            return;
        }
        if (is_new && save_stock_->isChecked() && unit_->text().trimmed().isEmpty()) {
            qCWarning(Infrastructure::Logging::ui) << "event=invoice.item.rejected reason=missing_unit";
            UiStyle::message(status_label_, "Enter a unit when saving this item to stock.");
            unit_->setFocus();
            return;
        }
        const auto* price_validator = qobject_cast<const QDoubleValidator*>(price->validator());
        const auto item_price = price->text().isEmpty() ? Domain::Clients::stock_price(client_, selected)
                                                       : price_validator->locale().toDouble(price->text());
        const auto* quantity_validator = qobject_cast<const QIntValidator*>(quantity->validator());
        QJsonObject item{{"Quantity", quantity_validator->locale().toInt(quantity->text())}, {"Price", item_price},
                         {"Name", is_new ? new_name_->text().trimmed() : selected.value("Name").toString()},
                         {"Notes", notes->text()}};
        if (!is_new)
            item["_id"] = selected.value("_id");
        if (is_new && save_stock_->isChecked()) {
            item["StockOnHand"] = stock_on_hand_->value();
            item["Margin"] = margin_->value() / 100.0;
            item["Unit"] = unit_->text().trimmed();
            item["Keywords"] = keywords_->text().trimmed();
            set_saving_stock(true);
            UiStyle::message(status_label_, offline_ ? "Adding demo stock..." : "Saving stock item...");
            emit save_invoice_stock(QJsonDocument(item), ++stock_request_);
            return;
        }
        append_item(item);
        UiStyle::message(status_label_, "Item added to this invoice.");
    });
    connect(create_invoice_pdf, &QPushButton::clicked, this, [this]() {
        if (saving_stock_ || saving_pdf_) return;
        if (stock_items.empty()) {
            UiStyle::message(status_label_, "Add at least one item before finishing the invoice.");
            return;
        }
        saving_pdf_ = true;
        set_saving_stock(false);
        UiStyle::message(status_label_, "Creating and saving PDF...");
        if (shortage_policy_ == "reject") emit add_item_list_to_invoice(QJsonDocument(stock_items));
        else emit resolve_stock_shortage(QJsonDocument(stock_items), shortage_policy_);
    });
    return container;
}

void NewInvoiceStock::resize_item_list() {
    if (!item_scroll_) return;
    const bool empty = item_rows_.isEmpty();
    item_header_->setVisible(!empty);
    empty_items_->setVisible(empty);
    auto* rows = item_scroll_->widget()->layout();
    const auto margins = rows->contentsMargins();
    const int name_width = qMax(1, item_scroll_->viewport()->width() - margins.left() - margins.right()
                                  - 58 - 82 - 102 - 88 - 4 * 10);
    for (auto* row : item_rows_) {
        auto* name = row->findChild<QLabel*>("invoice_line_name");
        auto* remove = row->findChild<QPushButton*>("invoice_remove_item");
        const auto row_height = qMax(remove->sizeHint().height(), name->heightForWidth(name_width));
        if (row->height() != row_height) row->setFixedHeight(row_height);
    }
    const auto height = rows->totalSizeHint().height();
    const auto desired = qBound(48, height + 2, 240);
    if (item_scroll_->height() != desired) item_scroll_->setFixedHeight(desired);
}

bool NewInvoiceStock::eventFilter(QObject* watched, QEvent* event) {
    if (item_scroll_ && ((watched == item_scroll_->widget() && event->type() == QEvent::LayoutRequest)
        || (watched == item_scroll_->viewport() && event->type() == QEvent::Resize)))
        QTimer::singleShot(0, this, [this]() { resize_item_list(); });
    return QWidget::eventFilter(watched, event);
}

void NewInvoiceStock::update_item_source() {
    const bool is_new = item_source_->currentIndex() == 0;
    new_name_->setVisible(is_new);
    invoice_body_form_fields.value("stock_selector")->setVisible(!is_new);
    save_stock_->setVisible(is_new);
    saved_fields_->setVisible(is_new && save_stock_->isChecked());
    qobject_cast<QLineEdit*>(invoice_body_form_fields.value("price"))->setPlaceholderText(is_new ? "Price each" : "Use stock price");
}

void NewInvoiceStock::set_saving_stock(bool saving) {
    saving_stock_ = saving;
    entry_widget_->setEnabled(!saving && !saving_pdf_ && !invoice_finalized_);
    for (auto* row : item_rows_)
        row->setEnabled(!saving && !saving_pdf_ && !invoice_finalized_);
    back_button_->setEnabled(!saving && !saving_pdf_);
    create_invoice_pdf->setEnabled(!saving && !saving_pdf_);
}

void NewInvoiceStock::append_item(const QJsonObject& item) {
    Infrastructure::Logging::Operation operation(Infrastructure::Logging::ui(), "NewInvoiceStock.append_item");
    stock_items.append(item);
    update_totals();
    shortage_policy_ = "reject";
    emit add_item_to_invoice(item);
    for (const auto& key : {"quantity", "price", "notes"})
        qobject_cast<QLineEdit*>(invoice_body_form_fields.value(key))->clear();
    new_name_->clear();
    save_stock_->setChecked(false);
    stock_on_hand_->setValue(0);
    margin_->setValue(0);
    unit_->clear();
    keywords_->clear();
    update_stock_price();
}

void NewInvoiceStock::stock_saved(const QJsonDocument& item, quint64 request_id) {
    Infrastructure::Logging::Operation operation(Infrastructure::Logging::ui(), "NewInvoiceStock.stock_saved");
    if (!saving_stock_ || request_id != stock_request_) return;
    set_saving_stock(false);
    append_item(item.object());
    auto saved = item.object();
    saved["Quantity"] = saved.value("StockOnHand");
    auto* selector = qobject_cast<QComboBox*>(invoice_body_form_fields.value("stock_selector"));
    selector->addItem(saved.value("Name").toString(), saved);
    UiStyle::message(status_label_, offline_ ? "Demo stock added to this invoice. It will be cleared when you exit the demo."
                                    : "Stock item saved and added to this invoice.");
}

void NewInvoiceStock::stock_save_failed(const QString& message, quint64 request_id) {
    Infrastructure::Logging::Operation operation(Infrastructure::Logging::ui(), "NewInvoiceStock.stock_save_failed");
    if (!saving_stock_ || request_id != stock_request_) return;
    set_saving_stock(false);
    UiStyle::message(status_label_, message + " Retry, or untick Save to add it only to this invoice.");
}

void NewInvoiceStock::set_offline(bool offline) {
    offline_ = offline;
    reset_invoice();
    item_source_->setItemText(1, offline ? "Demo stock" : "Saved stock");
    save_stock_->setText(offline ? "Keep this item in demo stock" : "Save this item to business stock");
}

void NewInvoiceStock::pdf_generated(const QString& path) {
    Infrastructure::Logging::Operation operation(Infrastructure::Logging::ui(), "NewInvoiceStock.pdf_generated");
    saving_pdf_ = false;
    invoice_finalized_ = true;
    set_saving_stock(false);
    UiStyle::message(status_label_, "PDF saved to " + path);
    entry_widget_->setEnabled(false);
    create_invoice_pdf->setText("Save PDF again");
}

void NewInvoiceStock::pdf_failed(const QString& message) {
    Infrastructure::Logging::Operation operation(Infrastructure::Logging::ui(), "NewInvoiceStock.pdf_failed");
    saving_pdf_ = false;
    set_saving_stock(false);
    UiStyle::message(status_label_, message);
}

void NewInvoiceStock::show_stock_shortage(const QJsonArray& items) {
    if (!saving_pdf_ || shortage_dialog_) return;
    auto* dialog = new QMessageBox(QMessageBox::Warning, "Not enough stock",
        "Some items on this invoice exceed the stock on hand. How should the remaining stock be recorded?",
        QMessageBox::NoButton, this);
    dialog->setObjectName("stock_shortage_dialog");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setTextFormat(Qt::PlainText);
    QStringList details;
    for (const auto& value : items) {
        const auto item = value.toObject();
        details.append(QString("%1: %2 available, %3 on this invoice")
            .arg(item.value("Name").toString()).arg(item.value("Available").toDouble())
            .arg(item.value("Requested").toDouble()));
    }
    dialog->setInformativeText(details.join('\n') + "\n\nYour choice applies to shortages on this invoice only.");
    auto* zero = dialog->addButton("Set stock to zero", QMessageBox::AcceptRole);
    auto* negative = dialog->addButton("Allow negative stock", QMessageBox::AcceptRole);
    auto* cancel = dialog->addButton(QMessageBox::Cancel);
    zero->setObjectName("shortage_zero");
    negative->setObjectName("shortage_negative");
    cancel->setObjectName("shortage_cancel");
    for (auto* button : {zero, negative, cancel}) UiStyle::button(button);
    dialog->setDefaultButton(cancel);
    dialog->setEscapeButton(cancel);
    shortage_dialog_ = dialog;
    const auto generation = stock_request_;
    connect(dialog, &QDialog::finished, this, [this, dialog, zero, negative, generation]() {
        shortage_dialog_.clear();
        if (generation != stock_request_ || !saving_pdf_) return;
        if (dialog->clickedButton() != zero && dialog->clickedButton() != negative) {
            pdf_failed("Invoice not completed. Stock has not changed; you can review the invoice or try again.");
            return;
        }
        shortage_policy_ = dialog->clickedButton() == zero ? "zero" : "negative";
        UiStyle::message(status_label_, "Creating and saving PDF...");
        emit resolve_stock_shortage(QJsonDocument(stock_items), shortage_policy_);
    });
    dialog->open();
}

void NewInvoiceStock::update_totals() {
    Domain::Invoices::Amounts totals;
    for (const auto& value : stock_items) {
        const auto line = value.toObject();
        totals.add(line.value("Price").toDouble(), line.value("Quantity").toInt(), gst_registered_);
    }
    totals_label_->setText(QString("Subtotal: $%1   |   GST%2: $%3   |   Total: $%4")
        .arg(totals.subtotal, 0, 'f', 2).arg(gst_registered_ ? " (10%)" : " (not registered)")
        .arg(totals.gst, 0, 'f', 2).arg(totals.total(), 0, 'f', 2));
}

#pragma once
#include "Domain/Business/Clients/ClientPreferences.h"
#include "View/UiStyle.h"
#include <QComboBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QTableWidget>

namespace App::Views {
class ClientPreferencesForm : public QWidget {
  public:
    explicit ClientPreferencesForm(QWidget* parent) : QWidget(parent) {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(12);
        auto* form = new QFormLayout;
        terms_ = new QComboBox(this);
        terms_->setObjectName("client_payment_terms");
        terms_->setProperty("role", "input");
        terms_->addItem("Use business default", "");
        terms_->addItem("COD (due on invoice date)", "COD");
        terms_->addItem("14 days", "14");
        terms_->addItem("30 days", "30");
        auto* label = UiStyle::field_label("Payment terms", this);
        label->setBuddy(terms_);
        form->addRow(label, terms_);
        UiStyle::form(form);
        layout->addLayout(form);
        auto* heading = new QHBoxLayout;
        heading->setContentsMargins(0, 12, 0, 0);
        heading->addWidget(UiStyle::label("Customer pricing", this, "sectionTitle"));
        heading->addStretch();
        heading->addWidget(UiStyle::label("EXCLUDING GST", this, "pricingBadge"));
        layout->addLayout(heading);
        auto* help = UiStyle::label("Set a price for this customer, or leave it blank to use the default.", this, "body");
        help->setWordWrap(true);
        layout->addWidget(help);
        prices_ = new QTableWidget(0, 3, this);
        prices_->setObjectName("client_stock_prices");
        prices_->setAccessibleName("Customer stock prices");
        prices_->setHorizontalHeaderLabels({"Stock item", "Default price", "Customer price"});
        prices_->verticalHeader()->hide();
        prices_->verticalHeader()->setDefaultSectionSize(56);
        prices_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
        prices_->horizontalHeader()->setFixedHeight(44);
        prices_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        prices_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        prices_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
        prices_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
        prices_->setColumnWidth(1, 140);
        prices_->setColumnWidth(2, 180);
        prices_->horizontalHeaderItem(1)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        prices_->horizontalHeaderItem(2)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        prices_->setShowGrid(false);
        prices_->setAlternatingRowColors(true);
        prices_->setSelectionMode(QAbstractItemView::NoSelection);
        prices_->setFocusPolicy(Qt::NoFocus);
        prices_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        prices_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        prices_->setFixedHeight(102);
        layout->addWidget(prices_);
        status_ = UiStyle::label("Stock prices will load when you open the customer form.", this, "status");
        status_->setWordWrap(true);
        layout->addWidget(status_);
    }
    void set_values(const QJsonObject& record) {
        const auto index = terms_->findData(record.value("PaymentTerms").toString());
        terms_->setCurrentIndex(index < 0 ? 0 : index);
        saved_prices_ = record.value("StockPrices").toObject();
        for (int row = 0; row < prices_->rowCount(); ++row) {
            const auto price = saved_prices_.value(prices_->item(row, 0)->data(Qt::UserRole).toString());
            prices_->cellWidget(row, 2)->findChild<QLineEdit*>()->setText(
                price.isDouble() ? QLocale().toString(price.toDouble(), 'g', 15) : QString{});
        }
    }
    QJsonObject values() const {
        auto prices = saved_prices_;
        for (int row = 0; row < prices_->rowCount(); ++row) {
            const auto id = prices_->item(row, 0)->data(Qt::UserRole).toString();
            auto* edit = prices_->cellWidget(row, 2)->findChild<QLineEdit*>();
            if (edit->text().trimmed().isEmpty()) prices.remove(id);
            else {
                bool ok = false;
                const auto value = QLocale().toDouble(edit->text(), &ok);
                if (!ok || !edit->hasAcceptableInput()) throw std::invalid_argument("Enter a valid customer price or leave it blank for the stock default.");
                prices[id] = value;
            }
        }
        return Domain::Clients::preferences({{"PaymentTerms", terms_->currentData().toString()}, {"StockPrices", prices}});
    }
    void set_stock(const QJsonArray& stocks) {
        QMap<QString, QString> edits;
        for (int row = 0; row < prices_->rowCount(); ++row)
            edits[prices_->item(row, 0)->data(Qt::UserRole).toString()] = prices_->cellWidget(row, 2)->findChild<QLineEdit*>()->text();
        prices_->setRowCount(0);
        for (const auto& value : stocks) {
            const auto stock = value.toObject();
            const auto id = stock.value("_id").toString();
            if (id.isEmpty()) continue;
            const auto row = prices_->rowCount();
            prices_->insertRow(row);
            auto* name = new QTableWidgetItem(stock.value("Name").toString());
            name->setData(Qt::UserRole, id);
            prices_->setItem(row, 0, name);
            auto* default_price = new QTableWidgetItem(QLocale().toString(stock.value("Price").toDouble(), 'f', 2));
            default_price->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            prices_->setItem(row, 1, default_price);
            auto* cell = new QWidget(prices_);
            cell->setObjectName("client_stock_price_cell");
            auto* cell_layout = new QHBoxLayout(cell);
            cell_layout->setContentsMargins(0, 8, 0, 8);
            auto* edit = new QLineEdit(cell);
            cell_layout->addWidget(edit, 1, Qt::AlignVCenter);
            edit->setObjectName("client_stock_price");
            edit->setProperty("stock_id", id);
            edit->setProperty("role", "input");
            edit->setAccessibleName("Customer price for " + name->text());
            edit->setPlaceholderText("Use default");
            edit->setAlignment(Qt::AlignRight);
            edit->setToolTip("Leave blank to use the stock item's default price.");
            auto* validator = new QDoubleValidator(0, 1e9, 2, edit);
            validator->setNotation(QDoubleValidator::StandardNotation);
            edit->setValidator(validator);
            edit->setText(edits.contains(id) ? edits.value(id) : saved_prices_.contains(id)
                ? QLocale().toString(saved_prices_.value(id).toDouble(), 'g', 15) : QString{});
            prices_->setCellWidget(row, 2, cell);
        }
        prices_->setFixedHeight(46 + qBound(1, prices_->rowCount(), 5) * 56);
        prices_->setVisible(prices_->rowCount() > 0);
        UiStyle::message(status_, prices_->rowCount() == 0 ? "No saved stock yet. Create stock items, then edit this customer to set individual prices." : QString{});
    }
    void set_error(const QString& message) { UiStyle::message(status_, "Could not load stock prices: " + message + " Reopen this form to retry."); }
  private:
    QComboBox* terms_;
    QTableWidget* prices_;
    QLabel* status_;
    QJsonObject saved_prices_;
};
}

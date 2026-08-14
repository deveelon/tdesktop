/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "local_admin/local_admin.h"

#include "config.h"
#include "base/unixtime.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "window/window_session_controller.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
#include <QtCore/QUuid>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QRadioButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QTimeEdit>
#include <QtWidgets/QVBoxLayout>

#include <map>
#include <set>

namespace LocalAdmin {
namespace {

constexpr auto kRulesLimit = 100;
constexpr auto kNumberLimit = 100;
constexpr auto kTextLimit = 500;
constexpr auto kMessageLimit = 4096;
constexpr auto kDateLimit = 100;

struct ItemKey {
	uint64 account = 0;
	FullMsgId message;

	friend inline auto operator<=>(ItemKey, ItemKey) = default;
};

struct OneTimeRule {
	Rule rule;
	std::set<ItemKey> items;
};

[[nodiscard]] QString SettingsPath() {
	return cWorkingDir() + u"tdata/local-admin.json"_q;
}

[[nodiscard]] bool IsAsciiNumber(const QString &value) {
	if (value.isEmpty() || value.size() > kNumberLimit) {
		return false;
	}
	for (const auto character : value) {
		if (character < QChar('0') || character > QChar('9')) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] bool IsValidRule(const Rule &rule) {
	if (rule.source == rule.replacement) {
		return false;
	}
	if (rule.kind == RuleKind::Number) {
		return IsAsciiNumber(rule.source)
			&& IsAsciiNumber(rule.replacement);
	}
	return !rule.source.trimmed().isEmpty()
		&& !rule.replacement.trimmed().isEmpty()
		&& rule.source.size() <= kTextLimit
		&& rule.replacement.size() <= kTextLimit;
}

[[nodiscard]] int Replace(
		TextWithEntities &value,
		const QString &source,
		const QString &replacement) {
	if (source.isEmpty()) {
		return 0;
	}
	auto count = 0;
	auto from = 0;
	while ((from = value.text.indexOf(source, from, Qt::CaseSensitive)) >= 0) {
		const auto sourceEnd = from + source.size();
		const auto shift = replacement.size() - source.size();
		auto entities = EntitiesInText();
		entities.reserve(value.entities.size());
		for (const auto &entity : value.entities) {
			const auto entityFrom = entity.offset();
			const auto entityTill = entityFrom + entity.length();
			if (entityTill <= from) {
				entities.push_back(entity);
			} else if (entityFrom >= sourceEnd) {
				auto shifted = entity;
				shifted.shiftRight(shift);
				entities.push_back(std::move(shifted));
			} else if (entityFrom <= from && entityTill >= sourceEnd) {
				entities.push_back(EntityInText(
					entity.type(),
					entityFrom,
					entity.length() + shift,
					entity.data()));
			}
		}
		value.text.replace(from, source.size(), replacement);
		value.entities = std::move(entities);
		from += replacement.size();
		++count;
	}
	return count;
}

class Manager final {
public:
	static Manager &Instance() {
		static auto result = Manager();
		return result;
	}

	[[nodiscard]] bool enabled() const {
		return _enabled;
	}

	void setEnabled(bool enabled) {
		if (_enabled == enabled) {
			return;
		}
		_enabled = enabled;
		save();
	}

	[[nodiscard]] const std::vector<Rule> &rules() const {
		return _rules;
	}

	void addPersistent(Rule rule) {
		const auto sameSource = [&](const Rule &existing) {
			return existing.kind == rule.kind
				&& existing.source == rule.source;
		};
		const auto i = ranges::find_if(_rules, sameSource);
		if (i != end(_rules)) {
			rule.id = i->id;
			_rules.erase(i);
		}
		_rules.push_back(std::move(rule));
		if (_rules.size() > kRulesLimit) {
			_rules.erase(begin(_rules), end(_rules) - kRulesLimit);
		}
		save();
	}

	bool removePersistent(const QString &id) {
		const auto i = ranges::find(_rules, id, &Rule::id);
		if (i == end(_rules)) {
			return false;
		}
		_rules.erase(i);
		save();
		return true;
	}

	int addOneTime(
			const Rule &rule,
			not_null<const Main::Session*> session,
			const std::vector<not_null<HistoryItem*>> &items) {
		auto entry = OneTimeRule{ .rule = rule };
		auto count = 0;
		for (const auto item : items) {
			const auto key = ItemKey{ session->uniqueId(), item->fullId() };
			const auto source = item->translatedTextWithLocalEntities();
			auto value = resolveText(key, source);
			const auto replaced = Replace(
				value,
				rule.source,
				rule.replacement);
			if (replaced) {
				entry.items.emplace(key);
				count += replaced;
			}
		}
		if (!entry.items.empty()) {
			_oneTimeRules.push_back(std::move(entry));
		}
		return count;
	}

	void setMessage(
			not_null<const Main::Session*> session,
			FullMsgId id,
			QString text,
			QTime time) {
		const auto key = ItemKey{ session->uniqueId(), id };
		_messageText[key] = TextWithEntities::Simple(text);
		_messageTime[key] = time;
	}

	void setDateDivider(
			not_null<const Main::Session*> session,
			FullMsgId id,
			QString text) {
		_dateDividers[ItemKey{ session->uniqueId(), id }] = std::move(text);
	}

	[[nodiscard]] TextWithEntities resolveText(
			const ItemKey &key,
			const TextWithEntities &original,
			bool includeOneTime = true) {
		if (!_enabled) {
			return original;
		}
		const auto exact = _messageText.find(key);
		if (exact != end(_messageText)) {
			return exact->second;
		}
		auto result = original;
		for (const auto &rule : _rules) {
			Replace(result, rule.source, rule.replacement);
		}
		if (includeOneTime) {
			for (const auto &entry : _oneTimeRules) {
				if (entry.items.contains(key)) {
					Replace(
						result,
						entry.rule.source,
						entry.rule.replacement);
				}
			}
		}
		return result;
	}

	[[nodiscard]] QDateTime resolveDateTime(
			const ItemKey &key,
			QDateTime original) const {
		if (!_enabled) {
			return original;
		}
		const auto i = _messageTime.find(key);
		if (i != end(_messageTime)) {
			original.setTime(i->second);
		}
		return original;
	}

	[[nodiscard]] QString resolveDateDivider(const ItemKey &key) const {
		if (!_enabled) {
			return QString();
		}
		const auto i = _dateDividers.find(key);
		return (i == end(_dateDividers)) ? QString() : i->second;
	}

private:
	Manager() {
		load();
	}

	void load() {
		auto file = QFile(SettingsPath());
		if (!file.open(QIODevice::ReadOnly)) {
			return;
		}
		const auto document = QJsonDocument::fromJson(file.readAll());
		if (!document.isObject()) {
			return;
		}
		const auto object = document.object();
		_enabled = object.value(u"enabled"_q).toBool(true);
		const auto rules = object.value(u"rules"_q).toArray();
		for (const auto &value : rules) {
			const auto data = value.toObject();
			auto rule = Rule{
				.id = data.value(u"id"_q).toString(),
				.kind = (data.value(u"kind"_q).toString() == u"number"_q)
					? RuleKind::Number
					: RuleKind::Text,
				.source = data.value(u"source"_q).toString(),
				.replacement = data.value(u"replacement"_q).toString(),
			};
			if (rule.id.isEmpty()) {
				rule.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
			}
			if (IsValidRule(rule)) {
				_rules.push_back(std::move(rule));
			}
			if (_rules.size() == kRulesLimit) {
				break;
			}
		}
	}

	void save() const {
		QDir().mkpath(cWorkingDir() + u"tdata"_q);
		auto rules = QJsonArray();
		for (const auto &rule : _rules) {
			auto data = QJsonObject();
			data.insert(u"id"_q, rule.id);
			data.insert(
				u"kind"_q,
				(rule.kind == RuleKind::Number) ? u"number"_q : u"text"_q);
			data.insert(u"source"_q, rule.source);
			data.insert(u"replacement"_q, rule.replacement);
			rules.push_back(data);
		}
		auto object = QJsonObject();
		object.insert(u"version"_q, 1);
		object.insert(u"enabled"_q, _enabled);
		object.insert(u"rules"_q, rules);
		auto file = QFile(SettingsPath());
		if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
		}
	}

	bool _enabled = true;
	std::vector<Rule> _rules;
	std::vector<OneTimeRule> _oneTimeRules;
	std::map<ItemKey, TextWithEntities> _messageText;
	std::map<ItemKey, QTime> _messageTime;
	std::map<ItemKey, QString> _dateDividers;
};

[[nodiscard]] std::vector<not_null<HistoryItem*>> LoadedItems(
		History *history) {
	auto result = std::vector<not_null<HistoryItem*>>();
	if (!history) {
		return result;
	}
	for (const auto &block : history->blocks) {
		for (const auto &view : block->messages) {
			result.push_back(view->data());
		}
	}
	return result;
}

void RefreshItems(const std::vector<not_null<HistoryItem*>> &items) {
	for (const auto item : items) {
		item->history()->owner().requestItemTextRefresh(item);
		item->history()->owner().requestItemViewRefresh(item);
	}
}

class Panel final : public QDialog {
public:
	Panel(QWidget *parent, not_null<Window::SessionController*> controller)
	: QDialog(parent)
	, _controller(controller) {
		setAttribute(Qt::WA_DeleteOnClose);
		setWindowTitle(u"Локальная админ-панель"_q);
		build();
		refreshRules();
	}

	[[nodiscard]] Window::SessionController *controller() const {
		return _controller.get();
	}

private:
	[[nodiscard]] MainWidget *mainWidget() const {
		return _controller->widget()->sessionContent();
	}

	[[nodiscard]] MessageIdsList selectedIds() const {
		const auto main = mainWidget();
		return main ? main->selectedMessageIds() : MessageIdsList();
	}

	[[nodiscard]] std::vector<not_null<HistoryItem*>> loadedItems() const {
		const auto main = mainWidget();
		return LoadedItems(main ? main->shownHistory() : nullptr);
	}

	void status(const QString &text, bool error = false) {
		_status->setText(text);
		_status->setStyleSheet(error
			? u"color:#d14b4b;"_q
			: u"color:#4a8bc9;"_q);
	}

	void build() {
		auto root = new QVBoxLayout(this);
		auto intro = new QLabel(
			u"Локальные изменения интерфейса. Данные на серверах Telegram не меняются."_q,
			this);
		intro->setWordWrap(true);
		root->addWidget(intro);

		auto scroll = new QScrollArea(this);
		scroll->setWidgetResizable(true);
		auto body = new QWidget(scroll);
		auto layout = new QVBoxLayout(body);
		buildMode(layout, body);
		buildMessage(layout, body);
		buildDate(layout, body);
		buildRule(layout, body, RuleKind::Number);
		buildRule(layout, body, RuleKind::Text);
		buildRules(layout, body);
		layout->addStretch();
		scroll->setWidget(body);
		root->addWidget(scroll);

		_status = new QLabel(this);
		_status->setWordWrap(true);
		root->addWidget(_status);
	}

	void buildMode(not_null<QVBoxLayout*> layout, QWidget *parent) {
		auto group = new QGroupBox(u"Отображение"_q, parent);
		auto row = new QHBoxLayout(group);
		auto original = new QRadioButton(u"Оригинал"_q, group);
		auto local = new QRadioButton(u"Локальные изменения"_q, group);
		local->setChecked(Manager::Instance().enabled());
		original->setChecked(!Manager::Instance().enabled());
		row->addWidget(original);
		row->addWidget(local);
		connect(local, &QRadioButton::toggled, this, [=](bool checked) {
			Manager::Instance().setEnabled(checked);
			RefreshItems(loadedItems());
			status(checked
				? u"Локальные изменения включены."_q
				: u"Показан оригинал без локальных подмен."_q);
		});
		layout->addWidget(group);
	}

	void buildMessage(not_null<QVBoxLayout*> layout, QWidget *parent) {
		auto group = new QGroupBox(u"Изменение сообщения"_q, parent);
		auto form = new QFormLayout(group);
		auto load = new QPushButton(u"Загрузить выделенное сообщение"_q, group);
		_messageText = new QPlainTextEdit(group);
		_messageText->setEnabled(false);
		_messageTime = new QTimeEdit(group);
		_messageTime->setDisplayFormat(u"HH:mm"_q);
		_messageTime->setEnabled(false);
		auto apply = new QPushButton(u"Применить изменения"_q, group);
		apply->setEnabled(false);
		form->addRow(load);
		form->addRow(u"Текст сообщения"_q, _messageText);
		form->addRow(u"Время отправки"_q, _messageTime);
		form->addRow(apply);
		connect(load, &QPushButton::clicked, this, [=] {
			const auto ids = selectedIds();
			if (ids.size() != 1) {
				status(u"Выделите ровно одно сообщение в чате."_q, true);
				return;
			}
			const auto item = _controller->session().data().message(ids.front());
			if (!item || item->isService()) {
				status(u"Выбранное сообщение нельзя загрузить."_q, true);
				return;
			}
			_loadedMessage = ids.front();
			const auto source = item->translatedTextWithLocalEntities();
			_messageText->setPlainText(ResolveMessageText(
				&_controller->session(),
				item->fullId(),
				source).text);
			_messageTime->setTime(ResolveMessageDateTime(
				&_controller->session(),
				item->fullId(),
				base::unixtime::parse(item->date())).time());
			_messageText->setEnabled(true);
			_messageTime->setEnabled(true);
			apply->setEnabled(true);
			status(u"Сообщение загружено."_q);
		});
		connect(apply, &QPushButton::clicked, this, [=] {
			const auto text = _messageText->toPlainText();
			if (!_loadedMessage
				|| text.trimmed().isEmpty()
				|| text.size() > kMessageLimit) {
				status(u"Введите от 1 до 4096 символов."_q, true);
				return;
			}
			const auto item = _controller->session().data().message(_loadedMessage);
			if (!item) {
				status(u"Сообщение больше не загружено."_q, true);
				return;
			}
			Manager::Instance().setMessage(
				&_controller->session(),
				_loadedMessage,
				text,
				_messageTime->time());
			RefreshItems({ item });
			status(u"Текст и время сообщения изменены локально."_q);
		});
		layout->addWidget(group);
	}

	void buildDate(not_null<QVBoxLayout*> layout, QWidget *parent) {
		auto group = new QGroupBox(u"Дата выбранных сообщений"_q, parent);
		auto form = new QFormLayout(group);
		auto input = new QLineEdit(group);
		input->setPlaceholderText(u"Например: Сегодня или 5 августа"_q);
		auto apply = new QPushButton(u"Применить дату"_q, group);
		form->addRow(u"Новая дата"_q, input);
		form->addRow(apply);
		connect(apply, &QPushButton::clicked, this, [=] {
			const auto value = input->text().trimmed();
			const auto ids = selectedIds();
			if (ids.empty()) {
				status(u"Выделите одно или несколько сообщений."_q, true);
				return;
			} else if (value.isEmpty() || value.size() > kDateLimit) {
				status(u"Введите дату длиной от 1 до 100 символов."_q, true);
				return;
			}
			Manager::Instance().setDateDivider(
				&_controller->session(),
				ids.front(),
				value);
			if (const auto item = _controller->session().data().message(ids.front())) {
				RefreshItems({ item });
			}
			status(u"Дата применена к выбранной пачке сообщений."_q);
		});
		layout->addWidget(group);
	}

	void buildRule(
			not_null<QVBoxLayout*> layout,
			QWidget *parent,
			RuleKind kind) {
		const auto number = (kind == RuleKind::Number);
		auto group = new QGroupBox(
			number ? u"Замена чисел"_q : u"Замена текста"_q,
			parent);
		auto form = new QFormLayout(group);
		auto source = new QPlainTextEdit(group);
		auto replacement = new QPlainTextEdit(group);
		auto persistent = new QCheckBox(
			u"Применять при обновлении и после перезапуска"_q,
			group);
		auto apply = new QPushButton(
			number ? u"Применить замену чисел"_q : u"Применить замену текста"_q,
			group);
		form->addRow(number ? u"Исходное число"_q : u"Исходный текст"_q, source);
		form->addRow(u"Заменить на"_q, replacement);
		form->addRow(persistent);
		form->addRow(apply);
		connect(apply, &QPushButton::clicked, this, [=] {
			auto rule = Rule{
				.id = QUuid::createUuid().toString(QUuid::WithoutBraces),
				.kind = kind,
				.source = source->toPlainText(),
				.replacement = replacement->toPlainText(),
			};
			if (number) {
				rule.source = rule.source.trimmed();
				rule.replacement = rule.replacement.trimmed();
			}
			if (!IsValidRule(rule)) {
				status(number
					? u"Оба поля должны содержать от 1 до 100 цифр."_q
					: u"Оба поля должны содержать от 1 до 500 символов."_q,
					true);
				return;
			}
			const auto items = loadedItems();
			if (persistent->isChecked()) {
				Manager::Instance().addPersistent(std::move(rule));
				RefreshItems(items);
				refreshRules();
				status(u"Постоянное правило сохранено и применено."_q);
			} else {
				const auto count = Manager::Instance().addOneTime(
					rule,
					&_controller->session(),
					items);
				RefreshItems(items);
				status(count
					? u"Одноразовая замена применена: %1."_q.arg(count)
					: u"Совпадений в открытом чате не найдено."_q);
			}
		});
		layout->addWidget(group);
	}

	void buildRules(not_null<QVBoxLayout*> layout, QWidget *parent) {
		auto group = new QGroupBox(u"Постоянные правила"_q, parent);
		auto column = new QVBoxLayout(group);
		_rules = new QListWidget(group);
		auto remove = new QPushButton(u"Удалить выбранное правило"_q, group);
		column->addWidget(_rules);
		column->addWidget(remove);
		connect(remove, &QPushButton::clicked, this, [=] {
			const auto item = _rules->currentItem();
			if (!item) {
				status(u"Сначала выберите правило."_q, true);
				return;
			}
			if (Manager::Instance().removePersistent(
					item->data(Qt::UserRole).toString())) {
				RefreshItems(loadedItems());
				refreshRules();
				status(u"Постоянное правило удалено."_q);
			}
		});
		layout->addWidget(group);
	}

	void refreshRules() {
		_rules->clear();
		for (const auto &rule : Manager::Instance().rules()) {
			auto item = new QListWidgetItem(
				((rule.kind == RuleKind::Number) ? u"Число: "_q : u"Текст: "_q)
					+ rule.source
					+ u"  →  "_q
					+ rule.replacement,
				_rules);
			item->setData(Qt::UserRole, rule.id);
		}
	}

	const not_null<Window::SessionController*> _controller;
	QPlainTextEdit *_messageText = nullptr;
	QTimeEdit *_messageTime = nullptr;
	QListWidget *_rules = nullptr;
	QLabel *_status = nullptr;
	FullMsgId _loadedMessage;
};

QPointer<Panel> PanelInstance;

} // namespace

TextWithEntities ResolveMessageText(
		not_null<const Main::Session*> session,
		FullMsgId id,
		const TextWithEntities &original) {
	return Manager::Instance().resolveText(
		ItemKey{ session->uniqueId(), id },
		original);
}

QDateTime ResolveMessageDateTime(
		not_null<const Main::Session*> session,
		FullMsgId id,
		QDateTime original) {
	return Manager::Instance().resolveDateTime(
		ItemKey{ session->uniqueId(), id },
		original);
}

QString ResolveDateDivider(
		not_null<const Main::Session*> session,
		FullMsgId id) {
	return Manager::Instance().resolveDateDivider(
		ItemKey{ session->uniqueId(), id });
}

void Show(not_null<Window::SessionController*> controller) {
	if (PanelInstance && PanelInstance->controller() != controller.get()) {
		PanelInstance->close();
	}
	if (!PanelInstance) {
		PanelInstance = new Panel(controller->widget().get(), controller);
	}
	PanelInstance->show();
	PanelInstance->raise();
	PanelInstance->activateWindow();
}

} // namespace LocalAdmin

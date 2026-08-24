/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_msg_id.h"
#include "local_admin/local_admin_ui.h"
#include "ui/text/text_entity.h"

#include <QtCore/QDateTime>

#include <map>
#include <set>
#include <vector>

class History;
class HistoryItem;

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace LocalAdmin {

enum class RuleKind {
	Number,
	Text,
};

struct Rule {
	QString id;
	RuleKind kind = RuleKind::Text;
	QString source;
	QString replacement;

	friend inline bool operator==(const Rule &, const Rule &) = default;
};

struct ExportSnapshot {
	struct OneTimeRule {
		Rule rule;
		std::set<FullMsgId> messages;
	};

	std::vector<Rule> rules;
	std::vector<OneTimeRule> oneTimeRules;
	std::map<FullMsgId, QString> messageText;
	std::map<FullMsgId, QTime> messageTime;
	std::map<FullMsgId, QString> dateDividers;
};

[[nodiscard]] TextWithEntities ResolveMessageText(
	not_null<const Main::Session*> session,
	FullMsgId id,
	const TextWithEntities &original);
[[nodiscard]] QDateTime ResolveMessageDateTime(
	not_null<const Main::Session*> session,
	FullMsgId id,
	QDateTime original);
[[nodiscard]] QString ResolveDateDivider(
	not_null<const Main::Session*> session,
	FullMsgId id);
[[nodiscard]] bool ServiceMessageSelectionEnabled();
[[nodiscard]] ExportSnapshot CreateExportSnapshot(
	not_null<const Main::Session*> session);

void Show(not_null<Window::SessionController*> controller);

} // namespace LocalAdmin

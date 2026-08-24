/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_peer_id.h"

#include <QtCore/QString>
#include <QtCore/QTime>

#include <map>
#include <set>
#include <vector>

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

struct ExportMessageId {
	PeerId peer = 0;
	int64 message = 0;

	friend inline auto operator<=>(ExportMessageId, ExportMessageId) = default;
};

struct ExportSnapshot {
	struct OneTimeRule {
		Rule rule;
		std::set<ExportMessageId> messages;
	};

	std::vector<Rule> rules;
	std::vector<OneTimeRule> oneTimeRules;
	std::map<ExportMessageId, QString> messageText;
	std::map<ExportMessageId, QTime> messageTime;
	std::map<ExportMessageId, QString> dateDividers;
};

} // namespace LocalAdmin

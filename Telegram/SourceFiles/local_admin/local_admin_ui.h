/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "rpl/producer.h"
#include "ui/text/text_entity.h"

namespace Ui {
struct StringWithNumbers;
} // namespace Ui

namespace LocalAdmin {

[[nodiscard]] QString ResolveUiText(QString original);
[[nodiscard]] TextWithEntities ResolveUiText(TextWithEntities original);
[[nodiscard]] Ui::StringWithNumbers ResolveUiText(
	Ui::StringWithNumbers original);

template <typename Value>
[[nodiscard]] Value ResolveUiText(Value original) {
	return original;
}

[[nodiscard]] rpl::producer<> Changes();

} // namespace LocalAdmin

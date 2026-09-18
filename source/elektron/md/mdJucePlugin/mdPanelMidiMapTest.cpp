#include "mdPanelMidiMap.h"

#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace
{
	using namespace mdJucePlugin::panelMidi;

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	Map makeMap(const EncoderMode _mode, const uint8_t _channel = 0)
	{
		Map map;
		Settings settings;
		settings.encoderMode = _mode;
		settings.channel = _channel;
		map.setSettings(settings);
		return map;
	}

	void testAbsoluteEncoder()
	{
		auto map = makeMap(EncoderMode::Absolute);

		// The first value only anchors the controller position.
		require(!map.translate(0xb0, 16, 100), "first absolute value must not move the encoder");

		auto a = map.translate(0xb0, 16, 103);
		require(a && a->kind == Action::Kind::EncoderSteps && a->steps == 3
			&& a->encoder == md::PanelEncoder::DataEntryA, "absolute +3");

		a = map.translate(0xb0, 16, 98);
		require(a && a->steps == -5, "absolute -5");

		require(!map.translate(0xb0, 16, 98), "unchanged absolute value is silent");

		// Each controller number tracks its own history.
		require(!map.translate(0xb0, 17, 10), "encoder B anchors independently");
		a = map.translate(0xb0, 17, 12);
		require(a && a->steps == 2 && a->encoder == md::PanelEncoder::DataEntryB, "encoder B +2");

		// Level and sound selection follow encoder H.
		require(!map.translate(0xb0, 24, 0), "level anchors");
		a = map.translate(0xb0, 24, 1);
		require(a && a->encoder == md::PanelEncoder::Level, "CC 24 is Level");
		require(!map.translate(0xb0, 25, 0), "sound anchors");
		a = map.translate(0xb0, 25, 1);
		require(a && a->encoder == md::PanelEncoder::SoundSelection, "CC 25 is Sound selection");

		// A reset (controller or mode change) re-anchors instead of jumping.
		map.reset();
		require(!map.translate(0xb0, 16, 5), "re-anchors after reset");
	}

	void testRelativeEncoders()
	{
		auto offset = makeMap(EncoderMode::RelativeOffset);
		require(!offset.translate(0xb0, 20, 64), "offset 64 is still");
		auto a = offset.translate(0xb0, 20, 66);
		require(a && a->steps == 2 && a->encoder == md::PanelEncoder::DataEntryE, "offset +2");
		a = offset.translate(0xb0, 20, 63);
		require(a && a->steps == -1, "offset -1");

		auto twos = makeMap(EncoderMode::RelativeTwosComplement);
		a = twos.translate(0xb0, 16, 3);
		require(a && a->steps == 3, "two's complement +3");
		a = twos.translate(0xb0, 16, 127);
		require(a && a->steps == -1, "two's complement -1");
		a = twos.translate(0xb0, 16, 125);
		require(a && a->steps == -3, "two's complement -3");
		require(!twos.translate(0xb0, 16, 0), "two's complement 0 is still");
		require(!twos.translate(0xb0, 16, 64), "two's complement 64 is treated as still");
	}

	void testTriggerNotes()
	{
		auto map = makeMap(EncoderMode::Absolute);

		auto a = map.translate(0x90, 36, 100);
		require(a && a->kind == Action::Kind::ButtonDown && a->control == md::PanelControl::Trigger1,
			"note 36 down is trig 1");
		a = map.translate(0x90, 51, 1);
		require(a && a->kind == Action::Kind::ButtonDown && a->control == md::PanelControl::Trigger16,
			"note 51 down is trig 16");
		a = map.translate(0x90, 36, 0);
		require(a && a->kind == Action::Kind::ButtonUp && a->control == md::PanelControl::Trigger1,
			"note-on velocity 0 is release");
		a = map.translate(0x80, 40, 64);
		require(a && a->kind == Action::Kind::ButtonUp && a->control == md::PanelControl::Trigger5,
			"note-off is release");

		require(!map.translate(0x90, 35, 100), "note below range is unmapped");
		require(!map.translate(0x90, 52, 100), "note above range is unmapped");
	}

	void testButtonControllers()
	{
		auto map = makeMap(EncoderMode::Absolute);

		auto a = map.translate(0xb0, 26, 127);
		require(a && a->kind == Action::Kind::ButtonDown && a->control == md::PanelControl::Track1,
			"CC 26 down is track 1");
		a = map.translate(0xb0, 26, 0);
		require(a && a->kind == Action::Kind::ButtonUp && a->control == md::PanelControl::Track1,
			"CC 26 up is track 1");
		a = map.translate(0xb0, 63, 64);
		require(!a, "CC beyond the button table is unmapped");

		const auto last = static_cast<uint8_t>(g_firstButtonController + std::size(g_buttonControllers) - 1);
		a = map.translate(0xb0, last, 64);
		require(a && a->control == md::PanelControl::ClassicExtended && a->kind == Action::Kind::ButtonDown,
			"last table entry is reachable, threshold is 64");
		a = map.translate(0xb0, last, 63);
		require(a && a->kind == Action::Kind::ButtonUp, "63 releases");
		require(!map.translate(0xb0, static_cast<uint8_t>(last + 1), 127), "one past the table is unmapped");
	}

	void testEveryControlReachedOnce()
	{
		// Every panel control must be reachable exactly once, by note or by CC.
		bool seen[64] = {};
		for(uint8_t i = 0; i < g_triggerCount; ++i)
			seen[static_cast<uint8_t>(md::PanelControl::Trigger1) + i] = true;
		for(const auto control : g_buttonControllers)
		{
			auto& flag = seen[static_cast<uint8_t>(control)];
			require(!flag, std::string("duplicate mapping for ") + md::panelControlName(control));
			flag = true;
		}
		for(uint8_t i = 0; i <= static_cast<uint8_t>(md::PanelControl::ClassicExtended); ++i)
			require(seen[i], std::string("unmapped control ")
				+ md::panelControlName(static_cast<md::PanelControl>(i)));
	}

	void testChannelFilter()
	{
		auto omni = makeMap(EncoderMode::RelativeOffset, 0);
		require(omni.translate(0xb0, 16, 66).has_value() && omni.translate(0xbf, 16, 66).has_value(), "omni accepts every channel");

		auto ch3 = makeMap(EncoderMode::RelativeOffset, 3);
		require(!ch3.translate(0xb0, 16, 66), "channel 3 rejects channel 1");
		require(ch3.translate(0xb2, 16, 66).has_value(), "channel 3 accepts channel 3");
		require(ch3.translate(0x92, 36, 100).has_value(), "channel filter applies to notes");
		require(!ch3.translate(0x93, 36, 100), "channel filter rejects other note channels");
	}

	void testIgnoredMessages()
	{
		auto map = makeMap(EncoderMode::RelativeOffset);
		require(!map.translate(0xc0, 5, 0), "program change is not a panel message");
		require(!map.translate(0xe0, 0, 64), "pitch bend is not a panel message");
		require(!map.translate(0xf8, 0, 0), "realtime clock is ignored");
		require(!map.translate(0x10, 16, 66), "data byte as status is ignored");
		require(!map.translate(0xb0, 15, 66), "CC below the encoder range is unmapped");
		require(!map.translate(0xb0, 26 + 31, 127), "CC above the button range is unmapped");
	}
}

int main()
{
	try
	{
		testAbsoluteEncoder();
		testRelativeEncoders();
		testTriggerNotes();
		testButtonControllers();
		testEveryControlReachedOnce();
		testChannelFilter();
		testIgnoredMessages();
	}
	catch(const std::exception& _e)
	{
		std::cerr << "mdPanelMidiMapTest failed: " << _e.what() << '\n';
		return 1;
	}
	std::cout << "mdPanelMidiMapTest passed\n";
	return 0;
}

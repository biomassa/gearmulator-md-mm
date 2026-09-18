#pragma once

#include <cstdint>
#include <optional>

#include "mdLib/mdpanel.h"

namespace mdJucePlugin::panelMidi
{
	// Translates MIDI from a controller into front-panel actions. This is separate
	// from the MIDI that reaches the emulated firmware: nothing here is forwarded
	// to the device, it only drives the panel like a person would.
	//
	// Default map:
	//   CC 16-23  encoders A-H         CC 24  Level        CC 25  Sound selection
	//   Notes 36-51  trig keys 1-16    (note-on = down, note-off / velocity 0 = up)
	//   CC 26-56  buttons, see g_buttonControllers (value >= 64 = down, else up)

	enum class EncoderMode : uint8_t
	{
		Absolute,				// knob/pot: the change since the last value turns the encoder
		RelativeOffset,			// 64 = still, 65 = +1, 63 = -1
		RelativeTwosComplement	// 1..63 = +n, 127..65 = -n
	};

	struct Settings
	{
		uint8_t channel = 0;	// 0 = omni, 1..16 = only that channel
		EncoderMode encoderMode = EncoderMode::Absolute;
	};

	struct Action
	{
		enum class Kind : uint8_t
		{
			EncoderSteps,
			ButtonDown,
			ButtonUp
		};

		Kind kind = Kind::EncoderSteps;
		md::PanelEncoder encoder = md::PanelEncoder::DataEntryA;
		md::PanelControl control = md::PanelControl::Trigger1;
		int steps = 0;	// signed detents, EncoderSteps only

		constexpr bool operator==(const Action& _other) const
		{
			return kind == _other.kind && encoder == _other.encoder
				&& control == _other.control && steps == _other.steps;
		}
	};

	constexpr uint8_t g_firstEncoderController = 16;
	constexpr uint8_t g_encoderCount = 10;	// A-H, Level, Sound selection
	constexpr uint8_t g_firstTriggerNote = 36;
	constexpr uint8_t g_triggerCount = 16;
	constexpr uint8_t g_firstButtonController = 26;

	// Controls addressed by CC, starting at g_firstButtonController.
	constexpr md::PanelControl g_buttonControllers[] =
	{
		md::PanelControl::Track1, md::PanelControl::Track2, md::PanelControl::Track3,
		md::PanelControl::Track4, md::PanelControl::Track5, md::PanelControl::Track6,
		md::PanelControl::Function, md::PanelControl::Kit,
		md::PanelControl::Enter, md::PanelControl::Exit,
		md::PanelControl::Up, md::PanelControl::Down,
		md::PanelControl::Left, md::PanelControl::Right,
		md::PanelControl::Play, md::PanelControl::Stop, md::PanelControl::Record,
		md::PanelControl::Tempo, md::PanelControl::SynthesisEffectsRouting,
		md::PanelControl::PatternSong, md::PanelControl::Scale,
		md::PanelControl::TrigSelect, md::PanelControl::SongEnable,
		md::PanelControl::DataPageForward, md::PanelControl::DataPageBackward,
		md::PanelControl::BankGroup,
		md::PanelControl::BankA, md::PanelControl::BankB,
		md::PanelControl::BankC, md::PanelControl::BankD,
		md::PanelControl::ClassicExtended,
	};

	class Map
	{
	public:
		void setSettings(const Settings& _settings);
		const Settings& getSettings() const { return m_settings; }

		// Forgets absolute-mode history. The next absolute value only re-anchors,
		// so switching controller or mode cannot make the encoders jump.
		void reset();

		// One raw channel-voice message. Returns nothing for messages that are
		// filtered out or not mapped.
		std::optional<Action> translate(uint8_t _status, uint8_t _data1, uint8_t _data2);

	private:
		std::optional<Action> translateController(uint8_t _controller, uint8_t _value);

		Settings m_settings;
		int16_t m_lastAbsolute[g_encoderCount] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
	};
}

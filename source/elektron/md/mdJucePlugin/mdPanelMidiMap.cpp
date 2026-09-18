#include "mdPanelMidiMap.h"

#include <iterator>

namespace mdJucePlugin::panelMidi
{
	namespace
	{
		constexpr uint8_t g_statusNoteOff = 0x80;
		constexpr uint8_t g_statusNoteOn = 0x90;
		constexpr uint8_t g_statusController = 0xb0;

		Action encoderAction(const uint8_t _index, const int _steps)
		{
			Action a;
			a.kind = Action::Kind::EncoderSteps;
			a.encoder = static_cast<md::PanelEncoder>(_index);
			a.steps = _steps;
			return a;
		}

		Action buttonAction(const md::PanelControl _control, const bool _down)
		{
			Action a;
			a.kind = _down ? Action::Kind::ButtonDown : Action::Kind::ButtonUp;
			a.control = _control;
			return a;
		}
	}

	void Map::setSettings(const Settings& _settings)
	{
		m_settings = _settings;
		reset();
	}

	void Map::reset()
	{
		for(auto& last : m_lastAbsolute)
			last = -1;
	}

	std::optional<Action> Map::translate(const uint8_t _status, const uint8_t _data1, const uint8_t _data2)
	{
		if(_status < 0x80 || _status >= 0xf0)
			return std::nullopt;

		if(m_settings.channel != 0 && (_status & 0x0f) != m_settings.channel - 1)
			return std::nullopt;

		switch(_status & 0xf0)
		{
		case g_statusNoteOn:
		case g_statusNoteOff:
			{
				if(_data1 < g_firstTriggerNote || _data1 >= g_firstTriggerNote + g_triggerCount)
					return std::nullopt;
				const auto control = static_cast<md::PanelControl>(
					static_cast<uint8_t>(md::PanelControl::Trigger1) + (_data1 - g_firstTriggerNote));
				const bool down = (_status & 0xf0) == g_statusNoteOn && _data2 != 0;
				return buttonAction(control, down);
			}
		case g_statusController:
			return translateController(_data1, _data2);
		default:
			return std::nullopt;
		}
	}

	std::optional<Action> Map::translateController(const uint8_t _controller, const uint8_t _value)
	{
		if(_controller >= g_firstEncoderController && _controller < g_firstEncoderController + g_encoderCount)
		{
			const auto index = static_cast<uint8_t>(_controller - g_firstEncoderController);
			int steps = 0;

			switch(m_settings.encoderMode)
			{
			case EncoderMode::Absolute:
				{
					const auto last = m_lastAbsolute[index];
					m_lastAbsolute[index] = _value;
					// The first value only anchors: the controller may sit anywhere.
					if(last >= 0)
						steps = static_cast<int>(_value) - last;
				}
				break;
			case EncoderMode::RelativeOffset:
				steps = static_cast<int>(_value) - 64;
				break;
			case EncoderMode::RelativeTwosComplement:
				if(_value < 64)
					steps = _value;
				else if(_value > 64)
					steps = static_cast<int>(_value) - 128;
				break;
			}

			if(steps == 0)
				return std::nullopt;
			return encoderAction(index, steps);
		}

		if(_controller >= g_firstButtonController
			&& _controller < g_firstButtonController + std::size(g_buttonControllers))
			return buttonAction(g_buttonControllers[_controller - g_firstButtonController], _value >= 64);

		return std::nullopt;
	}
}

#include "mdPanelMidiController.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using namespace mdJucePlugin::panelMidi;

	constexpr auto g_md = md::MachineModel::Machinedrum;
	constexpr auto g_mm = md::MachineModel::Monomachine;

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	struct Fixture
	{
		explicit Fixture(const md::MachineModel _model, const std::string& _file = {})
			: controller(_model, _file, [this](const Action& _action) { actions.push_back(_action); })
		{
		}

		void send(const uint8_t _status, const uint8_t _d1, const uint8_t _d2)
		{
			controller.handleMessage(RawMessage{ _status, _d1, _d2 });
		}

		std::vector<Action> actions;
		Controller controller;
	};

	std::string tempFile(const std::string& _name)
	{
		// std::filesystem needs macOS 10.15, above the project's deployment target.
		for(const auto* const variable : { "TMPDIR", "TEMP", "TMP" })
			if(const auto* const folder = std::getenv(variable))
			{
				std::string path = folder;
				if(!path.empty() && path.back() != '/' && path.back() != '\\')
					path += '/';
				return path + "mdPanelMidiControllerTest_" + _name + ".txt";
			}
		return "mdPanelMidiControllerTest_" + _name + ".txt";
	}

	Source buttonSource(const Controller& _c, const md::PanelControl _control)
	{
		return _c.getTable().buttons[static_cast<size_t>(_control)];
	}

	Source encoderSource(const Controller& _c, const md::PanelEncoder _encoder)
	{
		return _c.getTable().encoders[static_cast<size_t>(_encoder)].source;
	}

	void testDispatch()
	{
		Fixture f(g_md);
		f.send(0xb0, 65, 127);
		require(f.actions.size() == 1 && f.actions[0].kind == Action::Kind::ButtonDown
			&& f.actions[0].control == md::PanelControl::Trigger1, "CC drives trig 1");
		f.send(0xb0, 30, 127);	// unbound on the MD
		require(f.actions.size() == 1, "unbound message does nothing");
		require(f.controller.getMessageSerial() == 2, "serial counts every message");
		require(describe(f.controller.getLastMessage()) == "CC 30 = 127 (ch 1)", "last message is kept");
	}

	void testLearnButtonFromNote()
	{
		Fixture f(g_md);
		f.controller.beginLearn(md::PanelControl::Play);
		require(f.controller.getLearnTarget().is(md::PanelControl::Play), "learning Play");

		f.send(0x80, 70, 0);	// a note-off cannot be learned
		require(f.controller.getLearnTarget().active(), "note-off keeps waiting");
		f.send(0x90, 70, 100);
		require(!f.controller.getLearnTarget().active(), "learning ends");
		require(buttonSource(f.controller, md::PanelControl::Play) == Source{ Source::Kind::Note, 70 }, "Play is note 70");
		require(f.actions.empty(), "learning does not drive the panel");

		f.send(0x90, 70, 100);
		require(f.actions.size() == 1 && f.actions[0].control == md::PanelControl::Play, "the new binding works");
	}

	void testLearnButtonFromController()
	{
		Fixture f(g_md);
		f.controller.beginLearn(md::PanelControl::Stop);
		f.send(0xb0, 90, 0);	// any value: a button controller may start at 0
		require(buttonSource(f.controller, md::PanelControl::Stop) == Source{ Source::Kind::Controller, 90 }, "Stop is CC 90");
	}

	void testLearnEncoder()
	{
		Fixture f(g_md);
		f.controller.beginLearn(md::PanelEncoder::DataEntryB);
		f.send(0x90, 50, 100);	// a note cannot drive an encoder
		require(f.controller.getLearnTarget().is(md::PanelEncoder::DataEntryB), "note ignored while learning an encoder");
		f.send(0xb0, 77, 20);
		require(encoderSource(f.controller, md::PanelEncoder::DataEntryB) == Source{ Source::Kind::Controller, 77 }, "encoder B is CC 77");
		require(f.controller.getTable().encoders[1].mode == EncoderMode::Absolute, "the mode is kept");
	}

	void testLearnMovesAnExistingSource()
	{
		Fixture f(g_md);
		// Trig 2 is CC 66 by default; learning trig 1 onto CC 66 takes it over.
		f.controller.beginLearn(md::PanelControl::Trigger1);
		f.send(0xb0, 66, 127);
		require(buttonSource(f.controller, md::PanelControl::Trigger1) == Source{ Source::Kind::Controller, 66 }, "trig 1 got CC 66");
		require(!buttonSource(f.controller, md::PanelControl::Trigger2).valid(), "trig 2 lost it");

		// A controller used by an encoder moves to a button too.
		f.controller.beginLearn(md::PanelControl::Play);
		f.send(0xb0, 16, 127);
		require(buttonSource(f.controller, md::PanelControl::Play) == Source{ Source::Kind::Controller, 16 }, "Play got CC 16");
		require(!encoderSource(f.controller, md::PanelEncoder::DataEntryA).valid(), "encoder A lost CC 16");
	}

	void testLearnPush()
	{
		Fixture f(g_md);
		f.controller.beginLearnPush(md::PanelEncoder::DataEntryB);
		require(f.controller.getLearnTarget().isPush(md::PanelEncoder::DataEntryB), "learning B's push");
		require(!f.controller.getLearnTarget().is(md::PanelEncoder::DataEntryB), "not B's turning");

		f.send(0x80, 50, 0);	// a note-off cannot be learned
		require(f.controller.getLearnTarget().active(), "note-off keeps waiting");
		f.send(0xb0, 80, 127);
		require(!f.controller.getLearnTarget().active(), "learning ends");
		require(f.controller.getTable().pushes[1] == Source{ Source::Kind::Controller, 80 }, "B's push is CC 80");
		require(f.controller.getTable().encoders[1].source == Source{ Source::Kind::Controller, 17 }, "turning B is untouched");
		require(f.actions.empty(), "learning does not drive the panel");

		f.send(0xb0, 80, 127);
		require(f.actions.size() == 1 && f.actions[0].kind == Action::Kind::EncoderPushDown
			&& f.actions[0].encoder == md::PanelEncoder::DataEntryB, "the new push works");
		f.send(0xb0, 80, 0);
		require(f.actions.size() == 2 && f.actions[1].kind == Action::Kind::EncoderPushUp, "and lets go");

		// A note works too.
		f.controller.beginLearnPush(md::PanelEncoder::DataEntryC);
		f.send(0x90, 70, 100);
		require(f.controller.getTable().pushes[2] == Source{ Source::Kind::Note, 70 }, "C's push is note 70");

		// A source another control had moves to the push.
		f.controller.beginLearnPush(md::PanelEncoder::DataEntryE);
		f.send(0xb0, 65, 127);	// trig 1's CC
		require(f.controller.getTable().pushes[4] == Source{ Source::Kind::Controller, 65 }, "E's push is CC 65");
		require(!buttonSource(f.controller, md::PanelControl::Trigger1).valid(), "trig 1 lost CC 65");

		// A controller used by an encoder's turning moves to the push.
		f.controller.beginLearnPush(md::PanelEncoder::DataEntryD);
		f.send(0xb0, 16, 127);
		require(f.controller.getTable().pushes[3] == Source{ Source::Kind::Controller, 16 }, "D's push is CC 16");
		require(!encoderSource(f.controller, md::PanelEncoder::DataEntryA).valid(), "encoder A lost CC 16");

		f.controller.clearPush(md::PanelEncoder::DataEntryD);
		require(!f.controller.getTable().pushes[3].valid(), "push cleared");

		// Level and Sound selection cannot be pressed.
		f.controller.beginLearnPush(md::PanelEncoder::Level);
		require(!f.controller.getLearnTarget().active(), "Level has no push switch");
		f.controller.beginLearnPush(md::PanelEncoder::SoundSelection);
		require(!f.controller.getLearnTarget().active(), "Sound selection has no push switch");
	}

	void testHeldStepWhileTurning()
	{
		// p-lock gesture end to end: hold a trig, turn an encoder, release.
		Fixture f(g_md);
		f.controller.setEncoderMode(md::PanelEncoder::DataEntryA, EncoderMode::RelativeOffset);
		f.send(0xb0, 65, 127);
		f.send(0xb0, 16, 65);
		f.send(0xb0, 57, 127);	// and press A while at it
		f.send(0xb0, 57, 0);
		f.send(0xb0, 65, 0);
		require(f.actions.size() == 5, "five actions");
		require(f.actions[0].kind == Action::Kind::ButtonDown, "step held first");
		require(f.actions[1].kind == Action::Kind::EncoderSteps && f.actions[1].steps == 1, "turned while held");
		require(f.actions[2].kind == Action::Kind::EncoderPushDown, "pushed while held");
		require(f.actions[3].kind == Action::Kind::EncoderPushUp, "push released");
		require(f.actions[4].kind == Action::Kind::ButtonUp, "step released last");
	}

	void testLearnHonoursTheChannel()
	{
		Fixture f(g_md);
		f.controller.setChannel(3);
		f.controller.beginLearn(md::PanelControl::Play);
		f.send(0x90, 70, 100);	// channel 1
		require(f.controller.getLearnTarget().active(), "another channel is not learned");
		f.send(0x92, 70, 100);	// channel 3
		require(!f.controller.getLearnTarget().active(), "the selected channel is learned");
	}

	void testCancelAndClear()
	{
		Fixture f(g_md);
		const auto revision = f.controller.getRevision();
		f.controller.beginLearn(md::PanelControl::Play);
		require(f.controller.getRevision() != revision, "learn changes the revision");
		f.controller.cancelLearn();
		require(!f.controller.getLearnTarget().active(), "cancelled");
		f.send(0xb0, 65, 127);
		require(f.actions.size() == 1, "messages drive the panel again after cancel");

		f.controller.clear(md::PanelControl::Trigger1);
		require(!buttonSource(f.controller, md::PanelControl::Trigger1).valid(), "cleared");
		f.send(0xb0, 65, 127);
		require(f.actions.size() == 1, "a cleared control no longer responds");

		f.controller.clear(md::PanelEncoder::DataEntryA);
		require(!encoderSource(f.controller, md::PanelEncoder::DataEntryA).valid(), "encoder cleared");
	}

	void testOnlyAvailableControlsCanBeLearned()
	{
		Fixture f(g_md);
		f.controller.beginLearn(md::PanelControl::Track1);	// the MD has no track buttons
		require(!f.controller.getLearnTarget().active(), "a control the machine lacks cannot be learned");

		Fixture mmFixture(g_mm);
		mmFixture.controller.beginLearn(md::PanelEncoder::SoundSelection);
		require(!mmFixture.controller.getLearnTarget().active(), "the MM has no sound selection encoder");
		mmFixture.controller.setEncoderMode(md::PanelEncoder::SoundSelection, EncoderMode::RelativeOffset);
		require(mmFixture.controller.getTable().encoders[static_cast<size_t>(md::PanelEncoder::SoundSelection)].mode
			== EncoderMode::Absolute, "its mode cannot be changed either");
	}

	void testModeAndReset()
	{
		Fixture f(g_md);
		f.controller.setEncoderMode(md::PanelEncoder::DataEntryA, EncoderMode::RelativeOffset);
		f.send(0xb0, 16, 66);
		require(f.actions.size() == 1 && f.actions[0].steps == 2, "relative mode applies at once");

		f.controller.resetToDefault();
		require(f.controller.getTable() == makeDefaultTable(g_md), "reset restores the factory map");
	}

	void testPersistence()
	{
		const auto path = tempFile("persist");
		std::remove(path.c_str());

		{
			Fixture f(g_md, path);
			f.controller.setChannel(7);
			f.controller.beginLearn(md::PanelControl::Play);
			f.send(0x96, 88, 100);	// channel 7 is status 0x96
			f.controller.setEncoderMode(md::PanelEncoder::DataEntryC, EncoderMode::RelativeTwosComplement);
			f.controller.beginLearnPush(md::PanelEncoder::DataEntryE);
			f.send(0x96, 91, 100);
		}
		{
			Fixture f(g_md, path);
			require(f.controller.getTable().channel == 7, "channel restored");
			require(buttonSource(f.controller, md::PanelControl::Play) == Source{ Source::Kind::Note, 88 }, "learned binding restored");
			require(f.controller.getTable().encoders[2].mode == EncoderMode::RelativeTwosComplement, "mode restored");
			require(f.controller.getTable().pushes[4] == Source{ Source::Kind::Note, 91 }, "learned push restored");
		}

		// A damaged file falls back to the factory map instead of leaving the panel unbound.
		{
			std::ofstream(path, std::ios::trunc) << "this is not a panel map\n";
			Fixture f(g_md, path);
			require(f.controller.getTable() == makeDefaultTable(g_md), "garbage file falls back to the default map");
		}
		{
			Fixture f(g_md, tempFile("does_not_exist"));
			require(f.controller.getTable() == makeDefaultTable(g_md), "missing file uses the default map");
		}

		std::remove(path.c_str());
	}

	void testModelsKeepSeparateFiles()
	{
		const auto path = tempFile("shared_name");
		std::remove(path.c_str());
		{
			Fixture mmFixture(g_mm, path);
			mmFixture.controller.beginLearn(md::PanelControl::Track1);
			mmFixture.send(0xb0, 99, 127);
		}
		{
			// Same file read as an MD: the MM-only Track binding is dropped.
			Fixture mdFixture(g_md, path);
			require(!buttonSource(mdFixture.controller, md::PanelControl::Track1).valid(), "MD ignores MM-only controls");
		}
		std::remove(path.c_str());
	}
}

int main()
{
	try
	{
		testDispatch();
		testLearnButtonFromNote();
		testLearnButtonFromController();
		testLearnEncoder();
		testLearnMovesAnExistingSource();
		testLearnPush();
		testHeldStepWhileTurning();
		testLearnHonoursTheChannel();
		testCancelAndClear();
		testOnlyAvailableControlsCanBeLearned();
		testModeAndReset();
		testPersistence();
		testModelsKeepSeparateFiles();
	}
	catch(const std::exception& _e)
	{
		std::cerr << "mdPanelMidiControllerTest failed: " << _e.what() << '\n';
		return 1;
	}
	std::cout << "mdPanelMidiControllerTest passed\n";
	return 0;
}

#define DOCTEST_CONFIG_WITH_VARIADIC_MACROS
#include <doctest/doctest.h>

#include "Audio/AudioEngine.h"

TEST_SUITE("AudioEngine")
{
    TEST_CASE("AudioEngine singleton is accessible")
    {
        auto& engine = Limitless::Audio::AudioEngine::GetInstance();
        (void)engine;
    }

    TEST_CASE("AudioEngine reports not initialized before Initialize")
    {
        auto& engine = Limitless::Audio::AudioEngine::GetInstance();
        engine.Shutdown();
        CHECK_FALSE(engine.IsInitialized());
    }

    TEST_CASE("AudioEngine master volume defaults to 1.0")
    {
        auto& engine = Limitless::Audio::AudioEngine::GetInstance();
        CHECK(engine.GetMasterVolume() == doctest::Approx(1.0f));
    }

    TEST_CASE("AudioEngine SetMasterVolume updates value")
    {
        auto& engine = Limitless::Audio::AudioEngine::GetInstance();

        engine.SetMasterVolume(0.5f);
        CHECK(engine.GetMasterVolume() == doctest::Approx(0.5f));

        engine.SetMasterVolume(0.0f);
        CHECK(engine.GetMasterVolume() == doctest::Approx(0.0f));

        engine.SetMasterVolume(1.0f);
        CHECK(engine.GetMasterVolume() == doctest::Approx(1.0f));
    }

    TEST_CASE("AudioEngine Stop with invalid voice id does not crash")
    {
        auto& engine = Limitless::Audio::AudioEngine::GetInstance();
        CHECK_NOTHROW(engine.Stop(0));
        CHECK_NOTHROW(engine.Stop(999));
    }

    TEST_CASE("AudioEngine IsVoiceActive returns false for unknown voice")
    {
        auto& engine = Limitless::Audio::AudioEngine::GetInstance();
        CHECK_FALSE(engine.IsVoiceActive(0));
        CHECK_FALSE(engine.IsVoiceActive(12345));
    }

    TEST_CASE("AudioEngine StopAll does not crash when not initialized")
    {
        auto& engine = Limitless::Audio::AudioEngine::GetInstance();
        engine.Shutdown();
        CHECK_NOTHROW(engine.StopAll());
    }

    TEST_CASE("AudioEngine Shutdown is idempotent")
    {
        auto& engine = Limitless::Audio::AudioEngine::GetInstance();
        CHECK_NOTHROW(engine.Shutdown());
        CHECK_NOTHROW(engine.Shutdown());
        CHECK_FALSE(engine.IsInitialized());
    }

    TEST_CASE("AudioEngine mixer group volume defaults to 1.0 for unknown groups")
    {
        auto& engine = Limitless::Audio::AudioEngine::GetInstance();
        float vol = engine.GetMixerGroupVolume("NonexistentGroup");
        CHECK(vol == doctest::Approx(1.0f));
    }

    TEST_CASE("AudioEngine SetMixerGroupVolume updates the group fader")
    {
        auto& engine = Limitless::Audio::AudioEngine::GetInstance();

        engine.SetMixerGroupVolume("SFX", 0.75f);
        CHECK(engine.GetMixerGroupVolume("SFX") == doctest::Approx(0.75f));

        engine.SetMixerGroupVolume("Music", 0.3f);
        CHECK(engine.GetMixerGroupVolume("Music") == doctest::Approx(0.3f));

        // Restore defaults
        engine.SetMixerGroupVolume("SFX", 1.0f);
        engine.SetMixerGroupVolume("Music", 1.0f);
    }

    TEST_CASE("AudioEngine reverb send defaults to 0.0 for unknown groups")
    {
        auto& engine = Limitless::Audio::AudioEngine::GetInstance();
        float send = engine.GetMixerGroupReverbSend("SomeGroup");
        CHECK(send == doctest::Approx(0.0f));
    }

    TEST_CASE("AudioEngine SetMixerGroupReverbSend updates the send level")
    {
        auto& engine = Limitless::Audio::AudioEngine::GetInstance();

        engine.SetMixerGroupReverbSend("SFX", 0.4f);
        CHECK(engine.GetMixerGroupReverbSend("SFX") == doctest::Approx(0.4f));

        engine.SetMixerGroupReverbSend("SFX", 0.0f);
        CHECK(engine.GetMixerGroupReverbSend("SFX") == doctest::Approx(0.0f));
    }

    TEST_CASE("AudioCommand defaults are well-formed")
    {
        Limitless::Audio::AudioCommand cmd;

        CHECK(cmd.Type == Limitless::Audio::AudioCommandType::StopVoice);
        CHECK(cmd.VoiceId == 0);
        CHECK(cmd.Clip == nullptr);
        CHECK(cmd.Volume == doctest::Approx(1.0f));
        CHECK(cmd.Pan == doctest::Approx(0.0f));
        CHECK(cmd.Pitch == doctest::Approx(1.0f));
        CHECK(cmd.Loop == false);
        CHECK(cmd.MixerGroup == "Master");
    }
}

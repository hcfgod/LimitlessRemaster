#define DOCTEST_CONFIG_WITH_VARIADIC_MACROS
#include <doctest/doctest.h>

#include "Scripting/CoroutineTypes.h"

TEST_SUITE("Coroutine Types")
{
    TEST_CASE("WaitForSeconds stores duration")
    {
        Limitless::WaitForSeconds wait(2.5f);
        CHECK(wait.DurationSeconds == doctest::Approx(2.5f));
    }

    TEST_CASE("WaitForSeconds default is zero")
    {
        Limitless::WaitForSeconds wait;
        CHECK(wait.DurationSeconds == doctest::Approx(0.0f));
    }

    TEST_CASE("WaitForFrames stores frame count")
    {
        Limitless::WaitForFrames wait(5u);
        CHECK(wait.FrameCount == 5u);
    }

    TEST_CASE("WaitForFrames default is one frame")
    {
        Limitless::WaitForFrames wait;
        CHECK(wait.FrameCount == 1u);
    }

    TEST_CASE("WaitForNextFrame returns WaitForFrames with correct count")
    {
        auto wait = Limitless::WaitForNextFrame();
        CHECK(wait.FrameCount == 1u);

        auto wait3 = Limitless::WaitForNextFrame(3u);
        CHECK(wait3.FrameCount == 3u);
    }

    TEST_CASE("CoroutineHandle default is invalid")
    {
        Limitless::CoroutineHandle handle;

        CHECK(handle.Identifier == 0);
        CHECK_FALSE(handle.IsValid());
        CHECK_FALSE(static_cast<bool>(handle));
    }

    TEST_CASE("CoroutineHandle with nonzero identifier is valid")
    {
        Limitless::CoroutineHandle handle;
        handle.Identifier = 42;

        CHECK(handle.IsValid());
        CHECK(static_cast<bool>(handle));
    }

    TEST_CASE("CoroutineHandle equality comparison")
    {
        Limitless::CoroutineHandle a;
        a.Identifier = 1;
        Limitless::CoroutineHandle b;
        b.Identifier = 1;
        Limitless::CoroutineHandle c;
        c.Identifier = 2;

        CHECK(a == b);
        CHECK_FALSE(a == c);
    }

    TEST_CASE("CoroutineRoutine default constructed is invalid and completed")
    {
        Limitless::CoroutineRoutine routine;

        CHECK_FALSE(routine.IsValid());
        CHECK(routine.IsCompleted());
        CHECK(routine.GetPromise() == nullptr);
    }

    TEST_CASE("CoroutineRoutine Resume on default returns false")
    {
        Limitless::CoroutineRoutine routine;
        CHECK(routine.Resume() == false);
    }

    // Helper coroutine that yields WaitForSeconds then completes.
    static Limitless::CoroutineRoutine YieldSecondsCoroutine()
    {
        co_yield Limitless::WaitForSeconds(1.5f);
    }

    TEST_CASE("CoroutineRoutine from coroutine function is valid and resumable")
    {
        auto routine = YieldSecondsCoroutine();

        CHECK(routine.IsValid());
        CHECK_FALSE(routine.IsCompleted());

        bool stillRunning = routine.Resume();
        CHECK(stillRunning);

        auto* promise = routine.GetPromise();
        REQUIRE(promise != nullptr);
        CHECK(promise->LastYieldKind == Limitless::CoroutineRoutine::YieldKind::WaitForSeconds);
        CHECK(promise->WaitDurationSeconds == doctest::Approx(1.5f));

        bool completed = !routine.Resume();
        CHECK(completed);
        CHECK(routine.IsCompleted());
    }

    // Helper coroutine that yields WaitForFrames.
    static Limitless::CoroutineRoutine YieldFramesCoroutine()
    {
        co_yield Limitless::WaitForFrames(3u);
    }

    TEST_CASE("CoroutineRoutine yields WaitForFrames correctly")
    {
        auto routine = YieldFramesCoroutine();

        REQUIRE(routine.IsValid());
        routine.Resume();

        auto* promise = routine.GetPromise();
        REQUIRE(promise != nullptr);
        CHECK(promise->LastYieldKind == Limitless::CoroutineRoutine::YieldKind::WaitForFrames);
        CHECK(promise->WaitFrameCount == 3u);
    }

    // Helper coroutine that yields nullptr (equivalent to WaitForNextFrame).
    static Limitless::CoroutineRoutine YieldNullptrCoroutine()
    {
        co_yield nullptr;
    }

    TEST_CASE("CoroutineRoutine yields nullptr as WaitForFrames(1)")
    {
        auto routine = YieldNullptrCoroutine();

        REQUIRE(routine.IsValid());
        routine.Resume();

        auto* promise = routine.GetPromise();
        REQUIRE(promise != nullptr);
        CHECK(promise->LastYieldKind == Limitless::CoroutineRoutine::YieldKind::WaitForFrames);
        CHECK(promise->WaitFrameCount == 1u);
    }

    // Helper coroutine that yields a float directly.
    static Limitless::CoroutineRoutine YieldFloatCoroutine()
    {
        co_yield 0.5f;
    }

    TEST_CASE("CoroutineRoutine yields float as WaitForSeconds")
    {
        auto routine = YieldFloatCoroutine();

        REQUIRE(routine.IsValid());
        routine.Resume();

        auto* promise = routine.GetPromise();
        REQUIRE(promise != nullptr);
        CHECK(promise->LastYieldKind == Limitless::CoroutineRoutine::YieldKind::WaitForSeconds);
        CHECK(promise->WaitDurationSeconds == doctest::Approx(0.5f));
    }

    // Helper coroutine that yields a uint32_t directly.
    static Limitless::CoroutineRoutine YieldUint32Coroutine()
    {
        co_yield 10u;
    }

    TEST_CASE("CoroutineRoutine yields uint32_t as WaitForFrames")
    {
        auto routine = YieldUint32Coroutine();

        REQUIRE(routine.IsValid());
        routine.Resume();

        auto* promise = routine.GetPromise();
        REQUIRE(promise != nullptr);
        CHECK(promise->LastYieldKind == Limitless::CoroutineRoutine::YieldKind::WaitForFrames);
        CHECK(promise->WaitFrameCount == 10u);
    }

    // Helper coroutine that yields multiple times.
    static Limitless::CoroutineRoutine MultiYieldCoroutine()
    {
        co_yield Limitless::WaitForSeconds(1.0f);
        co_yield Limitless::WaitForFrames(2u);
        co_yield nullptr;
    }

    TEST_CASE("CoroutineRoutine multiple yields are processed in order")
    {
        auto routine = MultiYieldCoroutine();
        REQUIRE(routine.IsValid());

        // First resume: reaches WaitForSeconds(1.0f)
        CHECK(routine.Resume());
        REQUIRE(routine.GetPromise() != nullptr);
        CHECK(routine.GetPromise()->LastYieldKind == Limitless::CoroutineRoutine::YieldKind::WaitForSeconds);
        CHECK(routine.GetPromise()->WaitDurationSeconds == doctest::Approx(1.0f));

        // Second resume: reaches WaitForFrames(2)
        CHECK(routine.Resume());
        CHECK(routine.GetPromise()->LastYieldKind == Limitless::CoroutineRoutine::YieldKind::WaitForFrames);
        CHECK(routine.GetPromise()->WaitFrameCount == 2u);

        // Third resume: reaches nullptr yield
        CHECK(routine.Resume());
        CHECK(routine.GetPromise()->LastYieldKind == Limitless::CoroutineRoutine::YieldKind::WaitForFrames);
        CHECK(routine.GetPromise()->WaitFrameCount == 1u);

        // Fourth resume: coroutine completes
        CHECK_FALSE(routine.Resume());
        CHECK(routine.IsCompleted());
    }

    TEST_CASE("CoroutineRoutine move semantics transfer ownership")
    {
        auto routine = YieldSecondsCoroutine();
        REQUIRE(routine.IsValid());

        Limitless::CoroutineRoutine moved = std::move(routine);
        CHECK_FALSE(routine.IsValid());  // NOLINT(bugprone-use-after-move)
        CHECK(moved.IsValid());

        moved.Resume();
        auto* promise = moved.GetPromise();
        REQUIRE(promise != nullptr);
        CHECK(promise->WaitDurationSeconds == doctest::Approx(1.5f));
    }

    TEST_CASE("CoroutineRoutine move assignment cleans up previous handle")
    {
        auto routineA = YieldSecondsCoroutine();
        auto routineB = YieldFramesCoroutine();

        routineA = std::move(routineB);
        CHECK(routineA.IsValid());
        CHECK_FALSE(routineB.IsValid());  // NOLINT(bugprone-use-after-move)

        routineA.Resume();
        CHECK(routineA.GetPromise()->LastYieldKind == Limitless::CoroutineRoutine::YieldKind::WaitForFrames);
    }
}

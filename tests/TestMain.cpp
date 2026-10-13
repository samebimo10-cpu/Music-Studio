// Runs every JUCE UnitTest registered in the test files. Exit code 1 on any failure.

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);

    // JUCE's own module tests are compiled in too; run ours unless asked otherwise.
    runner.runTestsInCategory (argc > 1 ? juce::String (argv[1]) : juce::String ("Studio"));

    int failures = 0, passes = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        auto* r = runner.getResult (i);
        failures += r->failures;
        passes += r->passes;
    }
    std::cout << "\n" << passes << " checks passed, " << failures << " failed." << std::endl;
    return failures == 0 ? 0 : 1;
}

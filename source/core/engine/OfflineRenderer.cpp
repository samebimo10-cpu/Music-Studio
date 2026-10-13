#include "OfflineRenderer.h"
#include "Metronome.h"
#include "../dsp/DspCommon.h"
#include "../mastering/MasteringChain.h"

namespace studio
{

juce::AudioBuffer<float> OfflineRenderer::render (const Project& project, const RenderRequest& req,
                                                  std::function<bool (float)> progress)
{
    GraphOptions opts;
    opts.sampleRate = req.sampleRate;
    opts.maxBlockSize = req.blockSize;
    opts.includeMastering = req.includeMastering;
    opts.trackFilter = req.trackFilter;
    opts.projectFolder = req.projectFolder;

    MixGraph graph (project, opts);
    const auto& tm = graph.getTempoMap();

    const double endBeat = req.endBeat > 0 ? req.endBeat : project.lengthInBeats();
    const juce::int64 start = tm.beatsToSamples (req.startBeat, req.sampleRate);
    const juce::int64 end = tm.beatsToSamples (endBeat, req.sampleRate) + (juce::int64) (req.tailSeconds * req.sampleRate);

    // The master limiter has look-ahead latency: render a little extra and drop the start.
    const int masterLatency = req.includeMastering && graph.getMasteringChain() != nullptr ? graph.getMasteringChain()->getLatencySamples() : 0;
    const juce::int64 total = juce::jmax ((juce::int64) 1, end - start);
    if (total > (juce::int64) std::numeric_limits<int>::max() / 2)
        return {};

    juce::AudioBuffer<float> out (2, (int) total);
    out.clear();
    juce::AudioBuffer<float> block (2, req.blockSize);
    Metronome click;
    click.prepare (req.sampleRate);
    click.setVolume (0.7f);

    graph.requestChase();
    const juce::int64 renderLen = total + masterLatency;
    for (juce::int64 pos = 0; pos < renderLen; pos += req.blockSize)
    {
        const int n = (int) juce::jmin ((juce::int64) req.blockSize, renderLen - pos);
        block.clear();
        graph.process (block, start + pos, n, true, nullptr);

        // Copy into the output, shifted back by the master latency.
        const juce::int64 dst = pos - masterLatency;
        for (int i = 0; i < n; ++i)
        {
            const juce::int64 d = dst + i;
            if (d < 0 || d >= total) continue;
            for (int ch = 0; ch < 2; ++ch)
                out.setSample (ch, (int) d, block.getSample (ch, i));
        }

        if (progress && (pos / req.blockSize) % 64 == 0)
            if (! progress ((float) pos / (float) renderLen))
                return {};
    }

    if (req.includeClick)
    {
        if (req.clickOnLeftOnly)
        {
            // Performance track: music summed to mono on the right, click on the left.
            for (int i = 0; i < out.getNumSamples(); ++i)
            {
                const float mono = 0.5f * (out.getSample (0, i) + out.getSample (1, i));
                out.setSample (1, i, mono);
                out.setSample (0, i, 0.0f);
            }
            click.render (out, 0, out.getNumSamples(), start, tm, project.timeSigNumerator, 0);
        }
        else
            click.render (out, 0, out.getNumSamples(), start, tm, project.timeSigNumerator);
    }

    if (progress) progress (1.0f);
    return out;
}

juce::AudioBuffer<float> OfflineRenderer::renderTrack (const Project& project, const juce::String& trackId, const RenderRequest& request)
{
    auto req = request;
    req.includeMastering = false;
    req.trackFilter = [trackId] (const Track& t) { return t.id == trackId; };
    return render (project, req);
}

} // namespace studio
